#include "doca-cpp/rdma/internal/rdma_executor.hpp"

#include "doca-cpp/logging/logging.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "executor",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)

#endif

using doca::rdma::RdmaAwaitable;
using doca::rdma::RdmaConnection;
using doca::rdma::RdmaConnectionPtr;
using doca::rdma::RdmaConnectionRole;
using doca::rdma::RdmaEnginePtr;
using doca::rdma::RdmaExecutor;
using doca::rdma::RdmaExecutorPtr;
using doca::rdma::RdmaOperationRequest;
using doca::rdma::RdmaOperationResponce;

namespace constants
{
constexpr std::size_t initialBufferInventorySize = 16;
}  // namespace constants

std::tuple<RdmaExecutorPtr, error> RdmaExecutor::Create(doca::DevicePtr initialDevice)
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
        return { nullptr, errors::Wrap(err, "Failed to create RDMA Engine") };
    }

    DOCA_CPP_LOG_DEBUG("Created RDMA engine");

    auto executorConfig = Config{
        .initialRdmaEngine = rdmaEngine,
        .initialDevice = initialDevice,
    };
    auto rdmaExecutor = std::make_shared<RdmaExecutor>(executorConfig);
    return { rdmaExecutor, nullptr };
}

RdmaExecutor::RdmaExecutor(const Config & initialConfig)
    : rdmaEngine(initialConfig.initialRdmaEngine), device(initialConfig.initialDevice), workerRunning(false),
      workerThread(nullptr), rdmaContext(nullptr), progressEngine(nullptr), bufferInventory(nullptr)
{
}

RdmaExecutor::~RdmaExecutor()
{
    DOCA_CPP_LOG_DEBUG("Executor destructor called, joining all running threads");
    {
        std::scoped_lock lock(this->queueMutex);
        this->workerRunning.store(false);
    }
    this->queueCondVar.notify_one();

    if (this->workerThread->joinable()) {
        this->workerThread->join();
    }
    DOCA_CPP_LOG_DEBUG("Executor destroyed successfully");
}

