#pragma once

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <errors/errors.hpp>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/logging/logging.hpp"

namespace configs
{

inline const std::string configsFilename = "fblms_streaming_configs.yaml";

struct ChannelConfig {
    std::string deviceIbName;
    uint16_t port = 0;
};

struct SampleConfig {
    struct ServerConfig {
        std::string gpuPcieBdfAddress;
        std::vector<ChannelConfig> channels;
    };

    struct ClientConfig {
        std::string serverAddress;
        std::vector<ChannelConfig> channels;
    };

    struct StreamConfig {
        uint32_t numBuffers = 128;
        std::size_t bufferSize = 8192;
    };

    struct FblmsConfig {
        int blockLength = 1024;
        float stepSize = 1e-7f;
        float sampleRate = 8000.0f;
    };

    ServerConfig serverCfg;
    ClientConfig clientCfg;
    StreamConfig streamCfg;
    FblmsConfig fblmsCfg;

    uint32_t streamingDuration = 30;

    doca::logging::LogLevel loggingLevel = doca::logging::LogLevel::Off;
    doca::logging::DocaSdkLogLevel docaSdkLogLevel = doca::logging::DocaSdkLogLevel::DOCA_LOG_LEVEL_DISABLE;
};
using SampleConfigPtr = std::shared_ptr<SampleConfig>;

inline std::tuple<SampleConfigPtr, error> ParseSampleConfigs(const std::string & configFilename)
{
    auto cfg = std::make_shared<SampleConfig>();

    if (!std::filesystem::exists(configFilename)) {
        return { nullptr, errors::New("No config file found; make sure to add it next to executable") };
    }

    YAML::Node yamlConfig = YAML::LoadFile(configFilename);

    if (!yamlConfig.IsMap()) {
        return { nullptr, errors::New("Invalid format in YAML config file") };
    }

    // Parse sample configs
    if (auto sampleNode = yamlConfig["sample"]) {
        if (sampleNode["log-level"]) {
            cfg->loggingLevel = static_cast<doca::logging::LogLevel>(sampleNode["log-level"].as<uint32_t>());
        }
        if (sampleNode["doca-log-level"]) {
            cfg->docaSdkLogLevel =
                static_cast<doca::logging::DocaSdkLogLevel>(sampleNode["doca-log-level"].as<uint32_t>());
        }
        if (sampleNode["streaming-duration"]) {
            cfg->streamingDuration = sampleNode["streaming-duration"].as<uint32_t>();
        }
    }

    // Parse stream configs
    if (auto streamNode = yamlConfig["stream"]) {
        if (streamNode["num-buffers"]) {
            cfg->streamCfg.numBuffers = streamNode["num-buffers"].as<uint32_t>();
        }
        if (streamNode["buffer-size"]) {
            cfg->streamCfg.bufferSize = streamNode["buffer-size"].as<std::size_t>();
        }
    }

    // Parse FBLMS configs
    if (auto fblmsNode = yamlConfig["fblms"]) {
        if (fblmsNode["block-length"]) {
            cfg->fblmsCfg.blockLength = fblmsNode["block-length"].as<int>();
        }
        if (fblmsNode["step-size"]) {
            cfg->fblmsCfg.stepSize = fblmsNode["step-size"].as<float>();
        }
        if (fblmsNode["sample-rate"]) {
            cfg->fblmsCfg.sampleRate = fblmsNode["sample-rate"].as<float>();
        }
    }

    // Parse server configs
    if (auto serverNode = yamlConfig["server"]) {
        if (serverNode["gpu-pcie-bdf"]) {
            cfg->serverCfg.gpuPcieBdfAddress = serverNode["gpu-pcie-bdf"].as<std::string>();
        }
        if (serverNode["channels"] && serverNode["channels"].IsSequence()) {
            for (const auto & channelNode : serverNode["channels"]) {
                auto channel = ChannelConfig{};
                if (channelNode["device"]) {
                    channel.deviceIbName = channelNode["device"].as<std::string>();
                }
                if (channelNode["port"]) {
                    channel.port = channelNode["port"].as<uint16_t>();
                }
                cfg->serverCfg.channels.push_back(channel);
            }
        }
    }

    // Parse client configs
    if (auto clientNode = yamlConfig["client"]) {
        if (clientNode["server-ipv4"]) {
            cfg->clientCfg.serverAddress = clientNode["server-ipv4"].as<std::string>();
        }
        if (clientNode["channels"] && clientNode["channels"].IsSequence()) {
            for (const auto & channelNode : clientNode["channels"]) {
                auto channel = ChannelConfig{};
                if (channelNode["device"]) {
                    channel.deviceIbName = channelNode["device"].as<std::string>();
                }
                if (channelNode["server-port"]) {
                    channel.port = channelNode["server-port"].as<uint16_t>();
                }
                cfg->clientCfg.channels.push_back(channel);
            }
        }
    }

    return { cfg, nullptr };
}

inline void PrintSampleConfigs(const SampleConfigPtr cfg)
{
    std::println();
    std::println("========= Parsed configs =========");
    std::println("  Server:");
    std::println("    GPU PCIe BDF:       {}", cfg->serverCfg.gpuPcieBdfAddress);
    for (std::size_t i = 0; i < cfg->serverCfg.channels.size(); ++i) {
        std::println("    Channel {}:          {} port {}", i, cfg->serverCfg.channels[i].deviceIbName,
                     cfg->serverCfg.channels[i].port);
    }
    std::println("  Client:");
    std::println("    Server IPv4:        {}", cfg->clientCfg.serverAddress);
    for (std::size_t i = 0; i < cfg->clientCfg.channels.size(); ++i) {
        std::println("    Channel {}:          {} port {}", i, cfg->clientCfg.channels[i].deviceIbName,
                     cfg->clientCfg.channels[i].port);
    }
    std::println("  Stream:");
    std::println("    Num Buffers:        {}", cfg->streamCfg.numBuffers);
    std::println("    Buffer Size:        {} bytes", cfg->streamCfg.bufferSize);
    std::println("  FBLMS:");
    std::println("    Block Length:       {}", cfg->fblmsCfg.blockLength);
    std::println("    Step Size:          {}", cfg->fblmsCfg.stepSize);
    std::println("    Sample Rate:        {} Hz", cfg->fblmsCfg.sampleRate);
    std::println("  Streaming Duration:   {} s", cfg->streamingDuration);
    std::println("==================================");
    std::println();
}

}  // namespace configs
