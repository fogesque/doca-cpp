/**
 * @file common.hpp
 * @brief Shared configuration for 4-channel FBLMS streaming chain sample
 */

#pragma once

#include <doca-cpp/core/buffer_view.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/rdma/stream_service.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace sample
{

/// @brief Default streaming configuration for all 4 channels
inline doca::StreamConfig DefaultStreamConfig()
{
    return {
        .numBuffers = 64,
        .bufferSize = 65536,  // 64 KB — 8192 complex float samples
        .direction = doca::StreamDirection::write,
    };
}

constexpr uint16_t BasePort = 54321;  // server 0: 54321, server 1: 54322, etc.
constexpr uint32_t NumChannels = 4;

// ────────────────────────────────────────────────────
// Client-side: sensor data producer (CPU)
// ────────────────────────────────────────────────────

/**
 * @brief Simulates a sensor producing sinusoidal signal data.
 *        Each buffer is filled with a sine wave at a channel-specific frequency.
 */
class SensorProducer : public doca::rdma::IRdmaStreamService
{
public:
    explicit SensorProducer(uint32_t channelId, float frequency)
        : channelId(channelId), frequency(frequency)
    {
    }

    void OnBuffer(doca::BufferView buffer) override
    {
        auto samples = buffer.AsSpan<std::complex<float>>();
        auto sampleOffset = this->totalSamples.fetch_add(samples.size(), std::memory_order_relaxed);

        for (std::size_t i = 0; i < samples.size(); i++) {
            auto t = static_cast<float>(sampleOffset + i) / 44100.0f;
            auto value = std::sin(2.0f * static_cast<float>(M_PI) * this->frequency * t);
            samples[i] = std::complex<float>(value, 0.0f);
        }
    }

private:
    uint32_t channelId;
    float frequency;
    std::atomic<uint64_t> totalSamples{0};
};

// ────────────────────────────────────────────────────
// Server-side: per-channel FFT processor (CPU version)
// ────────────────────────────────────────────────────

/**
 * @brief CPU-based per-channel FFT processor.
 *        In the GPU version, this would use cuFFT.
 *        For this sample, we just compute magnitude spectrum (simplified DFT).
 */
class ChannelFftProcessor : public doca::rdma::IRdmaStreamService
{
public:
    explicit ChannelFftProcessor(uint32_t channelId) : channelId(channelId) {}

    void OnBuffer(doca::BufferView buffer) override
    {
        // Simple in-place processing: compute magnitude of complex samples
        auto samples = buffer.AsSpan<std::complex<float>>();
        for (auto & s : samples) {
            auto mag = std::abs(s);
            s = std::complex<float>(mag, 0.0f);
        }

        this->buffersProcessed.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t BuffersProcessed() const
    {
        return this->buffersProcessed.load(std::memory_order_relaxed);
    }

private:
    uint32_t channelId;
    std::atomic<uint64_t> buffersProcessed{0};
};

// ────────────────────────────────────────────────────
// Server-side: FBLMS aggregate processor
// ────────────────────────────────────────────────────

/**
 * @brief Cross-channel FBLMS-style aggregate processor.
 *        Receives one buffer from each of the 4 channels (post-FFT)
 *        and performs a simplified adaptive weight update.
 *
 *        In production, this would use Thrust transforms on GPU.
 *        Here we demonstrate the interface with CPU code.
 */
class FblmsAggregate : public doca::rdma::IAggregateStreamService
{
public:
    explicit FblmsAggregate(uint32_t samplesPerBuffer)
        : weights(samplesPerBuffer, std::complex<float>(1.0f, 0.0f)), mu(0.01f)
    {
    }

    void OnAggregate(std::span<doca::BufferView> channels) override
    {
        // channels.size() == 4 (one per server in the chain)
        auto numSamples = channels[0].Count<std::complex<float>>();

        // Weighted sum across channels
        std::vector<std::complex<float>> filtered(numSamples, {0, 0});
        for (std::size_t ch = 0; ch < channels.size(); ch++) {
            auto chData = channels[ch].DataAs<std::complex<float>>();
            for (std::size_t i = 0; i < numSamples; i++) {
                filtered[i] += this->weights[i] * chData[i];
            }
        }

        // Simplified weight update: weights += mu * error
        for (std::size_t i = 0; i < numSamples; i++) {
            auto error = std::complex<float>(1.0f, 0.0f) - filtered[i];  // desired = 1.0
            this->weights[i] += this->mu * std::conj(filtered[i]) * error;
        }

        this->roundsCompleted.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t RoundsCompleted() const
    {
        return this->roundsCompleted.load(std::memory_order_relaxed);
    }

private:
    std::vector<std::complex<float>> weights;
    float mu;
    std::atomic<uint64_t> roundsCompleted{0};
};

}  // namespace sample
