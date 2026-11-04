#pragma once

#include <cstdint>

#include "swm/core/net/link.h"

namespace swm::core::net {

// Non-blocking IPv4 UDP socket. Binds to `port` on all interfaces (0 picks a
// free port). Throws std::runtime_error if the socket can't be opened.
class UdpLink : public Link {
public:
    explicit UdpLink(uint16_t port = 0);
    ~UdpLink() override;
    UdpLink(const UdpLink&) = delete;
    UdpLink& operator=(const UdpLink&) = delete;

    void send(const Endpoint& to, std::span<const uint8_t> data) override;
    std::optional<Datagram> receive() override;
    uint16_t port() const { return port_; }

private:
    uintptr_t socket_;
    uint16_t port_ = 0;
};

} // namespace swm::core::net
