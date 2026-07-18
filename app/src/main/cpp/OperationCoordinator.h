#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace operation {

enum class Mode : uint32_t {
    IDLE = 0,
    PHONE_PREVIEW = 1,
    LOCAL_RECORD = 2,
    LOCAL_RECORD_WITH_PREVIEW = 3,
    PHONE_RECORD = 4,
};

enum class Phase : uint32_t {
    STABLE = 0,
    STARTING = 1,
    STOPPING = 2,
    ERROR = 3,
};

enum class RecordOrigin { PHONE, DEVICE_BUTTON, ADB };

struct Snapshot {
    Mode mode = Mode::IDLE;
    Phase phase = Phase::STABLE;
    uint64_t revision = 0;
};

struct MediaActions {
    // Blocking media work. These callbacks run on the coordinator worker, never
    // while the state mutex is held.
    std::function<bool(Mode, uint64_t, bool)> startRecording;
    std::function<void(uint64_t, const std::string&)> stopRecording;
    std::function<bool(Mode, uint64_t)> startPreview;
    std::function<void(Mode, uint64_t)> stopPreview;
    std::function<bool()> isControlConnected;
    std::function<void(const Snapshot&)> stateChanged;
};

class Coordinator {
public:
    static Coordinator& Instance();
    void Start(MediaActions actions);
    void Stop();

    void HandleRecordStart(RecordOrigin origin, const char* reason);
    void HandleRecordStop(const char* reason);
    void HandlePreviewStart();
    void HandlePreviewStop();
    void HandleRecordToggle(const char* reason);
    void HandleNetworkError(const char* reason);
    void HandleControlDisconnected(const char* reason);
    Snapshot GetSnapshot() const;

private:
    enum class EventType { RECORD_START, RECORD_STOP, PREVIEW_START, PREVIEW_STOP, TOGGLE,
                           RECORD_START_DONE, RECORD_STOP_DONE,
                           PREVIEW_START_DONE, PREVIEW_STOP_DONE,
                           NETWORK_ERROR, CONTROL_DISCONNECTED };
    struct Event {
        EventType type;
        RecordOrigin origin;
        std::string reason;
        uint64_t revision = 0;
        bool success = false;
        Mode target = Mode::IDLE;
        Mode rollback = Mode::IDLE;
    };
    void Enqueue(Event event);
    void Run();
    void Process(const Event& event);
    Snapshot Commit(Mode mode, Phase phase);
    void Notify(const Snapshot& snapshot);
    void RunAsync(std::function<void()> task);
    static bool IsRecording(Mode mode);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Event> queue_;
    Snapshot state_;
    MediaActions actions_;
    bool running_ = false;
    std::thread worker_;
    std::condition_variable taskCv_;
    uint32_t activeTasks_ = 0;
};

} // namespace operation
