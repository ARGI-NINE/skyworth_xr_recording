#pragma once

#include <string>

struct AMediaCodec;

namespace protocol_adapter {

void Start(const std::string& externalFilesDir);

void Stop();

void OnRecordingSessionStarted();

void OnRgbEncoderReady(AMediaCodec* codec);

void OnRecordingSessionStopped();

// Media sink controls used only by the authoritative operation coordinator.
void SetStreamingEnabled(bool enabled);
void NotifyAuthoritativeStateChanged();

}  // namespace protocol_adapter
