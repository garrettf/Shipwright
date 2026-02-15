#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace SOH {

struct GameSpeedTimeStretchStats {
    uint64_t inputFramesPushed = 0;
    uint64_t outputFramesPulled = 0;
    uint64_t underrunFrames = 0;
};

class GameSpeedTimeStretch {
  public:
    void Reset();

    void Configure(int32_t sampleRate, int32_t channels);
    void SetSpeed(float speed);

    void PushInterleaved(const int16_t* samples, size_t frames);
    size_t PullInterleaved(int16_t* outSamples, size_t requestedFrames);

    const GameSpeedTimeStretchStats& GetStats() const;

  private:
    size_t PopFrame(int16_t* outSamples);

    int32_t mSampleRate = 32000;
    int32_t mChannels = 2;
    float mSpeed = 1.0f;
    std::deque<int16_t> mInputFifo;
    GameSpeedTimeStretchStats mStats;
};

bool GameSpeedTimeStretch_SelfCheck();

} // namespace SOH
