# doca-cpp

C++23 RDMA streaming framework built on NVIDIA DOCA SDK. Provides high-performance CPU and GPU RDMA streaming with zero-copy data transfer, persistent GPU kernels, and cross-server aggregate processing.

## Project overview

doca-cpp wraps DOCA C APIs into modern C++ with RAII, Builder pattern, and `std::tuple<T, error>` returns. The library was redesigned from a request-based model into a streaming architecture targeting 37 Gbps on ConnectX-6.

Two main modules:
- **doca-cpp** (CPU) — `RdmaStreamServer`, `RdmaStreamClient`, `RdmaPipeline` with callback-driven task resubmission
- **doca-cpp-gpunetio** (GPU, optional) — `GpuRdmaServer`, `GpuRdmaClient`, persistent CUDA kernels, `PipelineControl` shared memory

## Building

```bash
cmake --preset amd64-linux-debug        # CPU only
cmake --preset amd64-linux-debug -DBUILD_GPUNETIO=ON -DBUILD_SAMPLES=ON  # with GPU + samples
```

Key CMake options:
- `BUILD_GPUNETIO=ON` — enables GPU module (requires CUDA 13.0+, DOCA GPUNetIO)
- `BUILD_SAMPLES=ON` — builds sample applications
- `ENABLE_LOGGING=ON` — enables doca-cpp logging macros (default ON)

When `BUILD_GPUNETIO=ON`, the macro `DOCA_CPP_ENABLE_GPUNETIO` is defined on doca-cpp target, enabling GPU-specific APIs like `RdmaEngine::Builder::SetDataPathOnGpu()`.

## Coding rules

All code must follow `agent/CODESTYLE.md`. Key points:
- C++23, no exceptions, `error` return type from `errors` library
- `std::tuple<T, error>` for fallible returns, error always last
- PascalCase public methods, camelCase private methods
- `this->` for all member access, no `m_` or `_` prefixes
- `auto`, `const`, `constexpr` everywhere possible
- No magic numbers — use `inline constexpr` in namespace
- Builder pattern for complex construction
- All objects via `std::shared_ptr` with `ClassNamePtr` alias
- `#pragma region` for Construction & Destruction and PrivateMethods sections
- DOCA_CPP_LOG macros require `std::format()` wrapper for formatted args:
  `DOCA_CPP_LOG_INFO(std::format("Port {}", port))`

## Architecture notes

- Library owns all memory. User provides `StreamConfig` (numBuffers, bufferSize, direction).
- Three-group buffer rotation per connection for triple-buffering overlap.
- CPU pipeline: pre-allocated tasks, callback-driven resubmission in tight `pe_progress()` loop.
- GPU pipeline: persistent CUDA kernel + `PipelineControl` in GPU+CPU shared memory.
- `StreamChain` links N servers for cross-server aggregate processing via `std::barrier`.
- `SessionManager` handles TCP OOB on single port — `AcceptOne()` returns new session per client.
- `SetDataPathOnGpu()` must be called before `SetReceiveQueueSize()` (DOCA limitation).
- Send/receive queue sizes must match numBuffers (one pre-allocated task per buffer).
- RDMA connections must be `Disconnect()`-ed before destroying engine/context resources.
- Use only existing DOCA wrappers (doca-cpp or mandoline patterns), not raw `doca_*` C calls in high-level code.
- `BufferArray` follows mandoline pattern: host-side `BufferArray` with Builder, then `RetrieveGpuBufferArray()` for GPU handle.

## Dependencies

- NVIDIA DOCA SDK (system)
- asio (vcpkg)
- errors (vcpkg) — https://github.com/fogesque/errors
- kvalog (vcpkg) — logging library
- yaml-cpp (vcpkg) — sample config parsing
- CUDA Toolkit 13.0+ (optional, for GPU module)
- DOCA GPUNetIO (optional, for GPU module)

## Repository structure

