#include "GameSpeedTimeStretch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace SOH {

void GameSpeedTimeStretch::Reset() {
    mInputFifo.clear();
    mStats = {};
}

void GameSpeedTimeStretch::Configure(int32_t sampleRate, int32_t channels) {
    mSampleRate = std::max<int32_t>(sampleRate, 8000);
    mChannels = std::clamp<int32_t>(channels, 1, 8);
}

void GameSpeedTimeStretch::SetSpeed(float speed) {
    if (std::isnan(speed) || std::isinf(speed)) {
        mSpeed = 1.0f;
    } else {
        mSpeed = std::clamp(speed, 0.125f, 8.0f);
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

size_t GameSpeedTimeStretch::PopFrame(int16_t* outSamples) {
    const size_t neededSamples = static_cast<size_t>(mChannels);
    if (mInputFifo.size() < neededSamples) {
        return 0;
    }

    for (size_t c = 0; c < neededSamples; c++) {
        outSamples[c] = mInputFifo.front();
        mInputFifo.pop_front();
    }

    return 1;
}

size_t GameSpeedTimeStretch::PullInterleaved(int16_t* outSamples, size_t requestedFrames) {
    if (outSamples == nullptr || requestedFrames == 0 || mChannels <= 0) {
        return 0;
    }

    const size_t requestedSamples = requestedFrames * static_cast<size_t>(mChannels);
    std::memset(outSamples, 0, requestedSamples * sizeof(int16_t));

    size_t producedFrames = 0;
    int16_t* write = outSamples;
    for (; producedFrames < requestedFrames; producedFrames++) {
        if (PopFrame(write) == 0) {
            break;
        }
        write += mChannels;
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

bool GameSpeedTimeStretch_SelfCheck() {
    GameSpeedTimeStretch stretch;
    stretch.Configure(32000, 2);
    stretch.SetSpeed(2.0f);

    int16_t input[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    stretch.PushInterleaved(input, 4);

    int16_t output[8] = {};
    const size_t produced = stretch.PullInterleaved(output, 4);

    return produced == 4 && output[0] == 1 && output[1] == 2 && output[6] == 7 && output[7] == 8;
}

} // namespace SOH
