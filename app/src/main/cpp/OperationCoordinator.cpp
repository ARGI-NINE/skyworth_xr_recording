#include "OperationCoordinator.h"
#include "NativeLogger.h"

namespace operation {
namespace { constexpr const char* kTag = "OperationCoordinator"; }

Coordinator& Coordinator::Instance() { static Coordinator value; return value; }

void Coordinator::Start(MediaActions actions) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    actions_ = std::move(actions);
    state_ = Snapshot{};
    running_ = true;
    worker_ = std::thread(&Coordinator::Run, this);
}

void Coordinator::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
        queue_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::unique_lock<std::mutex> lock(mutex_);
    taskCv_.wait(lock, [this] { return activeTasks_ == 0; });
}

void Coordinator::HandleRecordStart(RecordOrigin origin, const char* reason) {
    Enqueue({EventType::RECORD_START, origin, reason ? reason : "record start"});
}
void Coordinator::HandleRecordStop(const char* reason) {
    Enqueue({EventType::RECORD_STOP, RecordOrigin::PHONE, reason ? reason : "record stop"});
}
void Coordinator::HandlePreviewStart() { Enqueue({EventType::PREVIEW_START, RecordOrigin::PHONE, "preview start"}); }
void Coordinator::HandlePreviewStop() { Enqueue({EventType::PREVIEW_STOP, RecordOrigin::PHONE, "preview stop"}); }
void Coordinator::HandleRecordToggle(const char* reason) {
    Enqueue({EventType::TOGGLE, RecordOrigin::DEVICE_BUTTON, reason ? reason : "device toggle"});
}
void Coordinator::HandleNetworkError(const char* reason) {
    Enqueue({EventType::NETWORK_ERROR, RecordOrigin::PHONE,
             reason ? reason : "network error"});
}

void Coordinator::Enqueue(Event event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        queue_.push_back(std::move(event));
    }
    cv_.notify_one();
}

Snapshot Coordinator::GetSnapshot() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
bool Coordinator::IsRecording(Mode m) { return m == Mode::LOCAL_RECORD || m == Mode::LOCAL_RECORD_WITH_PREVIEW || m == Mode::PHONE_RECORD; }

Snapshot Coordinator::Commit(Mode mode, Phase phase) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.mode = mode; state_.phase = phase; ++state_.revision;
    return state_;
}
void Coordinator::Notify(const Snapshot& s) { if (actions_.stateChanged) actions_.stateChanged(s); }

void Coordinator::RunAsync(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++activeTasks_;
    }
    std::thread([this, task] {
        task();
        std::lock_guard<std::mutex> lock(mutex_);
        --activeTasks_;
        taskCv_.notify_all();
    }).detach();
}

void Coordinator::Run() {
    for (;;) {
        Event event;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_) return;
            event = std::move(queue_.front()); queue_.pop_front();
        }
        Process(event);
    }
}

void Coordinator::Process(const Event& event) {
    Snapshot current = GetSnapshot();
    if (event.type == EventType::NETWORK_ERROR) { Notify(current); return; }
    if (event.type == EventType::RECORD_START_DONE) {
        if (current.revision != event.revision || current.phase != Phase::STARTING) return;
        if (event.success) Notify(Commit(event.target, Phase::STABLE));
        else {
            Notify(Commit(event.rollback, Phase::ERROR));
            Notify(Commit(event.rollback, Phase::STABLE));
        }
        return;
    }
    if (event.type == EventType::RECORD_STOP_DONE) {
        if (current.revision == event.revision && current.phase == Phase::STOPPING)
            Notify(Commit(Mode::IDLE, Phase::STABLE));
        return;
    }
    if (current.phase == Phase::STOPPING) { Notify(current); return; }
    if (event.type == EventType::TOGGLE) {
        const bool stopping = current.phase == Phase::STARTING || IsRecording(current.mode);
        NATIVE_LOGI(kTag,
                    "event=device_record_toggle action=%s mode=%u phase=%u revision=%llu reason=%s",
                    stopping ? "stop" : "start",
                    static_cast<unsigned>(current.mode),
                    static_cast<unsigned>(current.phase),
                    static_cast<unsigned long long>(current.revision),
                    event.reason.c_str());
        if (stopping) {
            Process({EventType::RECORD_STOP, RecordOrigin::DEVICE_BUTTON, event.reason});
        } else {
            Process({EventType::RECORD_START, RecordOrigin::DEVICE_BUTTON, event.reason});
        }
        return;
    }
    if (event.type == EventType::RECORD_START) {
        if (current.phase != Phase::STABLE || IsRecording(current.mode)) { Notify(current); return; }
        const bool keepPreview = current.mode == Mode::PHONE_PREVIEW;
        const Mode target = event.origin == RecordOrigin::PHONE ? Mode::PHONE_RECORD
                : (keepPreview ? Mode::LOCAL_RECORD_WITH_PREVIEW : Mode::LOCAL_RECORD);
        Snapshot starting = Commit(target, Phase::STARTING); Notify(starting);
        const MediaActions actions = actions_;
        RunAsync([this, actions, target, starting, keepPreview] {
            const bool ok = actions.startRecording && actions.startRecording(target, starting.revision);
            Enqueue({EventType::RECORD_START_DONE, RecordOrigin::PHONE, "start done",
                     starting.revision, ok, target,
                     keepPreview ? Mode::PHONE_PREVIEW : Mode::IDLE});
        });
        return;
    }
    if (event.type == EventType::RECORD_STOP) {
        if (current.phase == Phase::STARTING || IsRecording(current.mode)) {
            Snapshot stopping = Commit(current.mode, Phase::STOPPING); Notify(stopping);
            const MediaActions actions = actions_;
            const std::string stopReason = event.reason;
            RunAsync([this, actions, stopping, stopReason] {
                if (actions.stopRecording) actions.stopRecording(stopping.revision, stopReason);
                Enqueue({EventType::RECORD_STOP_DONE, RecordOrigin::PHONE, "stop done",
                         stopping.revision, true});
            });
        } else Notify(current);
        return;
    }
    if (current.phase != Phase::STABLE) { Notify(current); return; }
    if (event.type == EventType::PREVIEW_START) {
        if (current.mode == Mode::IDLE || current.mode == Mode::LOCAL_RECORD) {
            const Mode target = current.mode == Mode::IDLE ? Mode::PHONE_PREVIEW : Mode::LOCAL_RECORD_WITH_PREVIEW;
            if (actions_.startPreview && actions_.startPreview(target, current.revision + 1)) Notify(Commit(target, Phase::STABLE));
            else Notify(current);
        } else Notify(current);
    } else if (event.type == EventType::PREVIEW_STOP) {
        if (current.mode == Mode::PHONE_PREVIEW || current.mode == Mode::LOCAL_RECORD_WITH_PREVIEW) {
            const Mode target = current.mode == Mode::PHONE_PREVIEW ? Mode::IDLE : Mode::LOCAL_RECORD;
            if (actions_.stopPreview) actions_.stopPreview(target, current.revision + 1);
            Notify(Commit(target, Phase::STABLE));
        } else Notify(current);
    }
}
} // namespace operation
