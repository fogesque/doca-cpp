#include "doca-cpp/rdma/rdma_executor.hpp"

#include "rdma_executor.hpp"

using doca::rdma::OperationRequest;
using doca::rdma::RdmaConnectionRole;
using doca::rdma::RdmaEnginePtr;
using doca::rdma::RdmaExecutor;
using doca::rdma::RdmaExecutorPtr;

namespace constants
{
constexpr std::size_t initialBufferInventorySize = 16;
constexpr std::string serverAddress = "192.168.88.252";
constexpr std::uint16_t serverport = 12345;
}  // namespace constants

std::tuple<RdmaExecutorPtr, error> RdmaExecutor::Create(RdmaConnectionRole initialRole, doca::DevicePtr initialDevice)
{
    if (initialDevice == nullptr) {
        return { nullptr, errors::New("Device is null") };
    }

    // Create RDMA engine
    auto [rdmaEngine, err] = RdmaEngine::Create(initialDevice)
                                 .SetTransportType(TransportType::rc)
                                 .SetGidIndex(0)
                                 .SetPermissions(doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaRead |
                                                 doca::AccessFlags::rdmaWrite)
                                 .SetMaxNumConnections(16)
                                 .Build();
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create RDMA Engine") };
    }

    auto executorConfig = Config{
        .rdmaConnectionRole = initialRole,
        .initialRdmaEngine = rdmaEngine,
        .initialDevice = initialDevice,
    };
    auto rdmaExecutor = std::make_shared<RdmaExecutor>(executorConfig);
    return { rdmaExecutor, nullptr };
}

RdmaExecutor::RdmaExecutor(const Config & initialConfig)
    : rdmaEngine(initialConfig.initialRdmaEngine), device(initialConfig.initialDevice), running(false),
      workerThread(nullptr), rdmaConnectionRole(initialConfig.rdmaConnectionRole), rdmaContext(nullptr),
      progressEngine(nullptr), bufferInventory(nullptr)
{
}

RdmaExecutor::~RdmaExecutor()
{
    std::println("[RdmaExecutor] Shutting down...");
    {
        std::scoped_lock lock(this->queueMutex);
        this->running.store(false);
    }
    this->queueCondVar.notify_one();

    if (this->workerThread->joinable()) {
        this->workerThread->join();
    }
    std::println("[RdmaExecutor] Shutdown complete");
}

