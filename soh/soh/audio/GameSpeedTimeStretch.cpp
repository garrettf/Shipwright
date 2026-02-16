#include "GameSpeedTimeStretch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace SOH {

static constexpr int32_t WSOLA_WINDOW_MS = 20;
static constexpr int32_t WSOLA_SEEK_MS = 30;
static constexpr float WSOLA_TIMELINE_PENALTY_PER_FRAME = 0.00025f;

void GameSpeedTimeStretch::Reset() {
    mInputFifo.clear();
    mOutputFifo.clear();
    ResetSynthesisState();
    mStats = {};
}

void GameSpeedTimeStretch::Configure(int32_t sampleRate, int32_t channels) {
    const int32_t newSampleRate = std::max<int32_t>(sampleRate, 8000);
    const int32_t newChannels = std::clamp<int32_t>(channels, 1, 8);

    if (mConfigured && mSampleRate == newSampleRate && mChannels == newChannels) {
        return;
    }

    mSampleRate = newSampleRate;
    mChannels = newChannels;
    RecomputeParameters();
    ResetSynthesisState();
    mConfigured = true;
}

void GameSpeedTimeStretch::SetSpeed(float speed) {
    const bool wasWsola = mSpeed > 1.01f;

    if (std::isnan(speed) || std::isinf(speed)) {
        mSpeed = 1.0f;
    } else {
        mSpeed = std::clamp(speed, 0.125f, 8.0f);
    }

    const bool nowWsola = mSpeed > 1.01f;
    if (wasWsola != nowWsola) {
        // Switching between WSOLA and direct-copy should not keep stale buffered audio,
        // otherwise latency and timeline discontinuities can accumulate.
        Reset();
    }
}

void GameSpeedTimeStretch::PushInterleaved(const int16_t* samples, size_t frames) {
    if (samples == nullptr || frames == 0 || mChannels <= 0) {
        return;
    }

    const size_t sampleCount = frames * static_cast<size_t>(mChannels);
    for (size_t i = 0; i < sampleCount; i++) {
        mInputFifo.push_back(samples[i]);
    }

    mStats.inputFramesPushed += frames;
}

size_t GameSpeedTimeStretch::PullInterleaved(int16_t* outSamples, size_t requestedFrames) {
    if (outSamples == nullptr || requestedFrames == 0 || mChannels <= 0) {
        return 0;
    }

    const size_t requestedSamples = requestedFrames * static_cast<size_t>(mChannels);
    std::memset(outSamples, 0, requestedSamples * sizeof(int16_t));

    GenerateOutputFrames(requestedFrames);

    const size_t producedFrames = std::min(requestedFrames, FramesInOutput());
    for (size_t i = 0; i < producedFrames * static_cast<size_t>(mChannels); i++) {
        outSamples[i] = mOutputFifo.front();
        mOutputFifo.pop_front();
    }

    mStats.outputFramesPulled += producedFrames;
    if (producedFrames < requestedFrames) {
        mStats.underrunFrames += (requestedFrames - producedFrames);
    }

    return producedFrames;
}

const GameSpeedTimeStretchStats& GameSpeedTimeStretch::GetStats() const {
    return mStats;
}

size_t GameSpeedTimeStretch::GetInputFramesBuffered() const {
    return FramesInInput();
}

size_t GameSpeedTimeStretch::GetOutputFramesBuffered() const {
    return FramesInOutput();
}

int16_t GameSpeedTimeStretch::FloatToS16(float value) {
    const float clamped = std::clamp(value, -32768.0f, 32767.0f);
    return static_cast<int16_t>(std::lround(clamped));
}

void GameSpeedTimeStretch::RecomputeParameters() {
    mWindowFrames = std::max<int32_t>(64, mSampleRate * WSOLA_WINDOW_MS / 1000);
    if (mWindowFrames % 2 != 0) {
        mWindowFrames += 1;
    }

    mOverlapFrames = mWindowFrames / 2;
    mHopOutFrames = mWindowFrames - mOverlapFrames;
    mSeekFrames = std::max<int32_t>(mSampleRate * WSOLA_SEEK_MS / 1000, mOverlapFrames);
}

