#include "doca-cpp/rdma/rdma_executor.hpp"

using doca::rdma::OperationRequest;
using doca::rdma::OperationResponce;
using doca::rdma::RdmaAwaitable;
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
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::completed;
    };
    auto taskReceiveErrorCallback = [](struct doca_rdma_task_receive * task, union doca_data taskUserData,
                                       union doca_data ctxUserData) -> void {
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::error;
    };
    err = this->rdmaEngine->SetReceiveTaskCompletionCallbacks(taskReceiveSuccessCallback, taskReceiveErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA receive task state change callback");
    }

    // Set RDMA Send Task state change callbacks
    auto taskSendSuccessCallback = [](struct doca_rdma_task_send * task, union doca_data taskUserData,
                                      union doca_data ctxUserData) -> void {
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::completed;
    };
    auto taskSendErrorCallback = [](struct doca_rdma_task_send * task, union doca_data taskUserData,
                                    union doca_data ctxUserData) -> void {
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::error;
    };
    err = this->rdmaEngine->SetSendTaskCompletionCallbacks(taskSendSuccessCallback, taskSendErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA send task state change callback");
    }

    // Set RDMA Read Task state change callbacks
    auto taskReadSuccessCallback = [](struct doca_rdma_task_read * task, union doca_data taskUserData,
                                      union doca_data ctxUserData) -> void {
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::completed;
    };
    auto taskReadErrorCallback = [](struct doca_rdma_task_read * task, union doca_data taskUserData,
                                    union doca_data ctxUserData) -> void {
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::error;
    };
    err = this->rdmaEngine->SetReadTaskCompletionCallbacks(taskReadSuccessCallback, taskReadErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA read task state change callback");
    }

    // Set RDMA Write Task state change callbacks
    auto taskWriteSuccessCallback = [](struct doca_rdma_task_write * task, union doca_data taskUserData,
                                       union doca_data ctxUserData) -> void {
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::completed;
    };
    auto taskWriteErrorCallback = [](struct doca_rdma_task_write * task, union doca_data taskUserData,
                                     union doca_data ctxUserData) -> void {
        auto transferState = static_cast<RdmaTaskInterface::State *>(taskUserData.ptr);
        *transferState = RdmaTaskInterface::State::error;
    };
    err = this->rdmaEngine->SetWriteTaskCompletionCallbacks(taskWriteSuccessCallback, taskWriteErrorCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA write task state change callback");
    }

    // Set Connection callbacks
    auto requestCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        connection->SetState(RdmaConnection::State::requested);
        executor->AddRequestedConnection(connection);
    };
    auto establishedCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                  union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        connection->SetState(RdmaConnection::State::established);
        executor->AddActiveConnection(connection);
    };
    auto failureCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                              union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        connection->SetState(RdmaConnection::State::failed);
        executor->RemoveActiveConnection(connId);
    };
    auto disconnectCallback = [](struct doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                 union doca_data ctxUserData) {
        auto executor = static_cast<RdmaExecutor *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        auto [connId, err] = connection->GetId();
        std::ignore = err;
        connection->SetState(RdmaConnection::State::disconnected);
        executor->RemoveActiveConnection(connId);
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

std::tuple<RdmaAwaitable, error> RdmaExecutor::SubmitOperation(OperationRequest request)
{
    auto operationFuture = request.promise->get_future();
    auto awaitable = RdmaAwaitable(operationFuture);
    {
        std::scoped_lock lock(this->queueMutex);
        if (!this->running) {
            auto err = errors::New("Executor is shut down");
            request.promise->set_value({ nullptr, err });
            return { std::move(awaitable), err };
        }
        if (this->operationQueue.size() >= this->TasksQueueSizeThreshold) {
            auto err = errors::New("Operations queue reached its size limit");
            request.promise->set_value({ nullptr, err });
            return { std::move(awaitable), err };
        }
        this->operationQueue.push(std::move(request));
    }
    this->queueCondVar.notify_one();
    return { std::move(awaitable), nullptr };
}

void RdmaExecutor::AddRequestedConnection(RdmaConnectionPtr connection)
{
    const auto & [id, err] = connection->GetId();
    // TODO: How to handle error?
    std::ignore = err;

    if (this->requestedConnections.contains(id)) {
        std::ignore = connection->Reject();
        return;
    }
    this->requestedConnections[id] = connection;
}

void RdmaExecutor::AddActiveConnection(RdmaConnectionPtr connection)
{
    const auto & [id, err] = connection->GetId();
    // TODO: How to handle error?
    std::ignore = err;

    this->activeConnections[id] = connection;
    this->requestedConnections.erase(id);
}

void RdmaExecutor::RemoveActiveConnection(RdmaConnectionId connectionId)
{
    this->activeConnections.erase(connectionId);
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
        auto responce = this->executeOperation(request);
        request.promise->set_value(responce);
    }
}