error doca::rdma::RdmaExecutor::Start()
{
    if (this->running.load()) {
        return errors::New("RdmaExecutor is already running");
    }

    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is not initialized");
    }

    // Create ProgressEngine
    auto [engine, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return errors::Wrap(peErr, "failed to create RDMA progress engine");
    }
    this->progressEngine = engine;

    // Create RDMA Context
    auto [context, ctxErr] = this->rdmaEngine->AsContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "failed to get RDMA context");
    }
    this->rdmaContext = context;

    // Connect RDMA Context to ProgressEngine
    auto err = this->progressEngine->ConnectContext(this->rdmaContext);
    if (err) {
        return errors::Wrap(err, "failed to connect RDMA context to progress engine");
    }

    // Set RDMA Context user data to this RdmaExecutor
    auto userData = doca::Data(static_cast<void *>(this));
    err = this->rdmaContext->SetUserData(userData);
    if (err) {
        return errors::Wrap(err, "failed to set executor to user data of RDMA context");
    }

    // Set RDMA Context state change callback
    auto ctxCallback = [](const union doca_data userData, struct doca_ctx * ctx, enum doca_ctx_states prevState,
                          enum doca_ctx_states nextState) -> void {
        // Do nothing. Context State will be checked with Context::GetState()
    };
    err = this->rdmaContext->SetContextStateChangedCallback(ctxCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA context state change callback");
    }

    // Set RDMA Receive Task state change callbacks
    auto taskReceiveSuccessCallback = [](struct doca_rdma_task_receive * task, union doca_data taskUserData,
                                         union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    auto taskReceiveErrorCallback = [](struct doca_rdma_task_receive * task, union doca_data taskUserData,
                                       union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    err = this->rdmaEngine->SetReceiveTaskCompletionCallbacks(taskReceiveSuccessCallback, taskReceiveErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA receive task state change callback");
    }

    // Set RDMA Send Task state change callbacks
    auto taskSendSuccessCallback = [](struct doca_rdma_task_send * task, union doca_data taskUserData,
                                      union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    auto taskSendErrorCallback = [](struct doca_rdma_task_send * task, union doca_data taskUserData,
                                    union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    err = this->rdmaEngine->SetSendTaskCompletionCallbacks(taskSendSuccessCallback, taskSendErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA send task state change callback");
    }

    // Set RDMA Read Task state change callbacks
    auto taskReadSuccessCallback = [](struct doca_rdma_task_read * task, union doca_data taskUserData,
                                      union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    auto taskReadErrorCallback = [](struct doca_rdma_task_read * task, union doca_data taskUserData,
                                    union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    err = this->rdmaEngine->SetReadTaskCompletionCallbacks(taskReadSuccessCallback, taskReadErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA read task state change callback");
    }

    // Set RDMA Write Task state change callbacks
    auto taskWriteSuccessCallback = [](struct doca_rdma_task_write * task, union doca_data taskUserData,
                                       union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    auto taskWriteErrorCallback = [](struct doca_rdma_task_write * task, union doca_data taskUserData,
                                     union doca_data ctxUserData) -> void {
        // TODO: implement task callback
    };
    err = this->rdmaEngine->SetWriteTaskCompletionCallbacks(taskWriteSuccessCallback, taskWriteErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA write task state change callback");
    }

    // Set Connection callbacks
    auto requestCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data ctxUserData) {
        // TODO: implement callback
    };
    auto establishedCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                  union doca_data ctxUserData) {
        // TODO: implement callback
    };
    auto failureCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                              union doca_data ctxUserData) {
        // TODO: implement callback
    };
    auto disconnectCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                 union doca_data ctxUserData) {
        // TODO: implement callback
    };
    auto connectionCallbacks = RdmaEngine::ConnectionCallbacks{ .requestCallback = requestCallback,
                                                                .establishedCallback = establishedCallback,
                                                                .failureCallback = failureCallback,
                                                                .disconnectCallback = disconnectCallback };
    err = this->rdmaEngine->SetConnectionStateChangedCallbacks(connectionCallbacks);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA connection state change callback");
    }

    // Create BufferInventory
    auto [inventory, invErr] = doca::BufferInventory::Create(constants::initialBufferInventorySize).Start();
    if (err) {
        return errors::Wrap(err, "failed to create and start buffer inventory");
    }
    this->bufferInventory = inventory;

    // ----------------------------------------------------------------------------

    // Start RDMA Context
    err = this->rdmaContext->Start();
    if (err) {
        return errors::Wrap(err, "failed to start RDMA context");
    }

    // Wait for Context::State is Running
    const auto desiredState = Context::State::running;
    err = this->waitForContextState(desiredState);
    if (err) {
        if (errors::Is(err, ErrorType::TimeoutExpired)) {
            return errors::Wrap(err, "failed to wait for desired context state due to timeout");
        }
        return errors::Wrap(err, "failed to wait for desired context state");
    }

    // Start worker thread
    this->running.store(true);
    this->workerThread = std::make_unique<std::thread>([this] { this->workerLoop(); });
}

error RdmaExecutor::SubmitOperation(OperationRequest request)
{
    {
        std::scoped_lock lock(this->queueMutex);
        if (!this->running) {
            auto err = errors::New("Executor is shut down");
            request.promise->set_value(err);
            return err;
        }
        if (this->operationQueue.size() >= this->TasksQueueSizeThreshold) {
            return errors::New("Operations queue reached its size limit");
        }
        this->operationQueue.push(std::move(request));
    }
    this->queueCondVar.notify_one();
    return nullptr;
}

const RdmaExecutor::Statistics & RdmaExecutor::GetStatistics() const
{
    return this->stats;
}

void RdmaExecutor::workerLoop()
{
    while (true) {
        OperationRequest request;
        {
            std::unique_lock lock(queueMutex);
            this->queueCondVar.wait(lock, [this] { return !this->running || !this->operationQueue.empty(); });

            if (!this->running && this->operationQueue.empty()) {
                return;
            }

            request = std::move(this->operationQueue.front());
            this->operationQueue.pop();
        }
        auto err = this->executeOperation(request);
        request.promise->set_value(std::move(err));
    }
}

error RdmaExecutor::executeOperation(const OperationRequest & request)
{
    switch (request.type) {
        case OperationRequest::Type::Send:
            return this->executeSend(request);
        case OperationRequest::Type::Receive:
            return this->executeReceive(request);
        case OperationRequest::Type::Read:
            return this->executeRead(request);
        case OperationRequest::Type::Write:
            return this->executeWrite(request);
    }
    return errors::New("Unknown operation type");
}

error RdmaExecutor::executeSend(const OperationRequest & request)
{
    // TODO: implement execute

    this->stats.sendOperations++;
    return nullptr;
}

error RdmaExecutor::executeReceive(const OperationRequest & request)
{
    // TODO: implement execute

    this->stats.receiveOperations++;
    return nullptr;
}

error RdmaExecutor::executeRead(const OperationRequest & request)
{
    // TODO: implement execute

    this->stats.readOperations++;
    return nullptr;
}

error RdmaExecutor::executeWrite(const OperationRequest & request)
{
    // TODO: implement execute

    this->stats.writeOperations++;
    return nullptr;
}

error RdmaExecutor::waitForContextState(doca::Context::State desiredState, std::chrono::milliseconds waitTimeout) const
{
    if (this->rdmaContext == nullptr) {
        return errors::New("Context is null");
    }

    auto [initialState, stateErr] = this->rdmaContext->GetState();
    if (stateErr) {
        return errors::Wrap(stateErr, "failed to get context state");
    }

    const auto startTime = std::chrono::steady_clock::now();
    auto currState = initialState;
    while (currState != desiredState) {
        if (this->timeoutExpired(startTime, waitTimeout)) {
            return ErrorType::TimeoutExpired;
        }
        std::this_thread::sleep_for(10us);
        auto [newState, err] = this->rdmaContext->GetState();
        if (err) {
            return errors::Wrap(err, "failed to get context state");
        }
        currState = newState;
    }

    return nullptr;
}

error doca::rdma::RdmaExecutor::setupConnection(RdmaConnectionRole role)
{
    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is not initialized");
    }

    // If role is Client, create Server address and try to connect
    if (role == RdmaConnectionRole::client) {
        auto connectionId = 42;

        auto [address, err] =
            RdmaAddress::Create(RdmaAddress::Type::ipv4, constants::serverAddress, constants::serverport);
        if (err) {
            return errors::Wrap(err, "failed to create server RDMA address");
        }

        // Set connection user data to connectionId
        auto connectionUserData = doca::Data(connectionId);
        err = this->rdmaEngine->ConnectToAddress(address, connectionUserData);
        if (err) {
            return errors::Wrap(err, "failed to connect to server RDMA address");
        }
    }

    // If role is Server, start listen to port
    if (role == RdmaConnectionRole::client) {
    }
}

bool RdmaExecutor::timeoutExpired(const std::chrono::steady_clock::time_point & startTime,
                                  std::chrono::milliseconds timeout) const
{
    if (timeout == std::chrono::milliseconds::zero()) {
        return false;
    }
    const auto currentTime = std::chrono::steady_clock::now();
    return (currentTime - startTime) > timeout;
}