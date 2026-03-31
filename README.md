# doca-cpp

C++ High-Performance RDMA Streaming Framework built on NVIDIA DOCA SDK

> **Note:** The library may be renamed in the future to a more generic name that does not reference DOCA directly.

> **Note:** This repository is for experimenting, testing and demonstration purposes only. Yet it is not intended for production use.

## Overview

doca-cpp is a C++23 framework that provides high-performance RDMA streaming for both CPU and GPU memory. The library wraps NVIDIA DOCA C APIs into modern C++ with RAII, Builder pattern, and zero-copy data transfer.

The framework was designed from scratch as a **streaming architecture** targeting line-rate throughput (37 Gbps on ConnectX-6 40G NIC). It eliminates the bottlenecks of traditional request-based RDMA approaches through pre-allocated tasks, callback-driven resubmission, and persistent GPU kernels.

### Key Features

- **Library-owned memory** — user specifies buffer count and size, library handles allocation, pinning, registration, and triple-buffering
- **Split CPU/GPU type system** — `RdmaBufferView` (CPU, safe to dereference) vs `GpuBufferView` (GPU device pointer for CUDA APIs)
- **Split service interfaces** — `IRdmaStreamService::OnBuffer(RdmaBufferView)` vs `IGpuRdmaStreamService::OnBuffer(GpuBufferView, cudaStream_t)`
- **Pre-allocated pipeline** — all RDMA tasks allocated once, callback-driven resubmission in tight `pe_progress()` loop
- **Persistent GPU kernels** — launched once, never relaunched, 0.1-1 us gap between rounds (vs 13-55 us with kernel relaunch)
- **Cross-server aggregation** — `RdmaStreamChain` synchronizes N servers via `std::barrier` for per-buffer aggregate processing
- **Zero-copy hot path** — NIC reads from pinned CPU memory via DMA, writes to GPU memory via PCIe/RDMA. No copies in the data path.

## Architecture

The library consists of two modules:

```
┌─────────────────────────────────────────────────────────────┐
│  doca-cpp (CPU)                                             │
│                                                             │
│  RdmaStreamServer / RdmaStreamClient                        │
│  RdmaPipeline (callback-driven task resubmission)           │
│  RdmaBufferPool (pinned CPU memory, DOCA mmap)              │
│  RdmaSessionManager (TCP OOB descriptor exchange)           │
│  RdmaStreamChain (cross-server aggregate)                   │
│                                                             │
│  Core DOCA wrappers: Device, MemoryMap, Buffer,             │
│  BufferInventory, Context, ProgressEngine, RdmaEngine       │
├─────────────────────────────────────────────────────────────┤
│  doca-cpp-gpunetio (GPU, optional)                          │
│                                                             │
│  GpuRdmaServer / GpuRdmaClient                              │
│  GpuRdmaPipeline (persistent kernel + PipelineControl)      │
│  GpuMemoryRegion (GPU memory, DMA buf mapping)              │
│  BufferArray / GpuBufferArray (device-side buffer handles)  │
│  Persistent CUDA kernels (server + client)                  │
└─────────────────────────────────────────────────────────────┘
```

### Streaming Model

Every server and client operates on a `RdmaStreamConfig`:

```cpp
doca::rdma::RdmaStreamConfig config{
    .numBuffers = 64,                                    // 3-128 buffers
    .bufferSize = 32 * 1024 * 1024,                      // 32 MB per buffer
    .direction  = doca::rdma::RdmaStreamDirection::write, // or ::read
};
```

The library allocates one contiguous memory region, registers it with the DOCA device, and divides it into 3 groups for triple-buffering rotation:

```
Group 0: ██ RDMA ██│░░ Processing ░░│·· Ready ··│██ RDMA ██│░░ ...
Group 1: ·· Ready ··│██ RDMA ██│░░ Processing ░░│·· Ready ··│██ ...
Group 2: ░░ Processing ░░│·· Ready ··│██ RDMA ██│░░ Processing ░░│...
```

### Execution Flow

