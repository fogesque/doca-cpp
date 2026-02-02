
#include "service.hpp"

error user::UserWriteService::Handle(doca::rdma::RdmaBufferPtr buffer)
{
    if (this->writePattern == 0xFF) {
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

error user::UserReadService::Handle(doca::rdma::RdmaBufferPtr buffer)
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