void GameSpeedTimeStretch::ResetSynthesisState() {
    mHasPrevOverlap = false;
    mAnalysisPosFrames = 0;
    mPrevOverlapEnergy = 1.0f;
    mPrevOverlap.assign(static_cast<size_t>(mOverlapFrames) * static_cast<size_t>(mChannels), 0.0f);
    mStats.synthesisResets++;
}

void GameSpeedTimeStretch::GenerateOutputFrames(size_t minFrames) {
    if (mSpeed > 1.01f) {
        GenerateWsolaOutputFrames(minFrames);
    } else {
        GenerateDirectOutputFrames(minFrames);
    }
}

void GameSpeedTimeStretch::GenerateDirectOutputFrames(size_t minFrames) {
    ResetSynthesisState();

    while (FramesInOutput() < minFrames && FramesInInput() > 0) {
        const size_t framesToMove = std::min(minFrames - FramesInOutput(), FramesInInput());
        const size_t samplesToMove = framesToMove * static_cast<size_t>(mChannels);

        for (size_t i = 0; i < samplesToMove; i++) {
            mOutputFifo.push_back(mInputFifo.front());
            mInputFifo.pop_front();
        }
    }
}

void GameSpeedTimeStretch::GenerateWsolaOutputFrames(size_t minFrames) {
    while (FramesInOutput() < minFrames) {
        const size_t inputFrames = FramesInInput();
        const size_t hopInFrames = HopInFrames();
        if (!mHasPrevOverlap) {
            if (inputFrames < static_cast<size_t>(mWindowFrames)) {
                return;
            }

            AppendRawHop(0);
            CaptureOverlap(static_cast<size_t>(mHopOutFrames));
            mHasPrevOverlap = true;
            mAnalysisPosFrames = hopInFrames;
            mStats.wsolaHops++;
            mStats.wsolaInputAdvanceFrames += hopInFrames;
            mStats.wsolaOutputFrames += static_cast<size_t>(mHopOutFrames);
            DiscardConsumedInput();
            continue;
        }

        if (inputFrames < static_cast<size_t>(mWindowFrames)) {
            return;
        }

        const size_t maxStart = inputFrames - static_cast<size_t>(mWindowFrames);
        if (maxStart == 0 && mAnalysisPosFrames > 0) {
            return;
        }

        const size_t expectedStart = std::min(mAnalysisPosFrames, maxStart);
        // Keep candidate search near expected timeline to avoid drifting backward and slowing tempo.
        const size_t maxBacktrack = static_cast<size_t>(std::max(1, mOverlapFrames / 4));
        const size_t searchStart = expectedStart > maxBacktrack ? (expectedStart - maxBacktrack) : 0;
        const size_t searchForward = static_cast<size_t>(std::max(1, mSeekFrames / 3));
        const size_t searchEnd = std::max(searchStart, std::min(maxStart, expectedStart + searchForward));

        size_t bestStart = searchStart;
        const auto DistancePenalty = [&](size_t candidate) -> float {
            return WSOLA_TIMELINE_PENALTY_PER_FRAME *
                   static_cast<float>(std::abs(static_cast<int64_t>(candidate) - static_cast<int64_t>(expectedStart)));
        };
        float bestScore = CorrelationScore(searchStart) - DistancePenalty(searchStart);
        for (size_t candidate = searchStart + 1; candidate <= searchEnd; candidate++) {
            const float score = CorrelationScore(candidate) - DistancePenalty(candidate);
            if (score > bestScore) {
                bestScore = score;
                bestStart = candidate;
            }
        }

        AppendBlendedHop(bestStart);
        CaptureOverlap(bestStart + static_cast<size_t>(mHopOutFrames));

        // Advance analysis cursor by requested hop-in amount, independent of local best-match location.
        mAnalysisPosFrames += hopInFrames;
        mStats.wsolaHops++;
        mStats.wsolaInputAdvanceFrames += hopInFrames;
        mStats.wsolaOutputFrames += static_cast<size_t>(mHopOutFrames);
        DiscardConsumedInput();
    }
}

size_t GameSpeedTimeStretch::FramesInInput() const {
    return mInputFifo.size() / static_cast<size_t>(mChannels);
}

size_t GameSpeedTimeStretch::FramesInOutput() const {
    return mOutputFifo.size() / static_cast<size_t>(mChannels);
}