error RdmaExecutor::Start()
{
    DOCA_CPP_LOG_DEBUG("Starting RDMA executor...");

    if (this->workerRunning.load()) {
        return errors::New("Executor is already running");
    }

    if (this->rdmaEngine == nullptr) {
        return errors::New("RDMA engine is not initialized");
    }

    // Create ProgressEngine
    auto [engine, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return errors::Wrap(peErr, "Failed to create RDMA progress engine");
    }
    this->progressEngine = engine;

    DOCA_CPP_LOG_DEBUG("Created progress engine");

    // Create RDMA Context
    auto [context, ctxErr] = this->rdmaEngine->AsContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "Failed to get RDMA context");
    }
    this->rdmaContext = context;

    // Connect RDMA Context to ProgressEngine
    auto err = this->progressEngine->ConnectContext(this->rdmaContext);
    if (err) {
        return errors::Wrap(err, "Failed to connect RDMA context to progress engine");
    }

    DOCA_CPP_LOG_DEBUG("Connected RDMA context to progress engine");

    // Set RDMA Context user data to this RdmaExecutor
    auto userData = doca::Data(static_cast<void *>(this));
    err = this->rdmaContext->SetUserData(userData);
    if (err) {
        return errors::Wrap(err, "Failed to set executor to user data of RDMA context");
    }

    DOCA_CPP_LOG_DEBUG("Set RDMA executor object pointer to RDMA context user data");

    // Set RDMA Context state change callback
    auto ctxCallback = [](const union doca_data userData, struct doca_ctx * ctx, enum doca_ctx_states prevState,
                          enum doca_ctx_states nextState) -> void {
        DOCA_CPP_LOG_DEBUG("Callback: context state changed");
        // Do nothing. Context State will be checked with Context::GetState()
    };
    err = this->rdmaContext->SetContextStateChangedCallback(ctxCallback);
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA context state change callback");
    }

    DOCA_CPP_LOG_DEBUG("Set RDMA context state change callback");

    // Set RDMA Receive Task state change callbacks
    auto taskReceiveSuccessCallback = [](struct doca_rdma_task_receive * task, union doca_data taskUserData,
                                         union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::completed;
        DOCA_CPP_LOG_DEBUG("Callback: receive task completed successfully");
    };
    auto taskReceiveErrorCallback = [](struct doca_rdma_task_receive * task, union doca_data taskUserData,
                                       union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::error;
        DOCA_CPP_LOG_DEBUG("Callback: receive task completed with error");
    };
    err = this->rdmaEngine->SetReceiveTaskCompletionCallbacks(taskReceiveSuccessCallback, taskReceiveErrorCallback);
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA receive task state change callback");
    }

    DOCA_CPP_LOG_DEBUG("Set RDMA receive task completion callbacks");

    // Set RDMA Send Task state change callbacks
    auto taskSendSuccessCallback = [](struct doca_rdma_task_send * task, union doca_data taskUserData,
                                      union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::completed;
        DOCA_CPP_LOG_DEBUG("Callback: send task completed successfully");
    };
    auto taskSendErrorCallback = [](struct doca_rdma_task_send * task, union doca_data taskUserData,
                                    union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::error;
        DOCA_CPP_LOG_DEBUG("Callback: send task completed with error");
    };
    err = this->rdmaEngine->SetSendTaskCompletionCallbacks(taskSendSuccessCallback, taskSendErrorCallback);
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA send task state change callback");
    }

    DOCA_CPP_LOG_DEBUG("Set RDMA send task completion callbacks");

    // Set RDMA Read Task state change callbacks
    auto taskReadSuccessCallback = [](struct doca_rdma_task_read * task, union doca_data taskUserData,
                                      union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::completed;
        DOCA_CPP_LOG_DEBUG("Callback: read task completed successfully");
    };
    auto taskReadErrorCallback = [](struct doca_rdma_task_read * task, union doca_data taskUserData,
                                    union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::error;
        DOCA_CPP_LOG_DEBUG("Callback: read task completed with error");
    };
    err = this->rdmaEngine->SetReadTaskCompletionCallbacks(taskReadSuccessCallback, taskReadErrorCallback);
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA read task state change callback");
    }

    DOCA_CPP_LOG_DEBUG("Set RDMA read task completion callbacks");

    // Set RDMA Write Task state change callbacks
    auto taskWriteSuccessCallback = [](struct doca_rdma_task_write * task, union doca_data taskUserData,
                                       union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::completed;
        DOCA_CPP_LOG_DEBUG("Callback: write task completed successfully");
    };
    auto taskWriteErrorCallback = [](struct doca_rdma_task_write * task, union doca_data taskUserData,
                                     union doca_data ctxUserData) -> void {
        auto transferState = static_cast<IRdmaTask::State *>(taskUserData.ptr);
        *transferState = IRdmaTask::State::error;
        DOCA_CPP_LOG_DEBUG("Callback: write task completed with error");
    };
    err = this->rdmaEngine->SetWriteTaskCompletionCallbacks(taskWriteSuccessCallback, taskWriteErrorCallback);
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA write task state change callback");
    }

    DOCA_CPP_LOG_DEBUG("Set RDMA write task completion callbacks");

    // Set Connection callbacks
    auto requestCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        executor->OnConnectionRequested(connection);
        DOCA_CPP_LOG_DEBUG(std::format("Callback: connection (ID: {}) state is requested", connId));
    };
    auto establishedCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                  union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        executor->OnConnectionEstablished(connection);
        DOCA_CPP_LOG_DEBUG(std::format("Callback: connection (ID: {}) state is established", connId));
    };
    auto failureCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                              union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        executor->OnConnectionClosed(connId);
        DOCA_CPP_LOG_DEBUG(std::format("Callback: connection (ID: {}) state is failed", connId));
    };
    auto disconnectCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                 union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        executor->OnConnectionClosed(connId);
        DOCA_CPP_LOG_DEBUG(std::format("Callback: connection (ID: {}) state is disconnected", connId));
    };
    auto connectionCallbacks = RdmaEngine::ConnectionCallbacks{ .requestCallback = requestCallback,
                                                                .establishedCallback = establishedCallback,
                                                                .failureCallback = failureCallback,
                                                                .disconnectCallback = disconnectCallback };
    err = this->rdmaEngine->SetConnectionStateChangedCallbacks(connectionCallbacks);
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA connection state change callback");
    }

    DOCA_CPP_LOG_DEBUG("Set RDMA connection state change callbacks");

    // Create BufferInventory
    auto [inventory, invErr] = doca::BufferInventory::Create(constants::initialBufferInventorySize).Start();
    if (err) {
        return errors::Wrap(err, "Failed to create and start buffer inventory");
    }
    this->bufferInventory = inventory;

    DOCA_CPP_LOG_DEBUG("Created buffer inventory");

    // ----------------------------------------------------------------------------

    // Start RDMA Context
    err = this->rdmaContext->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA context");
    }

    DOCA_CPP_LOG_DEBUG("Started RDMA context");

    // Wait for Context::State is Running
    const auto desiredState = Context::State::running;
    err = this->waitForContextState(desiredState);
    if (err) {
        if (errors::Is(err, ErrorTypes::TimeoutExpired)) {
            return errors::Wrap(err, "Failed to wait for desired context state due to timeout");
        }
        return errors::Wrap(err, "Failed to wait for desired context state");
    }

    DOCA_CPP_LOG_DEBUG("RDMA context state is running");

    // Start worker thread
    this->workerRunning.store(true);
    this->workerThread = std::make_unique<std::thread>([this] { this->workerLoop(); });

    DOCA_CPP_LOG_DEBUG("Started executor working thread");

    return nullptr;
}