OperationResponce RdmaExecutor::executeOperation(OperationRequest & request)
{
    switch (request.type) {
        case OperationRequest::Type::send:
            return this->executeSend(request);
        case OperationRequest::Type::receive:
            return this->executeReceive(request);
        case OperationRequest::Type::read:
            return this->executeRead(request);
        case OperationRequest::Type::write:
            return this->executeWrite(request);
    }
    return { nullptr, errors::New("Unknown operation type") };
}

OperationResponce RdmaExecutor::executeSend(OperationRequest & request)
{
    // Send Operation:
    // 1. Create MemoryMap for the buffer
    // 2. Get doca::Buffer from BufferInventory
    // 3. Create RdmaSendTask from RdmaEngine
    // 4. Submit RdmaSendTask to RdmaEngine
    // 5. Wait for task completion (will be done after callback)

    auto [connectionId, connErr] = request.connection->GetId();
    if (connErr) {
        this->stats.failedOperations++;
        return { nullptr, errors::Wrap(connErr, "Failed to get connection ID") };
    }

    // Check that connection is active
    if (!this->activeConnections.contains(connectionId)) {
        this->stats.failedOperations++;
        return { nullptr, errors::New("No active RDMA connection available for send operation") };
    }

    // Lock for operation execution
    request.buffer->LockMemory();

    // Initialize operation state
    auto taskState = RdmaTaskInterface::State::idle;

    // Get buffer memory range
    auto [memoryRange, err] = request.buffer->GetMemoryRange();
    if (err) {
        this->stats.failedOperations++;
        return { nullptr, errors::Wrap(err, "Failed to get buffer memory range") };
    }
    auto & memorySpan = *memoryRange;

    // Create MemoryMap for the buffer
    auto [memoryMap, mapErr] = doca::MemoryMap::Create()
                                   .AddDevice(this->device)
                                   .SetPermissions(doca::AccessFlags::localReadWrite)
                                   .SetMemoryRange(memorySpan)
                                   .Start();
    if (mapErr) {
        this->stats.failedOperations++;
        return { nullptr, errors::Wrap(mapErr, "Failed to create MemoryMap for buffer") };
    }

    // Get doca::Buffer from BufferInventory
    auto [buffer, bufErr] = this->bufferInventory->AllocBuffer(memoryMap, memorySpan);
    if (bufErr) {
        this->stats.failedOperations++;
        return { nullptr, errors::Wrap(bufErr, "Failed to allocate buffer from BufferInventory") };
    }

    // Create RdmaSendTask from RdmaEngine
    // Set task user data to current transfer state: it will be changed in the task callbacks
    auto taskUserData = doca::Data(static_cast<void *>(&taskState));
    auto activeConnection = this->activeConnections.at(connectionId);
    auto [sendTask, taskErr] = this->rdmaEngine->AllocateSendTask(activeConnection, buffer, taskUserData);
    if (taskErr) {
        this->stats.failedOperations++;
        return { nullptr, errors::Wrap(taskErr, "Failed to allocate RDMA Send Task") };
    }

    // Submit RdmaSendTask to RdmaEngine
    taskState = RdmaTaskInterface::State::submitted;
    err = sendTask->Submit();
    if (err) {
        this->stats.failedOperations++;
        return { nullptr, errors::Wrap(err, "Failed to submit RDMA Send Task") };
    }

    // Wait for task completion
    err = this->waitForTaskState(RdmaTaskInterface::State::completed, taskState);
    if (err) {
        this->stats.failedOperations++;
        if (errors::Is(err, ErrorType::TimeoutExpired)) {
            return { nullptr, errors::Wrap(err, "Failed to wait for RDMA Send Task completion due to timeout") };
        }
        return { nullptr, errors::Wrap(err, "Failed to wait for RDMA Send Task completion") };
    }

    // Free RdmaSendTask
    sendTask->Free();

    // Decrement buffer reference count in BufferInventory
    auto [refcount, rcErr] = buffer->DecRefcount();
    if (rcErr) {
        this->stats.failedOperations++;
        return { nullptr, errors::Wrap(rcErr, "Failed to decrement buffer reference count in BufferInventory") };
    }

    // Unlock buffer memory
    request.buffer->UnlockMemory();

    this->stats.sendOperations++;
    return { request.buffer, nullptr };
}

