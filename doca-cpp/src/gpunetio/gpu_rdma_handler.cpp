#include <doca-cpp/core/error.hpp>
#include <doca-cpp/gpunetio/gpu_rdma_handler.hpp>

namespace doca::gpunetio
{

GpuRdmaHandlerPtr GpuRdmaHandler::Create(doca_gpu_dev_rdma * nativeHandler)
{
    return std::make_shared<GpuRdmaHandler>(nativeHandler);
}

std::tuple<GpuRdmaHandlerPtr, error> GpuRdmaHandler::CreateFromEngine(doca_rdma * rdmaEngine)
{
    doca_gpu_dev_rdma * gpuRdmaHandler = nullptr;
    auto err = doca::FromDocaError(doca_rdma_get_gpu_handle(rdmaEngine, &gpuRdmaHandler));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to get GPU RDMA handler from engine") };
    }

    return { std::make_shared<GpuRdmaHandler>(gpuRdmaHandler), nullptr };
}

doca_gpu_dev_rdma * GpuRdmaHandler::GetNative()
{
    return this->handler;
}

GpuRdmaHandler::GpuRdmaHandler(doca_gpu_dev_rdma * nativeHandler) : handler(nativeHandler) {}

}  // namespace doca::gpunetio
