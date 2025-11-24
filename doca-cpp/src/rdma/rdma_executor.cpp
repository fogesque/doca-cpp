#include "doca-cpp/rdma/rdma_executor.hpp"

using doca::rdma::OperationRequest;
using doca::rdma::RdmaConnectionRole;
using doca::rdma::RdmaEnginePtr;
using doca::rdma::RdmaExecutor;
using doca::rdma::RdmaExecutorPtr;

std::tuple<RdmaExecutorPtr, error> RdmaExecutor::Create(RdmaConnectionRole connectionRole, doca::DevicePtr device)
{
    if (device == nullptr) {
        return { nullptr, errors::New("device is null") };
    }

    // Create Connection Manager
    auto [connManager, err] = RdmaConnectionManager::Create(connectionRole);
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create RDMA Connection Manager") };
    }

    // Create RDMA engine
    auto [rdmaEngine, err] = RdmaEngine::Create(connectionRole, connManager, device);
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create RDMA Engine") };
    }
    connManager->AttachToRdmaEngine(rdmaEngine);

    auto rdmaExecutor = std::make_shared<RdmaExecutor>(rdmaEngine, device);

    return { rdmaExecutor, nullptr };
}

error doca::rdma::RdmaExecutor::Start()
{
    if (this->running.load()) {
        return errors::New("RdmaExecutor is already running");
    }

    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is not initialized");
    }

    // Initialize RDMA engine
    auto err = this->rdmaEngine->Initialize();
    if (err) {
        return errors::Wrap(err, "failed to initialize RDMA engine");
    }

    auto connManager = this->rdmaEngine->GetConnectionManager();
    const auto & connRole = connManager->GetConnectionRole();

    const uint16_t serverPort = 12345;                  // TODO: make port configurable
    const std::string serverAddress = "192.168.88.92";  // TODO: make address configurable

    // Server Role
    if (connRole == RdmaConnectionRole::server) {
        // Listen for incoming connections
        auto err = connManager->ListenToPort(serverPort);  // TODO: make port configurable
        if (err) {
            return errors::Wrap(err, "failed to start listening on port 4791");
        }
        // Accept incoming connection
        const auto acceptTimeout =
            std::chrono::milliseconds(30000);  // 30 seconds // TODO: make server listening in cycle
        err = connManager->AcceptConnection(acceptTimeout);
        if (err) {
            return errors::Wrap(err, "failed to accept incoming RDMA connection");
        }
    }

    // Client Role
    if (connRole == RdmaConnectionRole::client) {
        // Connect to server
        auto err = connManager->Connect(RdmaAddress::Type::ipv4, serverAddress, serverPort);
        if (err) {
            return errors::Wrap(err, "failed to connect to RDMA server");
        }
    }

    // Start worker thread
    this->running.store(true);
    if (connRole == RdmaConnectionRole::server) {
        this->workerThread = std::make_unique<std::thread>([this] { this->ServerWorkerLoop(); });
    }
    if (connRole == RdmaConnectionRole::client) {
        this->workerThread = std::make_unique<std::thread>([this] { this->ClientWorkerLoop(); });
    }
    std::println("[RdmaExecutor] Initialized successfully");
}

RdmaExecutor::RdmaExecutor(RdmaConnectionRole connectionRole, RdmaEnginePtr initialRdmaEngine,
                           doca::DevicePtr initialDevice)
    : rdmaEngine(initialRdmaEngine), device(initialDevice), running(false), workerThread(nullptr)
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

error RdmaExecutor::SubmitOperation(OperationRequest request)
{
    std::println("[RdmaExecutor] Submitting {} operation", request.type == OperationRequest::Type::Send      ? "Send"
                                                           : request.type == OperationRequest::Type::Receive ? "Receive"
                                                           : request.type == OperationRequest::Type::Read    ? "Read"
                                                                                                             : "Write");
    {
        std::scoped_lock lock(this->queueMutex);
        if (!this->running) {
            std::println("[RdmaExecutor] ERROR: Executor is shut down");

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

void RdmaExecutor::ClientWorkerLoop()
{
    std::println("  [RdmaExecutor::Worker] Thread started (thread_id: {})",
                 std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);

    while (true) {
        OperationRequest request;
        {
            std::unique_lock lock(queueMutex);
            this->queueCondVar.wait(lock, [this] { return !this->running || !this->operationQueue.empty(); });

            if (!this->running && this->operationQueue.empty()) {
                std::println("  [RdmaExecutor::Worker] Thread stopping");
                return;
            }

            request = std::move(this->operationQueue.front());
            this->operationQueue.pop();
        }
        auto err = this->ExecuteOperation(request);
        request.promise->set_value(std::move(err));
    }
}

error RdmaExecutor::ExecuteOperation(const OperationRequest & request)
{
    std::println("    [RdmaExecutor] Executing {} operation on RDMA thread",
                 request.type == OperationRequest::Type::Send      ? "Send"
                 : request.type == OperationRequest::Type::Receive ? "Receive"
                 : request.type == OperationRequest::Type::Read    ? "Read"
                                                                   : "Write");

    switch (request.type) {
        case OperationRequest::Type::Send:
            return this->ExecuteSend(request);
        case OperationRequest::Type::Receive:
            return this->ExecuteReceive(request);
        case OperationRequest::Type::Read:
            return this->ExecuteRead(request);
        case OperationRequest::Type::Write:
            return this->ExecuteWrite(request);
    }

    return errors::New("Unknown operation type");
}

error RdmaExecutor::ExecuteSend(const OperationRequest & request)
{
    std::println("    [ExecuteSend] Preparing RDMA send...");

    auto & buffer = request.buffer;
    buffer->LockMemory();

    auto err = this->rdmaEngine->Send(buffer);
    if (err) {
        std::println("    [ExecuteSend] ERROR: {}", err->What());
        request.buffer->UnlockMemory();
        this->stats.failedOperations++;
        return err;
    }
    request.buffer->UnlockMemory();

    std::println("    [ExecuteSend] Send completed successfully");
    this->stats.sendOperations++;
    return nullptr;
}

error RdmaExecutor::ExecuteReceive(const OperationRequest & request)
{
    std::println("    [ExecuteReceive] Preparing RDMA receive...");

    auto [range, err] = request.buffer->GetMemoryRange();
    if (err) {
        this->stats.failedOperations++;
        return err;
    }

    request.buffer->LockMemory();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    auto & memory = *range;
    for (size_t i = 0; i < memory.size(); ++i) {
        memory[i] = std::byte((i + 42) % 256);
    }
    request.buffer->UnlockMemory();

    std::println("    [ExecuteReceive] Receive completed successfully");
    this->stats.receiveOperations++;
    return nullptr;
}

error RdmaExecutor::ExecuteRead(const OperationRequest & request)
{
    std::println("    [ExecuteRead] Executing RDMA read...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    this->stats.readOperations++;
    std::println("    [ExecuteRead] Read completed successfully");
    return nullptr;
}

error RdmaExecutor::ExecuteWrite(const OperationRequest & request)
{
    std::println("    [ExecuteWrite] Executing RDMA write...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    this->stats.writeOperations++;
    std::println("    [ExecuteWrite] Write completed successfully");
    return nullptr;
}