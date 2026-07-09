#pragma once

#include <string>

struct AMediaCodec;

namespace protocol_adapter {

void Start(const std::string& externalFilesDir);

void Stop();

void OnRecordingSessionStarted();

void OnRgbEncoderReady(AMediaCodec* codec);

void OnRecordingSessionStopped();

}  // namespace protocol_adapter
