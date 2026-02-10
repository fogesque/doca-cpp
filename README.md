# doca-cpp

C++ Adapter for NVIDIA DOCA Framework

> **Note:** The library may be renamed in the future to a more generic name that does not reference DOCA directly.

> **Note:** This repository is for experimenting, testing and demonstration purposes only. Yet it is not intended for production use.

## Library Overview

NVIDIA DOCA is a framework for network and data processing offloading. It consists of multiple libraries exposing C APIs. This repository provides C++ OOP wrappers around selected DOCA libraries so their functionality can be used easily from modern C++.

Currently implemented:
- **DOCA Common** — device management, memory mapping, task execution model
- **DOCA RDMA** — RDMA operations (Write and Read) with a client-server architecture

Planned:
- **DOCA GPUNetIO** — GPU-accelerated networking where packets are delivered directly to GPU memory

## Prerequisites

A DOCA-compatible Linux operating system and kernel are required. Refer to [NVIDIA DOCA Documentation](https://docs.nvidia.com/doca/sdk/doca-framework/index.html) for supported OS and kernel versions.

To build the library and samples, make sure you have:

* CMake (3.20+), Ninja, vcpkg
* GCC-14+
* DOCA libraries:
  - DOCA Common
  - DOCA RDMA

DOCA libraries can be installed via the `doca-networking` installation profile. Refer to [NVIDIA DOCA Documentation](https://docs.nvidia.com/doca/sdk/doca-framework/index.html) for details.

The library has been tested on `amd64` with Ubuntu 24.04 and Linux Kernel 6.8.0-31, using two ConnectX-6 DX SmartNIC devices. DOCA versions 3.0.0 through 3.2.0 were used.

## Build

### Install Overlay Ports

The library depends on custom C++ packages distributed via a vcpkg overlay. To make vcpkg install them automatically during CMake configuration:

```bash
cd <your_directory>
git clone https://github.com/fogesque/vcpkg-ports.git
export VCPKG_OVERLAY_PORTS="<your_directory>/vcpkg-ports"  # Or add to ~/.profile or ~/.bashrc
```

### Build the Library

```bash
cmake -S . -B build --preset amd64-linux-debug
cmake --build build --target doca-cpp
```

### Build Samples

Samples require additional dependencies (e.g., `yaml-cpp`). Enable or disable it by editing CMakePresets.json.

```bash
cmake -S . -B build --preset amd64-linux-debug
cmake --build build --target <sample_name>
```

Available samples:
- `rdma_client_server` — RDMA client and server communicating via Write/Read operations
- `device_discovery` — enumerates available DOCA devices and their properties

## Library Development Notes

### DOCA Background

DOCA Common provides foundational abstractions: `Device` (network device), `MemoryMap` (grants device access to memory), `Buffer` / `BufferInventory` (memory region management), `ProgressEngine` (polls asynchronous task completions), and `Context` (connects tasks to the execution environment).

DOCA RDMA builds on top of these, providing RDMA operations and connection management via RDMA CM (Connection Manager). The library focuses on **Write** and **Read** operations, which allow one-sided data transfer without requiring a matching task on the remote peer. The Requester must obtain a memory descriptor from the Responder before issuing a Write or Read.

| Task  | Data Direction                      |
|:------|:------------------------------------|
| Write | Write Requester → Write Responder   |
| Read  | Read Responder → Read Requester     |

> **Note:** RDMA Send/Receive operations and multiple simultaneous connections have been deprecated in the current version. The library now exclusively uses Write and Read for data transfer.

## Architecture Overview

The library is organized into three layers:

1. **High-Level API** — user-facing classes for building RDMA applications
2. **Internal Logic** — execution engine, session management, communication protocol
3. **DOCA C Wrappers** — thin RAII wrappers around DOCA C structures

### High-Level API

The public API consists of four main components:

**`RdmaServer`** listens for client connections and processes RDMA requests according to registered endpoints. Created via a builder:

```cpp
auto [server, err] = doca::rdma::RdmaServer::Create()
    .SetDevice(device)
    .SetListenPort(12345)
    .Build();
```

**`RdmaClient`** connects to a server and requests RDMA operations on specific endpoints:

```cpp
auto [client, err] = doca::rdma::RdmaClient::Create(device);
client->Connect(serverAddress, serverPort);
client->RequestEndpointProcessing(endpointId);
```

**`RdmaEndpoint`** represents a named RDMA operation with an associated memory buffer. Each endpoint has a path (a URI-like identifier such as `/rdma/ep0`) and a type (`write` or `read`). Two endpoints may share the same path but differ in type, meaning the same buffer can be used for both writing and reading. Created via a builder:

```cpp
auto [ep, err] = doca::rdma::RdmaEndpoint::Create()
    .SetDevice(device)
    .SetPath("/rdma/ep0")
    .SetType(doca::rdma::RdmaEndpointType::write)
    .SetBuffer(buffer)
    .Build();
```

**`IRdmaService`** is an abstract interface that users implement to process endpoint buffers. Services are registered on endpoints and called by the library at the appropriate point during request processing:

```cpp
class IRdmaService
{
public:
    virtual error Handle(RdmaBufferPtr buffer) = 0;
};
```

For Write endpoints, the service handler is called on the server side **after** the client writes data. For Read endpoints, the handler is called on the server side **before** the client reads data, allowing the server to populate the buffer.

### Endpoint Specification

Endpoints are defined in code (auto-generation from a YAML specification is planned). A sample configuration:

```yaml
endpoints:
  - path: /rdma/ep0
    type: write
    buffer:
      size: 4194304  # 4 MB

  - path: /rdma/ep0
    type: read
    buffer:
      size: 4194304  # 4 MB
```

### Sample Configuration

Both the server and client are configured via a YAML file:

```yaml
sample:
  log-level: 3  # Off:0 Trace:1 Debug:2 Info:3 Warning:4 Error:5 Critical:6

server:
  device: "mlx5_0"
  ipv4: "192.168.88.20"
  port: 12345

client:
  device: "mlx5_0"
```

### Execution Model

The library uses a hybrid communication model:

- **Control channel (TCP)** — an out-of-band TCP connection (via Asio) on port 41007 carries protocol messages: requests, responses (including memory descriptors), and acknowledgements.
- **Data channel (RDMA)** — actual data transfer happens over RDMA using RoCEv2 via the RDMA Connection Manager.

When a client requests an endpoint operation, the following protocol is executed:

**Write request flow:**

```
      server              client
        |                   |
        |  <-- TCP request (write) --
        |                   |
        | -- TCP response (descriptor) -->
        |                   |
        |  <-- RDMA write --
        |                   |
   user handler             |
  processes data            |
        |                   |
        |  <-- TCP acknowledge --
        |                   |
```

**Read request flow:**

```
      server              client
        |                   |
        |  <-- TCP request (read) --
        |                   |
   user handler             |
  populates data            |
        |                   |
        | -- TCP response (descriptor) -->
        |                   |
        | -- RDMA read -->  |
        |                   |
        |  <-- TCP acknowledge --
        |                   |
```

The `RdmaExecutor` is an internal component that manages the DOCA RDMA engine, progress engine, and a worker thread. It receives operation requests via a queue, allocates RDMA tasks, and polls for their completion. The `RdmaSession` classes (server and client variants) handle the TCP protocol using Asio coroutines.

### DOCA C Wrappers

The wrapper layer provides RAII classes that mirror DOCA C API modules. Each wrapper manages resource lifetime through smart pointers with custom deleters, covering `Device`, `MemoryMap`, `Buffer`, `BufferInventory`, `Context`, and `ProgressEngine`.

## Future Plans

1. Experiment with buffer sizes and run performance benchmarks
2. Support point-to-point connections over RoCEv1 with out-of-band connection descriptor exchange
3. Exchange descriptors at connection establishment rather than per request
4. Auto-generate client and server code from a YAML specification
5. Develop **DOCA GPUNetIO** wrappers for GPU-direct packet processing
