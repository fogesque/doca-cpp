/**
 * @file gpu_rdma_handler.cpp
 * @brief Device-side GPU RDMA handle wrapper implementation
 *
 * Exports a GPU-side RDMA handle from an existing RDMA engine via
 * doca_rdma_get_gpu_handle(). The handle allows GPU kernels to post
 * RDMA operations directly without CPU involvement.
 */

#include "doca-cpp/gpunetio/gpu_rdma_handler.hpp"

#include <doca_gpunetio.h>
#include <doca_rdma.h>

using doca::gpunetio::GpuRdmaHandler;
using doca::gpunetio::GpuRdmaHandlerPtr;

#pragma region GpuRdmaHandler::Create

std::tuple<GpuRdmaHandlerPtr, error> GpuRdmaHandler::Create(
    doca::rdma::RdmaEnginePtr engine,
    GpuDevicePtr gpuDevice)
{
    if (!engine) {
        return { nullptr, errors::New("RDMA engine is null") };
    }
    if (!gpuDevice) {
        return { nullptr, errors::New("GPU device is null") };
    }

    // Export device-side RDMA handle from the RDMA engine
    doca_gpu_dev_rdma * gpuRdmaHandle = nullptr;
    auto docaErr = doca_rdma_get_gpu_handle(engine->GetNative(), gpuDevice->GetNative(), &gpuRdmaHandle);
    if (docaErr != DOCA_SUCCESS) {
        return { nullptr, errors::Wrap(FromDocaError(docaErr), "Failed to get GPU RDMA handle from engine") };
    }

    auto handler = std::make_shared<GpuRdmaHandler>(gpuRdmaHandle, engine);
    return { handler, nullptr };
}

#pragma endregion

#pragma region GpuRdmaHandler::Operations

doca_gpu_dev_rdma * GpuRdmaHandler::GetNative() const
{
    return this->nativeHandler;
}

error GpuRdmaHandler::Destroy()
{
    // The GPU handle is owned by the RDMA engine — no explicit free needed.
    // Setting to null prevents use after the engine is destroyed.
    this->nativeHandler = nullptr;
    this->engine.reset();
    return nullptr;
}

#pragma endregion

#pragma region GpuRdmaHandler::Construct

GpuRdmaHandler::GpuRdmaHandler(doca_gpu_dev_rdma * nativeHandler, doca::rdma::RdmaEnginePtr engine)
    : nativeHandler(nativeHandler), engine(std::move(engine))
{
}

GpuRdmaHandler::~GpuRdmaHandler()
{
    std::ignore = this->Destroy();
}

#pragma endregion
