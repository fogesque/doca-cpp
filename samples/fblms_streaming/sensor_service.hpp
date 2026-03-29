#pragma once

#include <cmath>
#include <cstdint>

#include <doca-cpp/rdma/rdma_buffer_view.hpp>
#include <doca-cpp/rdma/rdma_stream_service.hpp>

namespace sample
{

///
/// @brief
/// CPU client service that generates synthetic sensor data (sine waves at various frequencies).
/// Fills each buffer with sensor samples simulating noise reference channels.
///
class SensorDataProducer : public doca::rdma::IRdmaStreamService
{
public:
    /// @brief Creates sensor data producer for given channel index and sample rate
    explicit SensorDataProducer(uint32_t channelIndex, float sampleRate)
        : channelIndex(channelIndex), sampleRate(sampleRate), sampleCounter(0)
    {
    }

    void OnBuffer(doca::rdma::RdmaBufferView buffer) override
    {
        auto * data = buffer.DataAs<float>();
        const auto count = buffer.Count<float>();

        // Generate sine wave at channel-specific frequency
        const auto frequency = 100.0f + static_cast<float>(this->channelIndex) * 250.0f;
        const auto amplitude = 1.0f;
        constexpr auto twoPi = 2.0f * 3.14159265358979f;

        for (std::size_t i = 0; i < count; ++i) {
            const auto t = static_cast<float>(this->sampleCounter + i) / this->sampleRate;
            data[i] = amplitude * std::sin(twoPi * frequency * t);
        }

        this->sampleCounter += count;
    }

private:
    /// @brief Channel index for frequency selection
    uint32_t channelIndex = 0;
    /// @brief Sample rate in Hz
    float sampleRate = 8000.0f;
    /// @brief Running sample counter for continuous phase
    uint64_t sampleCounter = 0;
};

}  // namespace sample