void RdmaExecutor::Stop()
{
    DOCA_CPP_LOG_DEBUG("Stopping executor...");

    if (!this->workerRunning.load()) {
        return;
    }

    // Stop worker loop
    {
        std::scoped_lock lock(this->queueMutex);
        this->workerRunning.store(false);
    }
    this->queueCondVar.notify_one();

    DOCA_CPP_LOG_DEBUG("Stopped executor's working thread");

    // Wait for worker thread to finish
    if (this->workerThread->joinable()) {
        this->workerThread->join();
    }

    // Clear operation queue
    {
        std::scoped_lock lock(this->queueMutex);
        while (!this->operationQueue.empty()) {
            this->operationQueue.pop();
        }
    }

    DOCA_CPP_LOG_DEBUG("Joined executor's thread and flushed its operations queue");
}

error RdmaExecutor::ConnectToAddress(const std::string & serverAddress, uint16_t serverPort)
{
    if (this->rdmaEngine == nullptr) {
        return errors::New("RDMA engine is not initialized");
    }

    // Create server address object
    auto [address, err] = RdmaAddress::Create(RdmaAddress::Type::ipv4, serverAddress, serverPort);
    if (err) {
        return errors::Wrap(err, "Failed to create server RDMA address");
    }

    DOCA_CPP_LOG_DEBUG("Created RDMA address");

    // Connect to server address
    auto connectionUserData = doca::Data();
    err = this->rdmaEngine->ConnectToAddress(address, connectionUserData);
    if (err) {
        return errors::Wrap(err, "Failed to connect to server RDMA address");
    }

    DOCA_CPP_LOG_DEBUG("Tried to connect to RDMA address");

    DOCA_CPP_LOG_DEBUG("Waiting for connection to get to established state...");

    // Wait for connection to be established
    const auto startTime = std::chrono::steady_clock::now();
    const auto waitTimeout = 5s;
    while (this->activeConnection == nullptr) {
        if (this->timeoutExpired(startTime, waitTimeout)) {
            return ErrorTypes::TimeoutExpired;
        }
        std::this_thread::sleep_for(10us);
        std::ignore = this->progressEngine->Progress();
    }

    DOCA_CPP_LOG_DEBUG("Connection was established");

    return nullptr;
}

