#include <doca-cpp/rdma/rdma_stream_chain.hpp>

#include <format>

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
namespace {
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "rdma::stream_chain",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::rdma
{

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

RdmaStreamChain::Builder RdmaStreamChain::Create()
{
    return Builder();
}

RdmaStreamChain::Builder & RdmaStreamChain::Builder::AddServer(RdmaStreamServerPtr server)
{
    this->config.cpuServers.push_back(server);
    return *this;
}

RdmaStreamChain::Builder & RdmaStreamChain::Builder::AddServer(doca::gpunetio::GpuRdmaServerPtr server)
{
    this->config.gpuServers.push_back(server);
    return *this;
}

RdmaStreamChain::Builder & RdmaStreamChain::Builder::SetAggregateService(RdmaAggregateStreamServicePtr service)
{
    this->config.cpuAggregateService = service;
    return *this;
}

RdmaStreamChain::Builder & RdmaStreamChain::Builder::SetAggregateService(doca::gpunetio::GpuRdmaAggregateStreamServicePtr service)
{
    this->config.gpuAggregateService = service;
    return *this;
}

std::tuple<RdmaStreamChainPtr, error> RdmaStreamChain::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }

    const auto totalServers = this->config.cpuServers.size() + this->config.gpuServers.size();
    if (totalServers == 0) {
        return { nullptr, errors::New("At least one server must be added to the chain") };
    }

    if (!this->config.cpuAggregateService && !this->config.gpuAggregateService) {
        return { nullptr, errors::New("Aggregate service must be set") };
    }

    auto chain = std::make_shared<RdmaStreamChain>(this->config);
    return { chain, nullptr };
}

// ─────────────────────────────────────────────────────────
// RdmaStreamChain
// ─────────────────────────────────────────────────────────

RdmaStreamChain::RdmaStreamChain(const Config & config) : config(config) {}

RdmaStreamChain::~RdmaStreamChain()
{
    this->running.store(false);

    for (auto & thread : this->serverThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

error RdmaStreamChain::Serve()
{
    if (this->running.load()) {
        return errors::New("Chain is already running");
    }

    this->running.store(true);

    DOCA_CPP_LOG_INFO(std::format("RdmaStreamChain starting with {} CPU servers and {} GPU servers", this->config.cpuServers.size(),
                                  this->config.gpuServers.size()));

    // Start all servers in separate threads
    this->startServers();

    // Wait for all server threads to complete
    for (auto & thread : this->serverThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    DOCA_CPP_LOG_INFO("RdmaStreamChain stopped");
    return nullptr;
}

error RdmaStreamChain::Shutdown(const std::chrono::milliseconds & timeout)
{
    this->running.store(false);

    // Shutdown CPU servers
    for (auto & server : this->config.cpuServers) {
        if (server) {
            std::ignore = server->Shutdown(timeout);
        }
    }

    // Shutdown GPU servers
    for (auto & server : this->config.gpuServers) {
        if (server) {
            std::ignore = server->Shutdown(timeout);
        }
    }

    return nullptr;
}

void RdmaStreamChain::startServers()
{
    // Start CPU servers
    for (auto & server : this->config.cpuServers) {
        this->serverThreads.emplace_back([server]() {
            auto err = server->Serve();
            if (err) {
                DOCA_CPP_LOG_ERROR(std::format("CPU server error: {}", err->What()));
            }
        });
    }

    // Start GPU servers
    for (auto & server : this->config.gpuServers) {
        this->serverThreads.emplace_back([server]() {
            auto err = server->Serve();
            if (err) {
                DOCA_CPP_LOG_ERROR(std::format("GPU server error: {}", err->What()));
            }
        });
    }
}

}  // namespace doca::rdma