```
┌────────────────────┐                         ┌────────────────────┐
│   CPU Client       │                         │   Server (CPU/GPU) │
│                    │                         │                    │
│ 1. Build client    │                         │ 1. Build server    │
│ 2. Connect()       │── TCP connect ─────────►│ 2. Serve()         │
│                    │                         │    AcceptOne()     │
│                    │◄─ descriptor exchange ─►│                    │
│                    │── RDMA CM connect ─────►│                    │
│ 3. Start()         │                         │                    │
│    submit tasks    │                         │    pipeline starts │
│                    │                         │                    │
│ ┌────────────────┐ │    RDMA write/read      │ ┌────────────────┐ │
│ │ pe_progress()  │ │════════════════════════►│ │ pe_progress()  │ │
│ │ tight loop     │ │    zero-copy DMA        │ │ or persistent  │ │
│ │ callback       │ │                         │ │ GPU kernel     │ │
│ │ resubmission   │ │                         │ │                │ │
│ └────────────────┘ │                         │ │ OnBuffer()     │ │
│                    │                         │ │ user service   │ │
│ 4. Stop()          │                         │ └────────────────┘ │
│ 5. Disconnect()    │                         │ Shutdown()         │
└────────────────────┘                         └────────────────────┘
```

## Code Examples

### CPU Write Stream: Client Sends Data to Server

**Client — fills buffers with sensor data:**

```cpp
class SensorProducer : public doca::rdma::IRdmaStreamService {
    void OnBuffer(doca::rdma::RdmaBufferView buffer) override {
        auto * data = buffer.DataAs<float>();
        sensor.ReadSamples(data, buffer.Count<float>());
    }
};

auto [client, err] = doca::rdma::RdmaStreamClient::Create()
    .SetDevice(device)
    .SetRdmaStreamConfig({.numBuffers = 64, .bufferSize = 32_MB, .direction = write})
    .SetService(std::make_shared<SensorProducer>(sensor))
    .Build();

client->Connect("192.168.1.100", 54321);
client->Start();
```

**Server — processes received data:**

```cpp
class Processor : public doca::rdma::IRdmaStreamService {
    void OnBuffer(doca::rdma::RdmaBufferView buffer) override {
        auto samples = buffer.AsSpan<float>();
        // Process received data...
    }
};

auto [server, err] = doca::rdma::RdmaStreamServer::Create()
    .SetDevice(device)
    .SetListenPort(54321)
    .SetRdmaStreamConfig({.numBuffers = 64, .bufferSize = 32_MB, .direction = write})
    .SetService(std::make_shared<Processor>())
    .Build();

server->Serve();  // blocks, accepts clients
```

### GPU Server: CPU Client Writes, GPU Server Processes with cuFFT

```cpp
class GpuFftProcessor : public doca::gpunetio::IGpuRdmaStreamService {
    cufftHandle plan;
public:
    void OnBuffer(doca::gpunetio::GpuBufferView buffer, cudaStream_t stream) override {
        // buffer.Data() is GPU pointer — data arrived via RDMA Write
        cufftSetStream(plan, stream);
        cufftExecC2C(plan, buffer.DataAs<cufftComplex>(),
                     buffer.DataAs<cufftComplex>(), CUFFT_FORWARD);
        // Do NOT synchronize — library handles it
    }
};

auto [server, err] = doca::gpunetio::GpuRdmaServer::Create()
    .SetDevice(device)
    .SetGpuDevice(gpuDevice)
    .SetGpuPcieBdfAddress("41:00.0")
    .SetListenPort(54321)
    .SetStreamConfig({.numBuffers = 64, .bufferSize = 32_MB, .direction = write})
    .SetService(std::make_shared<GpuFftProcessor>())
    .Build();

server->Serve();
```

The CPU client code is identical to the previous example — it doesn't know the server uses GPU memory.

### Multi-Server StreamChain with Cross-Channel Aggregate

4 GPU servers on different NICs, 4 CPU clients streaming sensor data. Per-channel cuFFT + cross-channel FBLMS aggregate:

```cpp
// Per-channel service: FFT on each buffer
class ChannelFft : public doca::gpunetio::IGpuRdmaStreamService {
    void OnBuffer(doca::gpunetio::GpuBufferView buffer, cudaStream_t stream) override {
        cufftSetStream(plan, stream);
        cufftExecC2C(plan, buffer.DataAs<cufftComplex>(),
                     buffer.DataAs<cufftComplex>(), CUFFT_FORWARD);
    }
};

// Aggregate service: FBLMS weight update across all channels
class FblmsAggregate : public doca::gpunetio::IGpuAggregateStreamService {
    void OnAggregate(std::span<doca::gpunetio::GpuBufferView> channels,
                     cudaStream_t stream) override {
        // channels.size() == 4 (one per server)
        // Weighted sum, error computation, weight update via Thrust
    }
};

// Create 4 GPU servers
auto [server0, _] = doca::gpunetio::GpuRdmaServer::Create()
    .SetDevice(dev0).SetGpuDevice(gpu).SetListenPort(54321)
    .SetStreamConfig(config).SetService(fft).SetAggregateService(fblms).Build();
// ... server1, server2, server3

// Chain with aggregate
auto [chain, err] = doca::rdma::RdmaStreamChain::Create()
    .AddServer(server0).AddServer(server1)
    .AddServer(server2).AddServer(server3)
    .SetAggregateService(fblmsService)
    .Build();

chain->Serve();
// Per buffer: 4x ChannelFft::OnBuffer (parallel) -> FblmsAggregate::OnAggregate
```