size_t GameSpeedTimeStretch::HopInFrames() const {
    return std::max<size_t>(1, static_cast<size_t>(std::lround(mHopOutFrames * mSpeed)));
}

int16_t GameSpeedTimeStretch::ReadSample(size_t frame, size_t channel) const {
    return mInputFifo[frame * static_cast<size_t>(mChannels) + channel];
}

float GameSpeedTimeStretch::CorrelationScore(size_t candidateStartFrame) const {
    float dot = 0.0f;
    float candidateEnergy = 0.0f;
    for (int32_t i = 0; i < mOverlapFrames; i++) {
        const size_t frame = candidateStartFrame + static_cast<size_t>(i);
        const size_t overlapBase = static_cast<size_t>(i) * static_cast<size_t>(mChannels);

        for (int32_t c = 0; c < mChannels; c++) {
            const float previous = mPrevOverlap[overlapBase + static_cast<size_t>(c)];
            const float current = static_cast<float>(ReadSample(frame, static_cast<size_t>(c)));
            dot += previous * current;
            candidateEnergy += current * current;
        }
    }

    const float denom = std::sqrt(std::max(1.0f, mPrevOverlapEnergy * candidateEnergy));
    return dot / denom;
}

void GameSpeedTimeStretch::AppendRawHop(size_t segmentStartFrame) {
    for (int32_t i = 0; i < mHopOutFrames; i++) {
        const size_t frame = segmentStartFrame + static_cast<size_t>(i);
        for (int32_t c = 0; c < mChannels; c++) {
            mOutputFifo.push_back(ReadSample(frame, static_cast<size_t>(c)));
        }
    }
}

void GameSpeedTimeStretch::AppendBlendedHop(size_t segmentStartFrame) {
    for (int32_t i = 0; i < mHopOutFrames; i++) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(mHopOutFrames + 1);
        const float alpha = t;
        const float beta = 1.0f - alpha;
        const size_t frame = segmentStartFrame + static_cast<size_t>(i);
        const size_t overlapBase = static_cast<size_t>(i) * static_cast<size_t>(mChannels);

        for (int32_t c = 0; c < mChannels; c++) {
            const float previous = mPrevOverlap[overlapBase + static_cast<size_t>(c)];
            const float current = static_cast<float>(ReadSample(frame, static_cast<size_t>(c)));
            mOutputFifo.push_back(FloatToS16(previous * beta + current * alpha));
        }
    }
}

void GameSpeedTimeStretch::CaptureOverlap(size_t overlapStartFrame) {
    float energy = 0.0f;
    for (int32_t i = 0; i < mOverlapFrames; i++) {
        const size_t frame = overlapStartFrame + static_cast<size_t>(i);
        const size_t overlapBase = static_cast<size_t>(i) * static_cast<size_t>(mChannels);

        for (int32_t c = 0; c < mChannels; c++) {
            const float sample = static_cast<float>(ReadSample(frame, static_cast<size_t>(c)));
            mPrevOverlap[overlapBase + static_cast<size_t>(c)] = sample;
            energy += sample * sample;
        }
    }
    mPrevOverlapEnergy = std::max(1.0f, energy);
}

void GameSpeedTimeStretch::DiscardConsumedInput() {
    const size_t keepFrames = static_cast<size_t>(mSeekFrames + mWindowFrames);
    if (mAnalysisPosFrames <= keepFrames) {
        return;
    }

    const size_t discardFrames = mAnalysisPosFrames - keepFrames;
    const size_t discardSamples = discardFrames * static_cast<size_t>(mChannels);

    for (size_t i = 0; i < discardSamples; i++) {
        if (mInputFifo.empty()) {
            break;
        }
        mInputFifo.pop_front();
    }

    mAnalysisPosFrames -= discardFrames;
}

bool GameSpeedTimeStretch_SelfCheck() {
    GameSpeedTimeStretch stretch;
    stretch.Configure(32000, 2);
    stretch.SetSpeed(1.0f);

    int16_t input[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    stretch.PushInterleaved(input, 4);

    int16_t output[8] = {};
    const size_t produced = stretch.PullInterleaved(output, 4);

    return produced == 4 && output[0] == 1 && output[1] == 2 && output[6] == 7 && output[7] == 8;
}

} // namespace SOH
