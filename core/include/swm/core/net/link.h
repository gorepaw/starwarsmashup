#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace swm::core::net {

// A peer's address: IPv4 address and port, both in host byte order.
struct Endpoint {
    uint32_t address = 0;
    uint16_t port = 0;

    friend auto operator<=>(const Endpoint&, const Endpoint&) = default;
    std::string to_string() const;
};

// Parses "a.b.c.d:port" (or "localhost:port"). Returns nullopt if malformed.
std::optional<Endpoint> parse_endpoint(const std::string& text);

struct Datagram {
    Endpoint from;
    std::vector<uint8_t> data;
};

// Unreliable, unordered datagrams, like UDP. The game protocol handles loss
// itself: inputs are sent redundantly, snapshots are latest-wins, and the
// handshake retries.
class Link {
public:
    virtual ~Link() = default;
    virtual void send(const Endpoint& to, std::span<const uint8_t> data) = 0;
    // Next datagram that has arrived, if any. Never blocks.
    virtual std::optional<Datagram> receive() = 0;
};

// In-memory network for tests and in-process play: every LoopbackLink on a
// hub can reach the others by endpoint, with instant delivery.
class LoopbackHub {
public:
    void deliver(const Endpoint& to, Datagram datagram);
    std::optional<Datagram> take(const Endpoint& at);

private:
    std::map<Endpoint, std::deque<Datagram>> queues_;
};

class LoopbackLink : public Link {
public:
    LoopbackLink(LoopbackHub& hub, Endpoint self) : hub_(hub), self_(self) {}
    void send(const Endpoint& to, std::span<const uint8_t> data) override;
    std::optional<Datagram> receive() override;

private:
    LoopbackHub& hub_;
    Endpoint self_;
};

// Simulated bad network on top of another link: delays datagrams in both
// directions and drops some. Round-trip time grows by twice `latency`.
class ImpairedLink : public Link {
public:
    struct Settings {
        double latency = 0;  // seconds, one way
        double jitter = 0;   // seconds, uniform extra delay 0..jitter
        double loss = 0;     // 0..1, per datagram and direction
        uint32_t seed = 1;
    };
    using Clock = std::function<double()>;

    ImpairedLink(Link& inner, Settings settings, Clock clock);
    void send(const Endpoint& to, std::span<const uint8_t> data) override;
    std::optional<Datagram> receive() override;

private:
    struct Pending {
        double due = 0;
        Endpoint peer;
        std::vector<uint8_t> data;
    };
    bool dropped();
    double delay();
    void flush_outgoing(double now);

    Link& inner_;
    Settings settings_;
    Clock clock_;
    std::mt19937 random_;
    std::deque<Pending> outgoing_;
    std::deque<Pending> incoming_;
};

} // namespace swm::core::net
