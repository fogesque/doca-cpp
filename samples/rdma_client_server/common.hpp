#pragma once

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <errors/errors.hpp>
#include <filesystem>
#include <map>
#include <string>

#include "doca-cpp/logging/logging.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

// Sample configs namespace
namespace configs
{

inline const auto loggingLevel = doca::logging::LogLevel::Trace;

inline const std::string configsFilename = "rdma_client_server_configs.yaml";

// Sample configs for server and client
struct SampleConfig {
    struct ServerConfig {
        std::string deviceServerIbName;
        std::string serverAddress;
        std::uint16_t serverPort;
    };

    struct ClientConfig {
        std::string deviceClientIbName;
    };

    ServerConfig serverCfg;
    ClientConfig clientCfg;
};
using SampleConfigPtr = std::shared_ptr<SampleConfig>;

// Parses sample configs from YAML config file
inline std::tuple<SampleConfigPtr, error> ParseSampleConfigs(const std::string & configFilename)
{
    auto cfg = std::make_shared<SampleConfig>();

    if (!std::filesystem::exists(configFilename)) {
        return { nullptr, errors::New("No config file found; make sure to add it next to executable") };
    }

    YAML::Node yamlConfig = YAML::LoadFile(configFilename);
    YAML::Node serverNode;
    YAML::Node clientNode;

    try {
        serverNode = yamlConfig["server"];
        clientNode = yamlConfig["client"];
    } catch (std::runtime_error & e) {
        return { nullptr, errors::New("Missing server or client node in YAML config file") };
    }

    if (!yamlConfig.IsMap()) {
        return { nullptr, errors::New("Invalid format in YAML config file") };
    }

    if (!serverNode && !clientNode) {
        return { nullptr, errors::New("Missing content in server or client node in YAML config file") };
    }

    // Parse server configs

    if (serverNode["device"]) {
        try {
            cfg->serverCfg.deviceServerIbName = serverNode["device"].as<std::string>();
        } catch (std::runtime_error & e) {
            return { nullptr, errors::New("Failed to parse 'device' field in server node") };
        }
    }

    if (serverNode["ipv4"]) {
        try {
            cfg->serverCfg.serverAddress = serverNode["ipv4"].as<std::string>();
        } catch (std::runtime_error & e) {
            return { nullptr, errors::New("Failed to parse 'ipv4' field in server node") };
        }
    }

    if (serverNode["port"]) {
        try {
            cfg->serverCfg.serverPort = serverNode["port"].as<uint16_t>();
        } catch (std::runtime_error & e) {
            return { nullptr, errors::New("Failed to parse 'port' field in server node") };
        }
    }

    // Parse client configs

    if (clientNode["device"]) {
        try {
            cfg->clientCfg.deviceClientIbName = clientNode["device"].as<std::string>();
        } catch (std::runtime_error & e) {
            return { nullptr, errors::New("Failed to parse 'device' field in client node") };
        }
    }

    return { cfg, nullptr };
}

// Prints parsed configs (for debugging)
inline void PrintSampleConfigs(const SampleConfigPtr cfg)
{
    std::println();
    std::println("========= Parsed configs =========");
    std::println("  Server:");
    std::println("    Device:          {}", cfg->serverCfg.deviceServerIbName);
    std::println("    IPv4:            {}", cfg->serverCfg.serverAddress);
    std::println("    Port:            {}", cfg->serverCfg.serverPort);
    std::println("  Client:");
    std::println("    Device:          {}", cfg->clientCfg.deviceClientIbName);
    std::println("==================================");
    std::println();
}

}  // namespace configs

// Endpoints declarations and create function
namespace endpoints
{
struct Config {
    std::string path = "";
    std::size_t size = 0;
    doca::rdma::RdmaEndpointType type = doca::rdma::RdmaEndpointType::receive;
};

// NOTE: soon it will be auto generated

// endpoints:
//   - path: /rdma/ep0
//     type: receive
//     buffer:
//       size: 4096 # bytes - 4KB

//   - path: /rdma/ep1
//     type: write
//     buffer:
//       size: 4194304 # bytes - 4MB

//   - path: /rdma/ep0
//     type: send
//     buffer:
//       size: 4096 # bytes - 4KB

//   - path: /rdma/ep1
//     type: read
//     buffer:
//       size: 4194304 # bytes - 4MB

inline const auto cfg0 = endpoints::Config{
    .path = "/rdma/ep0",
    .size = 4096,
    .type = doca::rdma::RdmaEndpointType::send,
};

inline const auto cfg1 = endpoints::Config{
    .path = "/rdma/ep0",
    .size = 4096,
    .type = doca::rdma::RdmaEndpointType::receive,
};

inline const auto cfg2 = endpoints::Config{
    .path = "/rdma/ep1",
    .size = 4194304,
    .type = doca::rdma::RdmaEndpointType::write,
};

inline const auto cfg3 = endpoints::Config{
    .path = "/rdma/ep1",
    .size = 4194304,
    .type = doca::rdma::RdmaEndpointType::read,
};

inline std::tuple<std::vector<doca::rdma::RdmaEndpointPtr>, error> CreateEndpoints(doca::DevicePtr device,
                                                                                   const std::vector<Config> & configs)
{
    using doca::rdma::RdmaBufferPtr;
    using doca::rdma::RdmaEndpointPath;
    using doca::rdma::RdmaEndpointPtr;

    std::vector<RdmaEndpointPtr> endpoints;
    std::map<RdmaEndpointPath, RdmaBufferPtr> buffers;

    for (const auto & config : configs) {
        // Same paths refer to one unique buffer
        if (!buffers.contains(config.path)) {
            // So if there is new unique path met, allocate buffer
            auto memrange = std::make_shared<doca::MemoryRange>(config.size);
            auto buffer = std::make_shared<doca::rdma::RdmaBuffer>();

            // Won't return error here
            std::ignore = buffer->RegisterMemoryRange(memrange);

            buffers[config.path] = buffer;
        }

        // This will be buffer just created early or from endpoint with same path
        auto uniqueBuffer = buffers[config.path];

        auto [ep, err] = doca::rdma::RdmaEndpoint::Create()
                             .SetDevice(device)
                             .SetPath(config.path)
                             .SetType(config.type)
                             .SetBuffer(uniqueBuffer)
                             .Build();
        if (err) {
            return { {}, err };
        }

        endpoints.emplace_back(std::move(ep));
    }

    return { endpoints, nullptr };
}

}  // namespace endpoints