error RdmaExecutor::ListenToPort(uint16_t port)
{
    if (this->rdmaEngine == nullptr) {
        return errors::New("RDMA engine is not initialized");
    }

    // Start listening to server port
    auto err = this->rdmaEngine->ListenToPort(port);
    if (err) {
        return errors::Wrap(err, "Failed to start listening on server port");
    }

    DOCA_CPP_LOG_DEBUG("Started to listen to port");

    return nullptr;
}

void RdmaExecutor::OnConnectionRequested(RdmaConnectionPtr connection)
{
    // Reject if already established
    if (this->activeConnection != nullptr) {
        std::ignore = connection->Reject();
        return;
    }

    // Reject if already requested
    if (this->requestedConnection != nullptr) {
        std::ignore = connection->Reject();
        return;
    }
    this->requestedConnection = connection;

    std::ignore = this->requestedConnection->Accept();

    DOCA_CPP_LOG_DEBUG(std::format("Add requested connection to executor"));
}

void RdmaExecutor::OnConnectionEstablished(RdmaConnectionPtr connection)
{
    // Disconnect if already established
    if (this->activeConnection != nullptr) {
        std::ignore = connection->Disconnect();
        return;
    }

    this->activeConnection = connection;
    this->requestedConnection = nullptr;

    DOCA_CPP_LOG_DEBUG(std::format("Assigned requested connection to active connection"));
}

void RdmaExecutor::OnConnectionClosed(RdmaConnectionId connectionId)
{
    this->activeConnection = nullptr;
    DOCA_CPP_LOG_DEBUG(std::format("Removed active connection from executor"));
}

std::tuple<RdmaConnectionPtr, error> RdmaExecutor::GetActiveConnection()
{
    if (this->activeConnection == nullptr) {
        return { nullptr, errors::New("No active RDMA connection") };
    }
    return { this->activeConnection, nullptr };
}

void doca::rdma::RdmaExecutor::Progress()
{
    this->progressEngine->Progress();
}

doca::DevicePtr doca::rdma::RdmaExecutor::GetDevice()
{
    return this->device;
}

std::tuple<RdmaAwaitable, error> RdmaExecutor::SubmitOperation(RdmaOperationRequest request)
{
    auto operationFuture = request.responcePromise->get_future();
    auto awaitable = RdmaAwaitable(operationFuture);
    {
        std::scoped_lock lock(this->queueMutex);
        if (!this->workerRunning) {
            auto err = errors::New("Executor is shut down");
            request.responcePromise->set_value({ nullptr, err });
            return { std::move(awaitable), err };
        }
        this->operationQueue.push(std::move(request));
        DOCA_CPP_LOG_DEBUG("Pushed RDMA operation to executor operations queue");
    }
    this->queueCondVar.notify_one();
    return { std::move(awaitable), nullptr };
}

void RdmaExecutor::workerLoop()
{
    while (true) {
        // Get operation request from operations queue
        RdmaOperationRequest request;
        {
            std::unique_lock lock(this->queueMutex);
            this->queueCondVar.wait(lock, [this] { return !this->workerRunning || !this->operationQueue.empty(); });

            if (!this->workerRunning && this->operationQueue.empty()) {
                DOCA_CPP_LOG_DEBUG("Exiting worker thread");
                return;
            }

            request = std::move(this->operationQueue.front());
            this->operationQueue.pop();
            DOCA_CPP_LOG_DEBUG("Worker thread took operation from queue");
        }
        // Execute operation from queue
        auto responce = this->executeOperation(request);

        // Set operation promise responce value
        request.responcePromise->set_value(responce);

        // Connection promise is handled separately for different operation type, will be set if no error occurs

        DOCA_CPP_LOG_DEBUG("Worker thread executed RDMA operation");
    }
}

RdmaOperationResponce RdmaExecutor::executeOperation(RdmaOperationRequest & request)
{
    switch (request.type) {
        case RdmaOperationType::read:
            return this->executeRead(request);
        case RdmaOperationType::write:
            return this->executeWrite(request);
    }
    return { nullptr, errors::New("Unknown operation type") };
}

