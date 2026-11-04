#include "swm/core/net/link.h"

#include <algorithm>
#include <charconv>
#include <format>

#include "swm/core/net/bytes.h"

namespace swm::core::net {

void ByteWriter::put_string(const std::string& value) {
    const size_t length = std::min<size_t>(value.size(), 255);
    put(uint8_t(length));
    data_.insert(data_.end(), value.begin(), value.begin() + std::ptrdiff_t(length));
}

std::string ByteReader::get_string() {
    const size_t length = get<uint8_t>();
    need(length);
    std::string value(reinterpret_cast<const char*>(data_.data() + at_), length);
    at_ += length;
    return value;
}

void ByteReader::need(size_t bytes) const {
    if (data_.size() - at_ < bytes) {
        throw PacketError("packet truncated");
    }
}

std::string Endpoint::to_string() const {
    return std::format("{}.{}.{}.{}:{}", address >> 24, (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF,
                       port);
}

std::optional<Endpoint> parse_endpoint(const std::string& text) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::string host = text.substr(0, colon);
    if (host == "localhost") {
        host = "127.0.0.1";
    }
    Endpoint endpoint;
    const char* p = host.data();
    const char* end = host.data() + host.size();
    for (int i = 0; i < 4; ++i) {
        unsigned octet = 0;
        const auto [next, error] = std::from_chars(p, end, octet);
        if (error != std::errc() || octet > 255 || (i < 3 && (next == end || *next != '.'))) {
            return std::nullopt;
        }
        endpoint.address = (endpoint.address << 8) | octet;
        p = i < 3 ? next + 1 : next;
    }
    unsigned port = 0;
    const char* port_begin = text.data() + colon + 1;
    const char* port_end = text.data() + text.size();
    const auto [next, error] = std::from_chars(port_begin, port_end, port);
    if (p != end || error != std::errc() || next != port_end || port == 0 || port > 65535) {
        return std::nullopt;
    }
    endpoint.port = uint16_t(port);
    return endpoint;
}

void LoopbackHub::deliver(const Endpoint& to, Datagram datagram) {
    queues_[to].push_back(std::move(datagram));
}

std::optional<Datagram> LoopbackHub::take(const Endpoint& at) {
    auto it = queues_.find(at);
    if (it == queues_.end() || it->second.empty()) {
        return std::nullopt;
    }
    Datagram datagram = std::move(it->second.front());
    it->second.pop_front();
    return datagram;
}

void LoopbackLink::send(const Endpoint& to, std::span<const uint8_t> data) {
    hub_.deliver(to, {self_, {data.begin(), data.end()}});
}

std::optional<Datagram> LoopbackLink::receive() {
    return hub_.take(self_);
}

ImpairedLink::ImpairedLink(Link& inner, Settings settings, Clock clock)
    : inner_(inner), settings_(settings), clock_(std::move(clock)), random_(settings.seed) {}

bool ImpairedLink::dropped() {
    return settings_.loss > 0 && std::uniform_real_distribution<double>(0, 1)(random_) < settings_.loss;
}

double ImpairedLink::delay() {
    const double jitter =
        settings_.jitter > 0 ? std::uniform_real_distribution<double>(0, settings_.jitter)(random_) : 0;
    return settings_.latency + jitter;
}

namespace {

// Removes and returns the earliest datagram that's due. Jitter can reorder.
std::optional<std::pair<Endpoint, std::vector<uint8_t>>> take_due(auto& queue, double now) {
    auto earliest = std::min_element(queue.begin(), queue.end(),
                                     [](const auto& a, const auto& b) { return a.due < b.due; });
    if (earliest == queue.end() || earliest->due > now) {
        return std::nullopt;
    }
    auto result = std::make_pair(earliest->peer, std::move(earliest->data));
    queue.erase(earliest);
    return result;
}

} // namespace

void ImpairedLink::flush_outgoing(double now) {
    while (auto due = take_due(outgoing_, now)) {
        inner_.send(due->first, due->second);
    }
}

void ImpairedLink::send(const Endpoint& to, std::span<const uint8_t> data) {
    const double now = clock_();
    if (!dropped()) {
        outgoing_.push_back({now + delay(), to, {data.begin(), data.end()}});
    }
    flush_outgoing(now);
}

std::optional<Datagram> ImpairedLink::receive() {
    const double now = clock_();
    flush_outgoing(now);
    while (auto datagram = inner_.receive()) {
        if (!dropped()) {
            incoming_.push_back({now + delay(), datagram->from, std::move(datagram->data)});
        }
    }
    if (auto due = take_due(incoming_, now)) {
        return Datagram{due->first, std::move(due->second)};
    }
    return std::nullopt;
}

} // namespace swm::core::net
