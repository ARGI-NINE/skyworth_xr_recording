#pragma once

#include <string>

struct AMediaCodec;
namespace SXR { class IRgbEncodedSink; }

namespace protocol_adapter {

void Start(const std::string& externalFilesDir);

void Stop();

void OnRgbEncoderReady(AMediaCodec* codec);

void OnRgbEncoderStopping();

void OnRgbEncoderStopped();

SXR::IRgbEncodedSink* GetRgbEncodedSink();

bool IsControlClientConnected();

void NotifyAuthoritativeStateChanged();

}  // namespace protocol_adapter
