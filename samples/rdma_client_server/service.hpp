#pragma once

#include <print>

#include "doca-cpp/rdma/rdma_endpoint.hpp"

class UserWriteService : public doca::rdma::RdmaServiceInterface
{
public:
    virtual error Handle(doca::rdma::RdmaBufferPtr buffer) override
    {
        if (this->writePattern = 0xFF) {
            this->writePattern = 0x11;
        }

        auto [memrange, err] = buffer->GetMemoryRange();
        std::ignore = err;  // Unlikely in this sample

        auto & memory = *memrange;
        for (int i = 0; i < memory.size(); i++) {
            memory[i] = this->writePattern;
        }

        std::println("[User Service] User write service's handler called; filled buffer with pattern: {:#x}",
                     this->writePattern);

        this->writePattern += 0x11;
        return nullptr;
    }

private:
    uint8_t writePattern = 0x11;
    // This class is User's entry point to their code and RDMA operation results processings
};

class UserReadService : public doca::rdma::RdmaServiceInterface
{
public:
    virtual error Handle(doca::rdma::RdmaBufferPtr buffer) override
    {
        auto [memrange, err] = buffer->GetMemoryRange();
        std::ignore = err;  // Unlikely in this sample

        const auto & memory = *memrange;

        const size_t bytesToPrint = 10;

        std::ostringstream outStr{};
        outStr << std::hex << std::uppercase << std::setfill('0');

        // Head
        for (int i = 0; i < bytesToPrint && i < memory.size(); i++) {
            outStr << std::setw(2) << static_cast<int>(memory[i]) << " ";
        }
        outStr << "... ";
        // Tail
        for (int i = memory.size() - bytesToPrint; i < memory.size() && i >= 0; i++) {
            outStr << std::setw(2) << static_cast<int>(memory[i]) << " ";
        }

        std::println("[User Service] User read service's handler called; buffer data: {}", outStr.str());
        return nullptr;
    }

private:
    // This class is User's entry point to their code and RDMA operation results processings
};