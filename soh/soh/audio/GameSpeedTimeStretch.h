#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace SOH {

struct GameSpeedTimeStretchStats {
    uint64_t inputFramesPushed = 0;
    uint64_t outputFramesPulled = 0;
    uint64_t underrunFrames = 0;
    uint64_t wsolaHops = 0;
    uint64_t wsolaInputAdvanceFrames = 0;
    uint64_t wsolaOutputFrames = 0;
    uint64_t synthesisResets = 0;
};

class GameSpeedTimeStretch {
  public:
    void Reset();

    void Configure(int32_t sampleRate, int32_t channels);
    void SetSpeed(float speed);

    void PushInterleaved(const int16_t* samples, size_t frames);
    size_t PullInterleaved(int16_t* outSamples, size_t requestedFrames);

    const GameSpeedTimeStretchStats& GetStats() const;
    size_t GetInputFramesBuffered() const;
    size_t GetOutputFramesBuffered() const;

  private:
    static int16_t FloatToS16(float value);

    void RecomputeParameters();
    void ResetSynthesisState();
    void GenerateOutputFrames(size_t minFrames);
    void GenerateDirectOutputFrames(size_t minFrames);
    void GenerateWsolaOutputFrames(size_t minFrames);

    size_t FramesInInput() const;
    size_t FramesInOutput() const;
    size_t HopInFrames() const;

    int16_t ReadSample(size_t frame, size_t channel) const;
    float CorrelationScore(size_t candidateStartFrame) const;
    void AppendRawHop(size_t segmentStartFrame);
    void AppendBlendedHop(size_t segmentStartFrame);
    void CaptureOverlap(size_t overlapStartFrame);
    void DiscardConsumedInput();

    int32_t mSampleRate = 32000;
    int32_t mChannels = 2;
    float mSpeed = 1.0f;

    int32_t mWindowFrames = 640;
    int32_t mOverlapFrames = 320;
    int32_t mHopOutFrames = 320;
    int32_t mSeekFrames = 960;
    bool mConfigured = false;

    std::deque<int16_t> mInputFifo;
    std::deque<int16_t> mOutputFifo;
    std::vector<float> mPrevOverlap;
    bool mHasPrevOverlap = false;
    size_t mAnalysisPosFrames = 0;
    float mPrevOverlapEnergy = 1.0f;

    GameSpeedTimeStretchStats mStats;
};

bool GameSpeedTimeStretch_SelfCheck();

} // namespace SOH
