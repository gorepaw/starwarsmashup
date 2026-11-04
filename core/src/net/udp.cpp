#include "swm/core/net/udp.h"

#include <array>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
using native_socket = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using native_socket = int;
#endif

namespace swm::core::net {

namespace {

#ifdef _WIN32
constexpr uintptr_t invalid_socket = INVALID_SOCKET;

// Winsock needs starting once per process.
void start_sockets() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started) {
        throw std::runtime_error("WSAStartup failed");
    }
}

void close_socket(uintptr_t s) {
    closesocket(SOCKET(s));
}

bool set_non_blocking(uintptr_t s) {
    u_long on = 1;
    return ioctlsocket(SOCKET(s), FIONBIO, &on) == 0;
}
#else
constexpr uintptr_t invalid_socket = uintptr_t(-1);

void start_sockets() {}

void close_socket(uintptr_t s) {
    close(int(s));
}

bool set_non_blocking(uintptr_t s) {
    return fcntl(int(s), F_SETFL, fcntl(int(s), F_GETFL, 0) | O_NONBLOCK) == 0;
}
#endif

} // namespace

UdpLink::UdpLink(uint16_t port) {
    start_sockets();
    const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    socket_ = uintptr_t(s);
    if (socket_ == invalid_socket) {
        throw std::runtime_error("cannot open a UDP socket");
    }
#ifdef _WIN32
    // Don't let an ICMP "port unreachable" from a vanished peer fail later reads.
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(SOCKET(socket_), _WSAIOW(IOC_VENDOR, 12), &report, sizeof(report), nullptr, 0, &returned, nullptr,
             nullptr);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    socklen_t length = sizeof(address);
    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || !set_non_blocking(socket_) ||
        getsockname(s, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        close_socket(socket_);
        throw std::runtime_error("cannot bind UDP port " + std::to_string(port));
    }
    port_ = ntohs(address.sin_port);
}

UdpLink::~UdpLink() {
    close_socket(socket_);
}

void UdpLink::send(const Endpoint& to, std::span<const uint8_t> data) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(to.address);
    address.sin_port = htons(to.port);
    // Unreliable by contract: a failed send is a lost datagram.
    sendto(native_socket(socket_), reinterpret_cast<const char*>(data.data()), int(data.size()), 0,
           reinterpret_cast<sockaddr*>(&address), sizeof(address));
}

std::optional<Datagram> UdpLink::receive() {
    std::array<uint8_t, 65536> buffer;  // the largest UDP payload
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    for (;;) {
        const auto received = recvfrom(native_socket(socket_), reinterpret_cast<char*>(buffer.data()),
                                       int(buffer.size()), 0, reinterpret_cast<sockaddr*>(&address), &length);
        if (received >= 0) {
            return Datagram{{ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)},
                            {buffer.begin(), buffer.begin() + received}};
        }
#ifdef _WIN32
        // An oversized datagram is dropped; keep reading.
        if (WSAGetLastError() == WSAEMSGSIZE) {
            continue;
        }
#endif
        return std::nullopt;  // nothing waiting (or an error: treated as no data)
    }
}

} // namespace swm::core::net
