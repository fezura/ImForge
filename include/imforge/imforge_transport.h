#pragma once

#include "imforge_transport_base.h"
#include "imforge_transport_pipe.h"
#include "imforge_transport_shm.h"
#include "imforge_transport_tcp.h"

namespace imforge {

inline std::unique_ptr<Transport> CreateTransport(const char* url) {
    if (!url) return nullptr;
    if (std::strncmp(url, "tcp://", 6) == 0) return std::unique_ptr<Transport>(new TcpTransport());
    if (std::strncmp(url, "pipe://", 7) == 0) return std::unique_ptr<Transport>(new PipeTransport());
    if (std::strncmp(url, "shm://", 6) == 0) return std::unique_ptr<Transport>(new ShmTransport());
    return nullptr;
}

}  // namespace imforge