RdmaOperationResponce RdmaExecutor::executeRead(RdmaOperationRequest & request)
{
    // Check requested buffers
    if (!request.localBuffer || !request.remoteBuffer) {
        return { nullptr, errors::New("Invalid request; provide both local and remote RDMA buffers") };
    }

    // Check that connection is active
    if (this->activeConnection == nullptr) {
        return { nullptr, errors::New("No active RDMA connection available for read operation") };
    }

    // Initialize operation state
    auto taskState = IRdmaTask::State::idle;

    // Get DOCA buffer for source RDMA buffer
    auto [srcBuf, srcBufErr] = this->getSourceRemoteBuffer(request.remoteBuffer);
    if (srcBufErr) {
        return { nullptr, errors::Wrap(srcBufErr, "Failed to get doca buffer") };
    }

    // Get DOCA buffer for destination RDMA buffer
    auto [dstBuf, dstBufErr] = this->getDestinationLocalBuffer(request.localBuffer);
    if (dstBufErr) {
        return { nullptr, errors::Wrap(dstBufErr, "Failed to get doca buffer") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread got plain doca source and destination buffers");

    // Create RdmaSendTask from RdmaEngine
    // Set task user data to current transfer state: it will be changed in the task callbacks
    auto taskUserData = doca::Data(static_cast<void *>(&taskState));
    auto [readTask, err] = this->rdmaEngine->AllocateReadTask(this->activeConnection, srcBuf, dstBuf, taskUserData);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to allocate RDMA read task") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread allocated read task");

    // Submit RdmaSendTask to RdmaEngine
    taskState = IRdmaTask::State::submitted;
    err = readTask->Submit();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to submit RDMA read task") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread submitted read task");

    DOCA_CPP_LOG_DEBUG("Worker thread is waiting for task to complete...");

    // Wait for task completion
    // const auto waitTimeout = 5000ms;
    err = this->waitForTaskState(IRdmaTask::State::completed, taskState /*,  waitTimeout */);
    if (err) {
        if (errors::Is(err, ErrorTypes::TimeoutExpired)) {
            return { nullptr, errors::Wrap(err, "Failed to wait for RDMA read task completion due to timeout") };
        }
        return { nullptr, errors::Wrap(err, "Failed to wait for RDMA read task completion") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread completed read task");

    // Free RdmaSendTask
    readTask->Free();

    // Decrement buffers references count in BufferInventory
    auto [_, srcRcErr] = srcBuf->DecRefcount();
    if (srcRcErr) {
        return { nullptr, errors::Wrap(srcRcErr, "Failed to decrement buffer reference count in buffer inventory") };
    }
    auto [__, dstRcErr] = dstBuf->DecRefcount();
    if (dstRcErr) {
        return { nullptr, errors::Wrap(dstRcErr, "Failed to decrement buffer reference count in buffer inventory") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread decreased plain doca source and destination buffers reference counts");

    return { request.localBuffer, nullptr };
}

RdmaOperationResponce RdmaExecutor::executeWrite(RdmaOperationRequest & request)
{
    // Check requested buffers
    if (!request.localBuffer || !request.remoteBuffer) {
        return { nullptr, errors::New("Invalid request; provide both local and remote RDMA buffers") };
    }

    // Check that connection is active
    if (this->activeConnection == nullptr) {
        return { nullptr, errors::New("No active RDMA connection available for write operation") };
    }

    // Initialize operation state
    auto taskState = IRdmaTask::State::idle;

    // Get DOCA buffer for source RDMA buffer
    auto [srcBuf, srcBufErr] = this->getSourceLocalBuffer(request.localBuffer);
    if (srcBufErr) {
        return { nullptr, errors::Wrap(srcBufErr, "Failed to get doca buffer") };
    }

    // Get DOCA buffer for destination RDMA buffer
    auto [dstBuf, dstBufErr] = this->getDestinationRemoteBuffer(request.remoteBuffer);
    if (dstBufErr) {
        return { nullptr, errors::Wrap(dstBufErr, "Failed to get doca buffer") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread got plain doca source and destination buffers");

    // Create task from RdmaEngine
    // Set task user data to current transfer state: it will be changed in the task callbacks
    auto taskUserData = doca::Data(static_cast<void *>(&taskState));
    auto [writeTask, err] = this->rdmaEngine->AllocateWriteTask(this->activeConnection, srcBuf, dstBuf, taskUserData);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to allocate RDMA write task") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread allocated write task");

    // Submit task to RdmaEngine
    taskState = IRdmaTask::State::submitted;
    err = writeTask->Submit();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to submit RDMA write task") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread submitted write task");

    DOCA_CPP_LOG_DEBUG("Worker thread is waiting for task to complete...");

    // Wait for task completion
    // const auto waitTimeout = 5000ms;
    err = this->waitForTaskState(IRdmaTask::State::completed, taskState /*,  waitTimeout */);
    if (err) {
        if (errors::Is(err, ErrorTypes::TimeoutExpired)) {
            return { nullptr, errors::Wrap(err, "Failed to wait for RDMA write task completion due to timeout") };
        }
        return { nullptr, errors::Wrap(err, "Failed to wait for RDMA write task completion") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread completed write task");

    // Free RdmaSendTask
    writeTask->Free();

    // Decrement buffers references count in BufferInventory
    auto [_, srcRcErr] = srcBuf->DecRefcount();
    if (srcRcErr) {
        return { nullptr, errors::Wrap(srcRcErr, "Failed to decrement buffer reference count in buffer inventory") };
    }
    auto [__, dstRcErr] = dstBuf->DecRefcount();
    if (dstRcErr) {
        return { nullptr, errors::Wrap(dstRcErr, "Failed to decrement buffer reference count in buffer inventory") };
    }

    DOCA_CPP_LOG_DEBUG("Worker thread decreased plain doca source and destination buffers reference counts");

    return { request.localBuffer, nullptr };
}

error RdmaExecutor::waitForContextState(doca::Context::State desiredState, std::chrono::milliseconds waitTimeout) const
{
    if (this->rdmaContext == nullptr) {
        return errors::New("Context is null");
    }

    auto [initialState, stateErr] = this->rdmaContext->GetState();
    if (stateErr) {
        return errors::Wrap(stateErr, "Failed to get context state");
    }

    const auto startTime = std::chrono::steady_clock::now();
    auto currState = initialState;
    while (currState != desiredState) {
        if (this->timeoutExpired(startTime, waitTimeout)) {
            return ErrorTypes::TimeoutExpired;
        }
        std::this_thread::sleep_for(10us);
        auto [newState, err] = this->rdmaContext->GetState();
        if (err) {
            return errors::Wrap(err, "Failed to get context state");
        }
        currState = newState;
    }

    return nullptr;
}

error RdmaExecutor::waitForTaskState(IRdmaTask::State desiredState, IRdmaTask::State & changingState,
                                     std::chrono::milliseconds waitTimeout)
{
    if (this->progressEngine == nullptr) {
        return errors::New("Progress engine is null");
    }

    const auto startTime = std::chrono::steady_clock::now();
    while (changingState != desiredState) {
        if (this->timeoutExpired(startTime, waitTimeout)) {
            return ErrorTypes::TimeoutExpired;
        }
        std::this_thread::sleep_for(10us);
        this->progressEngine->Progress();

        if (changingState == IRdmaTask::State::error) {
            return errors::New("Task completed with error");
        }
    }

    return nullptr;
}

error RdmaExecutor::waitForConnectionState(RdmaConnection::State desiredState, RdmaConnection::State & changingState,
                                           std::chrono::milliseconds waitTimeout)
{
    if (this->progressEngine == nullptr) {
        return errors::New("Progress engine is null");
    }

    const auto startTime = std::chrono::steady_clock::now();
    while (changingState != desiredState) {
        if (this->timeoutExpired(startTime, waitTimeout)) {
            return ErrorTypes::TimeoutExpired;
        }
        std::this_thread::sleep_for(10us);
        this->progressEngine->Progress();
    }

    return nullptr;
}

std::tuple<doca::BufferPtr, error> RdmaExecutor::getSourceLocalBuffer(RdmaBufferPtr rdmaBuffer)
{
    if (rdmaBuffer == nullptr) {
        return { nullptr, errors::New("RDMA buffer is null") };
    }

    // Get buffer memory range
    auto [memoryRange, err] = rdmaBuffer->GetMemoryRange();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to get buffer memory range") };
    }

    // Get MemoryMap from buffer
    auto [memoryMap, mapErr] = rdmaBuffer->GetMemoryMap();
    if (mapErr) {
        return { nullptr, errors::Wrap(mapErr, "Failed to get memory map from buffer") };
    }

    // Get doca::Buffer from BufferInventory
    auto [buffer, bufErr] = this->bufferInventory->AllocBufferByData(
        memoryMap, static_cast<void *>(memoryRange->data()), memoryRange->size());
    if (bufErr) {
        return { nullptr, errors::Wrap(bufErr, "Failed to allocate buffer from buffer inventory") };
    }

    return { buffer, nullptr };
}

std::tuple<doca::BufferPtr, error> RdmaExecutor::getDestinationLocalBuffer(RdmaBufferPtr rdmaBuffer)
{
    if (rdmaBuffer == nullptr) {
        return { nullptr, errors::New("RDMA buffer is null") };
    }

    // Get buffer memory range
    auto [memoryRange, err] = rdmaBuffer->GetMemoryRange();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to get buffer memory range") };
    }

    // Get MemoryMap from buffer
    auto [memoryMap, mapErr] = rdmaBuffer->GetMemoryMap();
    if (mapErr) {
        return { nullptr, errors::Wrap(mapErr, "Failed to get memory map from buffer") };
    }

    // Get doca::Buffer from BufferInventory
    auto [buffer, bufErr] = this->bufferInventory->AllocBufferByAddress(
        memoryMap, static_cast<void *>(memoryRange->data()), memoryRange->size());
    if (bufErr) {
        return { nullptr, errors::Wrap(bufErr, "Failed to allocate buffer from buffer inventory") };
    }

    return { buffer, nullptr };
}

std::tuple<doca::BufferPtr, error> RdmaExecutor::getSourceRemoteBuffer(RdmaRemoteBufferPtr rdmaBuffer)
{
    if (rdmaBuffer == nullptr) {
        return { nullptr, errors::New("Remote RDMA buffer is null") };
    }

    // Get buffer memory range
    auto [memoryRange, err] = rdmaBuffer->GetMemoryRange();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to get buffer memory range") };
    }

    // Get MemoryMap from buffer
    auto [memoryMap, mapErr] = rdmaBuffer->GetMemoryMap();
    if (mapErr) {
        return { nullptr, errors::Wrap(mapErr, "Failed to get memory map from buffer") };
    }

    // Get doca::Buffer from BufferInventory
    auto [buffer, bufErr] = this->bufferInventory->AllocBufferByData(
        memoryMap, static_cast<void *>(memoryRange->memoryAddress), memoryRange->memorySize);
    if (bufErr) {
        return { nullptr, errors::Wrap(bufErr, "Failed to allocate buffer from buffer inventory") };
    }

    return { buffer, nullptr };
}

std::tuple<doca::BufferPtr, error> RdmaExecutor::getDestinationRemoteBuffer(RdmaRemoteBufferPtr rdmaBuffer)
{
    if (rdmaBuffer == nullptr) {
        return { nullptr, errors::New("Remote RDMA buffer is null") };
    }

    // Get buffer memory range
    auto [memoryRange, err] = rdmaBuffer->GetMemoryRange();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to get buffer memory range") };
    }

    // Get MemoryMap from buffer
    auto [memoryMap, mapErr] = rdmaBuffer->GetMemoryMap();
    if (mapErr) {
        return { nullptr, errors::Wrap(mapErr, "Failed to get memory map from buffer") };
    }

    // Get doca::Buffer from BufferInventory
    auto [buffer, bufErr] = this->bufferInventory->AllocBufferByAddress(
        memoryMap, static_cast<void *>(memoryRange->memoryAddress), memoryRange->memorySize);
    if (bufErr) {
        return { nullptr, errors::Wrap(bufErr, "Failed to allocate buffer from buffer inventory") };
    }

    return { buffer, nullptr };
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