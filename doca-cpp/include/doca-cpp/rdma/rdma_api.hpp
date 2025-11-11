#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace internal
{
class RdmaEngine;

class RdmaConnectionManager;

}  // namespace internal

namespace api
{

class RdmaServer;

class RdmaClient;

class RdmaTask;

class RdmaTaskPool;

class RdmaBuffer;

class RdmaMode;

class RdmaConnectionType;

}  // namespace api

using namespace api;

constexpr size_t userBuffersCount = 4;

std::string serverAddress = "127.0.0.1:50993";

namespace doca
{
class DevicePtr;
}

doca::DevicePtr OpenDevice()
{
    //...
    return doca::DevicePtr;
}

void exampleServerApi()
{
    auto device = OpenDevice();

    constexpr size_t userBufferSize = 4 * 1024 * 1024 * sizeof(uint8_t);  // 4 MB
    RdmaTaskPool * pool;
    for (int i = 0; i < userBuffersCount; i++) {
        auto userBuffer = std::make_shared<std::vector<std::uint8_t>>(userBufferSize);

        RdmaBuffer * buffer;
        buffer->RegisterMemoryRange(userBuffer->data(), userBuffer->size());

        RdmaTask * sendTask;
        sendTask->AssociateBuffer(buffer);

        pool->AddTask(sendTask);
    }

    RdmaServer server = RdmaServer::Builder()
                            .WithDevice(device)
                            .WithMode(RdmaMode::Streaming)
                            .WithConnectionType(RdmaConnectionType::ipv4)
                            .WithAddress(serverAddress)
                            .Build();

    server.RegisterTaskPool(pool);
    server.Serve();

    auto completedBuffer = server.GetPool().GetTask(taskId).GetCompletedBuffer();
}

void exampleClientApi()
{
    auto device = OpenDevice();

    constexpr size_t userBufferSize = 4 * 1024 * 1024 * sizeof(uint8_t);  // 4 MB
    RdmaTaskPool * pool;
    for (int i = 0; i < userBuffersCount; i++) {
        auto userBuffer = std::make_shared<std::vector<std::uint8_t>>(userBufferSize);

        RdmaBuffer * buffer;
        buffer->RegisterMemoryRange(userBuffer->data(), userBuffer->size());

        RdmaTask * receiveTask;
        receiveTask->AssociateBuffer(buffer);

        pool->AddTask(receiveTask);
    }

    RdmaClient client = RdmaClient::Builder()
                            .WithDevice(device)
                            .WithMode(RdmaMode::Streaming)
                            .WithConnectionType(RdmaConnectionType::ipv4)
                            .Build();

    client.RegisterTaskPool(pool);
    client.Start();

    auto completedBuffer = client.GetPool().GetTask(taskId).GetCompletedBuffer();
    auto isOwnerUser = completedBuffer.OwnershipReserved();

    // client.Stop();
    // client.GetPool().FreeTasks();
}

/*
RdmaServer
RdmaTaskPool           RdmaConnectionManager
RdmaTask-RdmaBuffer
RdmaTask-RdmaBuffer
RdmaTask-RdmaBuffer
                RdmaEngine


RdmaClient
RdmaTaskPool           RdmaConnectionManager
RdmaTask-RdmaBuffer
RdmaTask-RdmaBuffer
RdmaTask-RdmaBuffer
                RdmaEngine
*/