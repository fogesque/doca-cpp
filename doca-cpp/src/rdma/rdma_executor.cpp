#include "doca-cpp/rdma/rdma_executor.hpp"

std::tuple<doca::rdma::RdmaExecutorPtr, error> doca::rdma::RdmaExecutor::Create(doca::DevicePtr device)
{
    if (device == nullptr) {
        return { nullptr, errors::New("device is null") };
    }

    auto [rdmaEngine, err] = RdmaEngine::Create(device);
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create RDMA engine for executor") };
    }

    auto rdmaExecutor = std::make_shared<RdmaExecutor>(rdmaEngine, device);

    return { rdmaExecutor, nullptr };
}

doca::rdma::RdmaExecutor::RdmaExecutor(RdmaEnginePtr initialRdmaEngine, doca::DevicePtr initialDevice)
    : rdmaEngine(initialRdmaEngine), device(initialDevice)
{
    std::println("[RdmaExecutor] Initializing...");
    this->running.store(true);
    this->workerThread = std::thread([this] { this->WorkerLoop(); });
    std::println("[RdmaExecutor] Initialized successfully");
}

doca::rdma::RdmaExecutor::~RdmaExecutor()
{
    std::println("[RdmaExecutor] Shutting down...");
    {
        std::scoped_lock lock(this->queueMutex);
        this->running.store(false);
    }
    this->queueCondVar.notify_one();

    if (this->workerThread.joinable()) {
        this->workerThread.join();
    }
    std::println("[RdmaExecutor] Shutdown complete");
}

error doca::rdma::RdmaExecutor::SubmitOperation(OperationRequest request)
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

const doca::rdma::RdmaExecutor::Statistics & doca::rdma::RdmaExecutor::GetStatistics() const
{
    return this->stats;
}

void doca::rdma::RdmaExecutor::WorkerLoop()
{
    std::println("  [RdmaExecutor::Worker] Thread started (thread_id: {})",
                 std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);

    // Initialize RDMA engine
    auto err = this->rdmaEngine->Initialize();
    if (err) {
        this->running.store(false);
        std::println("  [RdmaExecutor::Worker] ERROR: Failed to initialize RDMA engine: {}", err->What());
        return;
    }

    // Start RDMA context with timeout
    err = this->rdmaEngine->StartContext();
    if (err) {
        this->running.store(false);
        std::println("  [RdmaExecutor::Worker] ERROR: Failed to start RDMA context: {}", err->What());
        return;
    }

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

error doca::rdma::RdmaExecutor::ExecuteOperation(const OperationRequest & request)
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

error doca::rdma::RdmaExecutor::ExecuteSend(const OperationRequest & request)
{
    std::println("    [ExecuteSend] Preparing RDMA send...");

    // Get memory range from buffer
    auto [range, err] = request.buffer->GetMemoryRange();
    if (err) {
        std::println("    [ExecuteSend] ERROR: {}", err->What());
        this->stats.failedOperations++;
        return err;
    }

    // Lock memory in buffer
    request.buffer->LockMemory();

    // this->rdmaEngine->SubmitTask();

    // while (!this->rdmaEngine->Progressed()) {    }

    // Unlock memory in buffer
    request.buffer->UnlockMemory();

    std::println("    [ExecuteSend] Send completed successfully");
    this->stats.sendOperations++;
    return nullptr;
}

error doca::rdma::RdmaExecutor::ExecuteReceive(const OperationRequest & request)
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

error doca::rdma::RdmaExecutor::ExecuteRead(const OperationRequest & request)
{
    std::println("    [ExecuteRead] Executing RDMA read...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    this->stats.readOperations++;
    std::println("    [ExecuteRead] Read completed successfully");
    return nullptr;
}

error doca::rdma::RdmaExecutor::ExecuteWrite(const OperationRequest & request)
{
    std::println("    [ExecuteWrite] Executing RDMA write...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    this->stats.writeOperations++;
    std::println("    [ExecuteWrite] Write completed successfully");
    return nullptr;
}