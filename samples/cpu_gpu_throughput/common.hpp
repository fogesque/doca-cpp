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

#include "doca-cpp/logging/logging.hpp"

namespace configs
{

inline const std::string configsFilename = "cpu_gpu_throughput_configs.yaml";

struct SampleConfig {
    struct ServerConfig {
        std::string deviceIbName;
        std::string gpuPcieBdfAddress;
        uint16_t port = 0;
    };

    struct ClientConfig {
        std::string deviceIbName;
        std::string serverAddress;
        uint16_t serverPort = 0;
    };

    struct StreamConfig {
        uint32_t numBuffers = 64;
        std::size_t bufferSize = 33554432;
    };

    ServerConfig serverCfg;
    ClientConfig clientCfg;
    StreamConfig streamCfg;

    uint32_t measurementDuration = 10;

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
        if (sampleNode["measurement-duration"]) {
            cfg->measurementDuration = sampleNode["measurement-duration"].as<uint32_t>();
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

    // Parse server configs
    if (auto serverNode = yamlConfig["server"]) {
        if (serverNode["device"]) {
            cfg->serverCfg.deviceIbName = serverNode["device"].as<std::string>();
        }
        if (serverNode["gpu-pcie-bdf"]) {
            cfg->serverCfg.gpuPcieBdfAddress = serverNode["gpu-pcie-bdf"].as<std::string>();
        }
        if (serverNode["port"]) {
            cfg->serverCfg.port = serverNode["port"].as<uint16_t>();
        }
    }

    // Parse client configs
    if (auto clientNode = yamlConfig["client"]) {
        if (clientNode["device"]) {
            cfg->clientCfg.deviceIbName = clientNode["device"].as<std::string>();
        }
        if (clientNode["server-ipv4"]) {
            cfg->clientCfg.serverAddress = clientNode["server-ipv4"].as<std::string>();
        }
        if (clientNode["server-port"]) {
            cfg->clientCfg.serverPort = clientNode["server-port"].as<uint16_t>();
        }
    }

    return { cfg, nullptr };
}

inline void PrintSampleConfigs(const SampleConfigPtr cfg)
{
    std::println();
    std::println("========= Parsed configs =========");
    std::println("  Server:");
    std::println("    Device:             {}", cfg->serverCfg.deviceIbName);
    std::println("    GPU PCIe BDF:       {}", cfg->serverCfg.gpuPcieBdfAddress);
    std::println("    Port:               {}", cfg->serverCfg.port);
    std::println("  Client:");
    std::println("    Device:             {}", cfg->clientCfg.deviceIbName);
    std::println("    Server IPv4:        {}", cfg->clientCfg.serverAddress);
    std::println("    Server Port:        {}", cfg->clientCfg.serverPort);
    std::println("  Stream:");
    std::println("    Num Buffers:        {}", cfg->streamCfg.numBuffers);
    std::println("    Buffer Size:        {} bytes", cfg->streamCfg.bufferSize);
    std::println("  Measurement Duration: {} s", cfg->measurementDuration);
    std::println("==================================");
    std::println();
}

}  // namespace configs