OperationResponce RdmaExecutor::executeReceive(OperationRequest & request)
{
    // Receive Operation:
    // 1. Create MemoryMap for the buffer
    // 2. Get doca::Buffer from BufferInventory
    // 3. Create RdmaReceiveTask from RdmaEngine
    // 4. Submit RdmaReceiveTask to RdmaEngine
    // 5. Wait for task completion (will be done after callback)
    // 6. Get received bytes count from RdmaReceiveTask

    // auto [connectionId, connErr] = request.connection->GetId();
    // if (connErr) {
    //     this->stats.failedOperations++;
    //     return { nullptr, errors::Wrap(connErr, "Failed to get connection ID") };
    // }

    // // Check that connection is active
    // if (!this->activeConnections.contains(connectionId)) {
    //     this->stats.failedOperations++;
    //     return { nullptr, errors::New("No active RDMA connection available for receive operation") };
    // }

    // // Lock for operation execution
    // request.buffer->LockMemory();

    // // Initialize operation state
    // auto taskState = RdmaTaskInterface::State::idle;

    // // Get buffer memory range
    // auto [memoryRange, err] = request.buffer->GetMemoryRange();
    // if (err) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(err, "failed to get buffer memory range");
    // }
    // auto & memorySpan = *memoryRange;

    // // Create MemoryMap for the buffer
    // auto [memoryMap, mapErr] = doca::MemoryMap::Create()
    //                                .AddDevice(this->device)
    //                                .SetPermissions(doca::AccessFlags::localReadWrite)
    //                                .SetMemoryRange(memorySpan)
    //                                .Start();
    // if (mapErr) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(mapErr, "failed to create MemoryMap for buffer");
    // }

    // // Get doca::Buffer from BufferInventory
    // auto [buffer, bufErr] =
    //     this->bufferInventory->AllocBuffer(memoryMap, static_cast<void *>(memorySpan.data()), memorySpan.size());
    // if (bufErr) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(bufErr, "failed to allocate buffer from BufferInventory");
    // }

    // // Create RdmaReceiveTask from RdmaEngine
    // // Set task user data to current transfer state: it will be changed in the task callbacks
    // auto taskUserData = doca::Data(static_cast<void *>(&taskState));
    // auto [receiveTask, taskErr] = this->rdmaEngine->AllocateReceiveTask(buffer, taskUserData);
    // if (taskErr) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(taskErr, "failed to allocate RDMA Send Task");
    // }

    // // Submit RdmaReceiveTask to RdmaEngine
    // taskState = RdmaTaskInterface::State::submitted;
    // err = receiveTask->Submit();
    // if (err) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(err, "failed to submit RDMA Send Task");
    // }

    // // Wait for task completion
    // // FIXME: what if state will be error?
    // err = this->waitForTaskState(RdmaTaskInterface::State::completed, taskState);
    // if (err) {
    //     this->stats.failedOperations++;
    //     if (errors::Is(err, ErrorType::TimeoutExpired)) {
    //         return errors::Wrap(err, "failed to wait for RDMA Send Task completion due to timeout");
    //     }
    //     return errors::Wrap(err, "failed to wait for RDMA Send Task completion");
    // }

    // // Set affected bytes
    // auto [destBuffer, getErr] = receiveTask->GetBuffer(RdmaBufferType::destination);
    // if (getErr) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(getErr, "failed to get destination buffer from RDMA Receive Task");
    // }
    // auto [length, lenErr] = destBuffer->GetLength();
    // if (lenErr) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(lenErr, "failed to get length of destination buffer from RDMA Receive Task");
    // }
    // request.bytesAffected = length;
    // auto [_, dstRcErr] = destBuffer->DecRefcount();
    // if (dstRcErr) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(err, "failed to decrement reference count of destination buffer from RDMA Receive Task");
    // }

    // // Free RdmaSendTask
    // receiveTask->Free();

    // // Decrement buffer reference count in BufferInventory
    // auto [_, rcErr] = buffer->DecRefcount();
    // if (rcErr) {
    //     this->stats.failedOperations++;
    //     return errors::Wrap(rcErr, "failed to decrement buffer reference count in BufferInventory");
    // }

    // // Unlock buffer memory
    // request.buffer->UnlockMemory();

    this->stats.receiveOperations++;
    return { nullptr, nullptr };
}