## Configuration

All samples are configured via YAML files placed next to the executable:

```yaml
# cpu_cpu_throughput_configs.yaml
sample:
  log-level: 3           # Off:0 Trace:1 Debug:2 Info:3 Warning:4 Error:5 Critical:6
  doca-log-level: 0
  measurement-duration: 10

stream:
  num-buffers: 64
  buffer-size: 33554432   # 32 MB

server:
  device: "mlx5_0"
  port: 54321

client:
  device: "mlx5_1"
  server-ipv4: "192.168.88.20"
  server-port: 54321
```

## Prerequisites

A DOCA-compatible Linux operating system and kernel are required. Refer to [NVIDIA DOCA Documentation](https://docs.nvidia.com/doca/sdk/doca-framework/index.html) for supported OS and kernel versions.

To build the library and samples:

* CMake (3.20+), Ninja, vcpkg
* GCC-14+ with C++23 support
* DOCA libraries: DOCA Common, DOCA RDMA
* *Optional for GPU module:* CUDA Toolkit 13.0+, DOCA GPUNetIO

DOCA libraries can be installed via the `doca-networking` installation profile. Refer to [NVIDIA DOCA Documentation](https://docs.nvidia.com/doca/sdk/doca-framework/index.html) for details.

The library has been tested on `amd64` with Ubuntu 24.04 and Linux Kernel 6.8.0-31, using ConnectX-6 DX SmartNIC devices and NVIDIA RTX 5000 Ada GPU devices.

## Build

### Install Overlay Ports

The library depends on custom C++ packages distributed via a vcpkg overlay:

```bash
cd <your_directory>
git clone https://github.com/fogesque/vcpkg-ports.git
export VCPKG_OVERLAY_PORTS="<your_directory>/vcpkg-ports"
```

### Build the Library (CPU only)

```bash
cmake -S . -B build --preset amd64-linux-debug
cmake --build build --target doca-cpp
```

### Build with GPU Module

```bash
cmake -S . -B build --preset amd64-linux-debug -DBUILD_GPUNETIO=ON
cmake --build build --target doca-cpp-gpunetio
```

### Build Samples

```bash
cmake -S . -B build --preset amd64-linux-debug -DBUILD_SAMPLES=ON -DBUILD_GPUNETIO=ON
cmake --build build
```

Available samples:

| Sample | Description | Requires GPU |
|--------|-------------|:---:|
| `cpu_cpu_throughput` | CPU client + CPU server throughput benchmark | No |
| `cpu_gpu_throughput` | CPU client + GPU server throughput benchmark | Yes |
| `fblms_streaming` | 4-channel FBLMS with StreamChain (cuFFT + Thrust) | Yes |
| `device_discovery` | Enumerates available DOCA devices | No |

## DOCA Background

DOCA Common provides foundational abstractions: `Device` (network device), `MemoryMap` (grants device access to memory), `Buffer` / `BufferInventory` (memory region management), `ProgressEngine` (polls asynchronous task completions), and `Context` (connects tasks to the execution environment).

DOCA RDMA builds on top of these, providing RDMA operations and connection management via RDMA CM (Connection Manager). The library uses **Write** and **Read** operations for one-sided data transfer without requiring a matching task on the remote peer.

| Task  | Data Direction                      |
|:------|:------------------------------------|
| Write | Write Requester -> Write Responder  |
| Read  | Read Responder -> Read Requester    |

DOCA GPUNetIO extends this with GPU-direct RDMA, where the NIC reads/writes directly to GPU device memory via PCIe DMA, enabling zero-copy GPU processing of RDMA data.

## Zero-Copy Data Path

```
Sensor HW -> CPU buffer (pinned) -> NIC DMA read -> RDMA write -> GPU buffer -> cuFFT -> Aggregate
                                      ^                              ^
                                      |                              |
                               no CPU copy                    no GPU copy
                               NIC reads directly             service operates on
                               from pinned memory             RDMA destination memory
```
