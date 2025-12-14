# doca-cpp

C++ Adapter For NVIDIA DOCA Framework 

TODO: Rename the module to something that doesn’t mention DOCA (acod maybe :))

## Library Overview

NVIDIA DOCA is a large framework for network offloading. The framework consists of libraries. This repository aims to provide C++ OOP wrappers around the DOCA Common, DOCA RDMA, DOCA Flow, and DOCA Comch libraries (which expose APIs in C) so their functionality can be used easily from C++.

Currently, wrappers for DOCA Common and DOCA RDMA are under development.

## Prerequisities 

Before building library and samples, DOCA compatible Linux operating system and kernel are required. Please refer to [NVIDIA DOCA Documentation](https://docs.nvidia.com/doca/sdk/doca-framework/index.html) for more information about supported OS and kernel versions.

To build library and samples, make sure you have:

* CMake, Ninja, vcpkg
* DOCA libraries
  - DOCA Common
  - DOCA RDMA

DOCA libraries can be installed with DOCA installation profile called ```doca-networking```. Please refer to [NVIDIA DOCA Documentation](https://docs.nvidia.com/doca/sdk/doca-framework/index.html) for more information about this profile.

Library was tested only for ```amd64``` architecture with Ubuntu 24.04 and Linux Kernel 6.8.0-31 with two ConnectX-6 DX SmartNIC devices. DOCA versions from 3.0.0 to 3.2.0 were used to run RDMA applications.

## Build Library

To build library, run following commands:

```bash
cmake -S . -B build --preset amd64-linux-debug
cmake --build build --target doca-cpp
```

To build samples, run following commands:

```bash
cmake --build build --target <sample_name>
```

## Library Development Notes

### DOCA Common Overview

DOCA Common works with the following entities:
- Device:
   * `Device`
- Memory:
   * `Buffer`
   * `BufferInventory`
   * `MemoryMap`
- Runtime:
   * `Context`
   * `ProgressEngine`
   * `Task`

`Device` represents a network device like a ConnectX-6 DX SmartNIC.  
`Buffer` is an abstraction over a memory region and is used for memory operations via the DOCA API. `BufferInventory` is a container for `Buffer`. `MemoryMap` is a memory mapping used to grant device access to memory.
Most operations in DOCA are performed asynchronously — they invoke user-supplied callbacks when completed. To trigger the callbacks, you must poll the `ProgressEngine`. Contexts are used to attach `Task`s to the execution environment and also connect them to the `ProgressEngine`.

### DOCA RDMA Overview

DOCA RDMA works with the following entities:
- Runtime:
   * `Rdma`
   * `RdmaContext`
   * `RdmaTask`
   * `RdmaConnection`

`Rdma` is the structure that contains and manages the entire RDMA logic. `RdmaContext` attaches to the `ProgressEngine`. `RdmaTask` represents an RDMA operation (`RdmaTaskSend`, `RdmaTaskReceive`, and others). `RdmaConnection` manages the connection between peers.

DOCA RDMA supports many RDMA operations; the author focuses on:
- Send
- Receive
- Write
- Read

DOCA RDMA allows you to set up either client-server connections or point-to-point connections.  
- For client-server mode, RDMA CM (Connection Manager) is used. The server listens on an OS port and accepts connection requests; the client connects to a server address; this typically uses RoCEv2.  
- For point-to-point mode, out-of-band communication is used whereby connection information is exported and transferred through another channel (e.g., classic socket or DOCA Comch); this uses RoCEv1.

Before or after establishing the connection, client and server may create RDMA tasks. Send and Receive tasks are mirrored: when a Send task is submitted on one side, there should be a matching Receive task on the other side. Read and Write operations do not require a corresponding task on the remote peer.

In DOCA terms, two RDMA CM peers are Requester and Responder. The table below illustrates direction of data on the wire:

| Task          | Data Direction                               |
| :---          | :---                                         |
| Send/Receive  | Side that submitted Send ⟶ Side that submitted Receive |
| Write         | Write Requester ⟶ Write Responder            |
| Read          | Read Responder ⟶ Read Requester              |

To perform a Write or Read, the Requester must obtain a descriptor from the Responder for the remote memory to read from or write to. The descriptor is passed via a Send/Receive operation or any of out-of-band communication type (like socket).

Example: To perform a Write, the Requester creates a Receive task and waits to get a descriptor from the Responder. The Responder creates a Send task and sends the descriptor. After receiving the descriptor, the Requester creates a `MemoryMap` from the descriptor. The Write is issued and the Responder's device places the payload into its memory.

## Architecture Overview

Library doca-cpp has three layers:
* High-level instances for API
* Internal instances for library logic
* Wrappers above DOCA API

### High-Level API

Library provides two high-level instances: ```RdmaServer``` and ```RdmaClient```. There is custom specification of how server and client will exchange data via RDMA. The structure of specification is presented in YAML format and looks like this:

```yaml
version: "0.0.1"

endpoints:
  - path: /rdma/ep0
    type: receive
    buffer:
      size: 4096 # bytes - 4KB

  - path: /rdma/ep1
    type: write
    buffer:
      size: 4194304 # bytes - 4MB

  - path: /rdma/ep0
    type: send
    buffer:
      size: 4096 # bytes - 4KB

  - path: /rdma/ep1
    type: read
    buffer:
      size: 4194304 # bytes - 4MB
```

Another instance is ```RdmaEndpoint``` which is presented in specification above. Endpoint is just buffer with its path that is like buffer's unique resource identifier (URI). Specification may have two same paths since buffer may be requested for writing or reading. Type represents RDMA operation which will be processed after request.

Instance ```RdmaBuffer``` is also high-level instance that allows user to register memory to it. When user registers memory and invokes RDMA operation with this buffer, library takes ownership of this memory, yet imlicitly.

In the future code for server and client will be auto generated by specification file, but now user must provide endpoints creation and its registration in server. To process buffers by requests user can implement simple interface:

```C++
class RdmaServiceInterface
{
public:
    virtual error Handle(RdmaBufferPtr buffer) = 0;
};
```

Server code after that will look like this:

```C++
int main()
{
    // Open InfiniBand device
    auto device = doca::OpenIbDevice("mlx5_0");

    // Create RDMA server
    auto server = doca::rdma::RdmaServer::Create()
            .SetDevice(device)
            .SetListenPort(12345)
            .Build();

    const auto cfg0 = endpoints::Config{
        .path = "/rdma/ep0",
        .size = 4096,
        .type = doca::rdma::RdmaEndpointType::send,
    };

    const auto cfg1 = endpoints::Config{
        .path = "/rdma/ep0",
        .size = 4096,
        .type = doca::rdma::RdmaEndpointType::receive,
    };

    const auto cfg2 = endpoints::Config{
        .path = "/rdma/ep1",
        .size = 4194304,
        .type = doca::rdma::RdmaEndpointType::write,
    };

    const auto cfg3 = endpoints::Config{
        .path = "/rdma/ep1",
        .size = 4194304,
        .type = doca::rdma::RdmaEndpointType::read,
    };

    const auto configs = std::vector<endpoints::Config>({ cfg0, cfg1, cfg2, cfg3 });

    auto endpoints = endpoints::CreateEndpoints(device, configs);

    // Attach User's handlers to endpoints

    auto userHandler = std::make_shared<UserService>();
    for (auto & endpoint : endpoints) {
        endpoint->RegisterService(userHandler);
    }

    // Register endpoints to server
    server->RegisterEndpoints(endpoints);

    // Start port listening and RDMA requests handling
    server->Serve();

    return 0;
}
```

Client code example will be presented soon.

### Execution model

To run RDMA operations, server submits Receive task and waits for any of the clients requests. After receiving it, server parses payload data to fetch endpoint information: path and operation type. Then it processes RDMA operation.

If operation type is Send or Write, server calls user's handler after completion. If it's Receive or Read, then server calls user's handler before submitting it.

Mirrored operations are processed simply with Receive or Send task submittion and require client to submit opposite task on its side.
Operations Write and Read are processed with more complex way. First, server submits Send task to send endpoint's memory descriptor to client. After that client submits Write or Read task. Since there is synchronization issues in DOCA API, library synchronize this operation by setting additional tasks in both client and server: when client will finish Write or Read operation, it submits Send task to send empty message as acknowledge; server submits Receive task accordingly.  

Example of Write request processing:

```
      server              client
        |                   |
     receive                |
        |                   |
        | <--request----- send
        |                   |
        |                receive
        |                   |
      send --descriptor---> |
        |                   |
        |  <-----data---- write
        |                   |
   user handler             |
  processes data            |
        |                   |
     receive                |
        |                   |
        |  <-----ack----  send
        |                   |
        |                   |
```

There may be multiple connections issues in this design, but author will troubleshoot it later.

All RDMA tasks are handled by executor, which is instance that creates DOCA RDMA engine and contexts, then launches one thread that is waiting for incoming tasks requests by server. There is asynchronous mechanism for these tasks, but yet server waits for completion right after submitting. Maybe in future there will be way to use asynchronous processing, for example when there will be batch operations support.

Instances like ```RdmaExecutor``` is related to library's internal logic and should not be used by customer.

### DOCA API Wrappers

DOCA API presents C-style functions that is very similar to C++ class methods since their module name is in its signature. So library wrappers just adds classes with similar methods and handles RAII strategy. There are objects that created and should be destroyed and objects that are just references to DOCA constant structures. 

## Future Plans

1. Provide client code
2. Launch and test one client and server
3. Experiment with buffers sizes, make performance test
4. Handle point-to-point operations above RoCEv1
5. Provide descriptors exchange on connection established event, not per request
6. Provide multiple connections support
7. Add code generation for client and server from YAML