OperationResponce RdmaExecutor::executeRead(OperationRequest & request)
{
    // TODO: implement execute

    this->stats.readOperations++;
    return { nullptr, nullptr };
}

OperationResponce RdmaExecutor::executeWrite(OperationRequest & request)
{
    // TODO: implement execute

    this->stats.writeOperations++;
    return { nullptr, nullptr };
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

error doca::rdma::RdmaExecutor::waitForTaskState(RdmaTaskInterface::State desiredState,
                                                 RdmaTaskInterface::State & changingState,
                                                 std::chrono::milliseconds waitTimeout = 0ms)
{
    if (this->progressEngine == nullptr) {
        return errors::New("ProgressEngine is null");
    }

    const auto startTime = std::chrono::steady_clock::now();
    while (changingState != desiredState) {
        if (this->timeoutExpired(startTime, waitTimeout)) {
            return ErrorType::TimeoutExpired;
        }
        std::this_thread::sleep_for(10us);
        this->progressEngine->Progress();
    }

    return nullptr;
}

error doca::rdma::RdmaExecutor::waitForConnectionState(RdmaConnection::State desiredState,
                                                       RdmaConnection::State & changingState,
                                                       std::chrono::milliseconds waitTimeout)
{
    if (this->progressEngine == nullptr) {
        return errors::New("ProgressEngine is null");
    }

    const auto startTime = std::chrono::steady_clock::now();
    while (changingState != desiredState) {
        if (this->timeoutExpired(startTime, waitTimeout)) {
            return ErrorType::TimeoutExpired;
        }
        std::this_thread::sleep_for(10us);
        this->progressEngine->Progress();
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
        auto [address, err] =
            RdmaAddress::Create(RdmaAddress::Type::ipv4, constants::serverAddress, constants::serverport);
        if (err) {
            return errors::Wrap(err, "failed to create server RDMA address");
        }

        // Set connection user data
        auto connectionState = RdmaConnection::State::idle;
        auto connectionUserData = doca::Data(static_cast<void *>(&connectionState));
        err = this->rdmaEngine->ConnectToAddress(address, connectionUserData);
        if (err) {
            return errors::Wrap(err, "failed to connect to server RDMA address");
        }

        // Wait for connection to be established
        const auto desiredState = RdmaConnection::State::established;
        err = this->waitForConnectionState(desiredState, connectionState);
        if (err) {
            if (errors::Is(err, ErrorType::TimeoutExpired)) {
                return errors::Wrap(err, "failed to wait for RDMA connection establishment due to timeout");
            }
            return errors::Wrap(err, "failed to wait for RDMA connection establishment");
        }
    }

    // If role is Server, start listen to port and accept connection
    if (role == RdmaConnectionRole::server) {
        // Start listening to server port
        auto err = this->rdmaEngine->ListenToPort(constants::serverport);
        if (err) {
            return errors::Wrap(err, "failed to start listening on server port");
        }

        // Wait for connection request
        while (this->requestedConnections.empty()) {
            std::this_thread::sleep_for(10us);
            this->progressEngine->Progress();
        }

        // Accept the first requested connection
        auto it = this->requestedConnections.begin();
        auto connection = it->second;
        err = connection->Accept();
        if (err) {
            return errors::Wrap(err, "failed to accept RDMA connection request");
        }

        // Set connection user data
        auto connectionUserData = doca::Data();
        err = connection->SetUserData(connectionUserData);
        if (err) {
            return errors::Wrap(err, "failed to set RDMA connection user data");
        }

        // Wait for connection to be established
        while (this->activeConnections.empty()) {
            std::this_thread::sleep_for(10us);
            this->progressEngine->Progress();
        }
    }

    return nullptr;
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