```
doca-cpp-redesign/
├── CMakeLists.txt                          # Root build, doca-cpp + optional doca-cpp-gpunetio
├── CMakePresets.json
├── vcpkg.json                              # vcpkg dependencies
├── vcpkg-configuration.json
├── CLAUDE.md                               # This file
├── README.md
├── LICENSE
├── tasks.md                                # Implementation task tracker
├── prompt.txt                              # Initial task prompt
│
├── agent/
│   └── CODESTYLE.md                        # C++ coding style rules (Russian)
│
├── plan/
│   ├── design.md                           # Streaming architecture design
│   ├── flow.md                             # CPU client -> GPU server code flow
│   └── report.md                           # Architecture decisions report
│
├── cmake/
│   ├── FindDOCA.cmake
│   └── doca-cpp-config.cmake.in
│
├── doca-cpp/                               # Main library
│   ├── include/doca-cpp/
│   │   ├── core/                           # DOCA C wrappers
│   │   │   ├── buffer.hpp                  # Buffer + BufferInventory
│   │   │   ├── context.hpp                 # DOCA Context
│   │   │   ├── device.hpp                  # Device + DeviceList + DeviceInfo
│   │   │   ├── error.hpp                   # DocaError wrapper
│   │   │   ├── interfaces.hpp              # IDestroyable, IStoppable
│   │   │   ├── mmap.hpp                    # MemoryMap + RemoteMemoryMap
│   │   │   ├── progress_engine.hpp         # ProgressEngine + ITask
│   │   │   ├── resource_scope.hpp          # RAII tiered resource teardown
│   │   │   └── types.hpp                   # AccessFlags, Data, enums, concepts
│   │   │
│   │   ├── rdma/                           # RDMA streaming (CPU)
│   │   │   ├── rdma_buffer_view.hpp        # RdmaBufferView — CPU memory view
│   │   │   ├── rdma_stream_config.hpp      # RdmaStreamConfig, RdmaStreamDirection, validation
│   │   │   ├── rdma_stream_service.hpp     # IRdmaStreamService, IRdmaAggregateStreamService
│   │   │   ├── rdma_stream_chain.hpp       # RdmaStreamChain — cross-server aggregate
│   │   │   ├── rdma_stream_server.hpp      # RdmaStreamServer — CPU streaming server
│   │   │   ├── rdma_stream_client.hpp      # RdmaStreamClient — CPU streaming client
│   │   │   └── internal/
│   │   │       ├── rdma_session_manager.hpp # TCP OOB session management
│   │   │       ├── rdma_buffer_pool.hpp    # Pinned CPU memory pool + DOCA buffers
│   │   │       ├── rdma_pipeline.hpp       # Pre-allocated task pipeline
│   │   │       ├── rdma_engine.hpp         # DOCA RDMA engine wrapper
│   │   │       ├── rdma_connection.hpp     # RDMA connection + address
│   │   │       └── rdma_task.hpp           # Write/Read/Send/Receive task wrappers
│   │   │
│   │   ├── gpunetio/                       # GPU RDMA streaming (optional)
│   │   │   ├── gpu_buffer_view.hpp         # GpuBufferView — GPU device memory view
│   │   │   ├── gpu_stream_service.hpp      # IGpuRdmaStreamService, IGpuAggregateStreamService
│   │   │   ├── gpu_device.hpp              # GpuDevice — doca_gpu wrapper
│   │   │   ├── gpu_manager.hpp             # GpuManager — CUDA runtime + streams
│   │   │   ├── gpu_memory_region.hpp       # GpuMemoryRegion — GPU memory + DMA buf mapping
│   │   │   ├── gpu_buffer_array.hpp        # BufferArray + GpuBufferArray
│   │   │   ├── gpu_rdma_handler.hpp        # GpuRdmaHandler — doca_gpu_dev_rdma wrapper
│   │   │   ├── gpu_rdma_pipeline.hpp       # GpuRdmaPipeline — persistent kernel pipeline
│   │   │   ├── gpu_rdma_server.hpp         # GpuRdmaServer — GPU streaming server
│   │   │   ├── gpu_rdma_client.hpp         # GpuRdmaClient — GPU streaming client
│   │   │   ├── gpu_pipeline_control.hpp    # GpuPipelineControl — GPU+CPU shared flags
│   │   │   └── kernels/
│   │   │       ├── kernel_common.cuh       # Common kernel includes and defines
│   │   │       ├── server_kernel.cuh       # Persistent server kernel declaration
│   │   │       └── client_kernel.cuh       # Persistent client kernel declaration
│   │   │
│   │   ├── flow/                           # DOCA Flow packet processing (partial)
│   │   │   ├���─ flow.hpp
│   │   │   ├── flow_config.hpp
│   │   │   ├── flow_pipe.hpp
│   │   │   └── flow_port.hpp
│   │   │
│   │   └── logging/
│   │       └── logging.hpp                 # Conditional logging macros via kvalog
│   │
│   └── src/                                # Implementation files mirror include/ layout
│       ├── core/                           # buffer.cpp, context.cpp, device.cpp, mmap.cpp, ...
│       ├── rdma/                           # rdma_stream_config.cpp, rdma_buffer_view.cpp, rdma_stream_chain.cpp, ...
│       │   └── internal/                   # rdma_session_manager.cpp, rdma_buffer_pool.cpp, rdma_pipeline.cpp, ...
│       ├── gpunetio/                       # gpu_device.cpp, gpu_manager.cpp, gpu_memory_region.cpp, ...
│       │   └── kernels/                    # server_kernel.cu, client_kernel.cu
│       └── flow/                           # flow_config.cpp, flow_pipe.cpp, flow_port.cpp
│
├── samples/
│   ├── CMakeLists.txt
│   ├── device_discovery/                   # Enumerates DOCA devices
│   ├── cpu_cpu_throughput/                 # CPU client + CPU server throughput benchmark
│   │   ├── client.cpp, server.cpp, common.hpp
│   │   ├── cpu_cpu_throughput_configs.yaml
│   │   └── CMakeLists.txt
│   ├── cpu_gpu_throughput/                 # CPU client + GPU server throughput benchmark
│   │   ├── client.cpp, server.cpp, common.hpp
│   │   ├── cpu_gpu_throughput_configs.yaml
│   │   └── CMakeLists.txt
│   └── fblms_streaming/                    # 4-channel FBLMS with StreamChain
│       ├── client.cpp, server.cpp, common.hpp
│       ├── fft_service.hpp                 # Per-channel cuFFT service
│       ├── fblms_service.hpp               # Cross-channel FBLMS aggregate (Thrust)
│       ├── sensor_service.hpp              # Synthetic sensor data generator
│       ├── fblms_streaming_configs.yaml
│       └── CMakeLists.txt
│
├── examples/
│   └── gpunetio/mandoline/                 # Reference GPU RDMA library (ported to doca-cpp-gpunetio)
│       ├── include/mandoline/              # Headers: core/, gpu/, rdma/, logging/
│       ├── source/                         # Implementations
│       └── samples/rdma_client_server/     # Mandoline sample
│
└── integration/
    ├── fblms/                              # CPU FBLMS algorithm (FFTW3)
    ├── fblms-cuda/                         # GPU FBLMS algorithm (cuFFT + Thrust)
    ├── fblms-runner/                       # FBLMS test runner with signal generation
    ├── matlab/                             # MATLAB FBLMS reference
    └── visualize/                          # Python visualization
```
