// Protocol encoding, and a server with two predicting clients over a
// simulated lossy, laggy network.

#include <cmath>
#include <numbers>
#include <optional>

#include "swm/core/client.h"
#include "swm/core/net/bytes.h"
#include "swm/core/net/protocol.h"
#include "swm/core/server.h"
#include "test.h"

using namespace swm::core;
using namespace swm::core::net;
using swm::test::near;

namespace {

void test_protocol() {
    Snapshot snapshot;
    snapshot.server_tick = 1234;
    snapshot.last_input = 99;
    snapshot.entities.push_back({7, {{1.5, -2.25, 3e6}, {4, 0, -5}, 0.75f, false}});
    snapshot.entities.push_back({8, {}});
    const Message decoded = decode(encode(snapshot));
    const auto* s = std::get_if<Snapshot>(&decoded);
    CHECK(s != nullptr && s->server_tick == 1234 && s->last_input == 99 && s->entities.size() == 2);
    CHECK(s != nullptr && s->entities[0].id == 7 && s->entities[0].state == snapshot.entities[0].state);

    InputBatch batch;
    for (uint32_t i = 1; i <= 20; ++i) {
        batch.inputs.push_back({i, int8_t(i), -5, 0.5f, MoveInput::jump});
    }
    const Message inputs = decode(encode(batch));
    const auto* b = std::get_if<InputBatch>(&inputs);
    // Only the newest max_batched_inputs travel.
    CHECK(b != nullptr && b->inputs.size() == max_batched_inputs && b->inputs.front().sequence == 5 &&
          b->inputs.back() == batch.inputs.back());

    const Message hello = decode(encode(Hello{"trooper"}));
    CHECK(std::get_if<Hello>(&hello) != nullptr && std::get<Hello>(hello).name == "trooper");
    const Message welcome = decode(encode(Welcome{3, 30, 77}));
    CHECK(std::get<Welcome>(welcome).entity_id == 3 && std::get<Welcome>(welcome).server_tick == 77);
    CHECK(std::holds_alternative<Goodbye>(decode(encode(Goodbye{}))));

    // Malformed packets are rejected.
    const auto expect_error = [](std::vector<uint8_t> packet) {
        try {
            decode(packet);
            return false;
        } catch (const PacketError&) {
            return true;
        }
    };
    std::vector<uint8_t> packet = encode(snapshot);
    const std::vector<uint8_t> truncated(packet.begin(), packet.end() - 1);
    CHECK(expect_error(truncated));
    packet.push_back(0);
    CHECK(expect_error(packet));  // trailing bytes
    packet = encode(Goodbye{});
    packet[2] = uint8_t(protocol_version + 1);
    CHECK(expect_error(packet));  // another version
    const std::vector<uint8_t> garbage{1, 2, 3};
    CHECK(expect_error(garbage));

    CHECK(parse_endpoint("127.0.0.1:7777") == (Endpoint{0x7F000001, 7777}));
    CHECK(parse_endpoint("localhost:80") == (Endpoint{0x7F000001, 80}));
    CHECK(!parse_endpoint("127.0.0:7777") && !parse_endpoint("1.2.3.4") && !parse_endpoint("1.2.3.4:0") &&
          !parse_endpoint("1.2.3.400:5") && !parse_endpoint("1.2.3.4:5x"));
    CHECK((Endpoint{0x7F000001, 7777}.to_string() == "127.0.0.1:7777"));
}

Heightfield rolling_ground() {
    std::vector<float> heights;
    for (int row = 0; row < 201; ++row) {
        for (int column = 0; column < 201; ++column) {
            heights.push_back(float(4 * std::sin(column * 0.07) + 3 * std::cos(row * 0.05)));
        }
    }
    return Heightfield(201, 1.0, -100, -100, std::move(heights));  // game x and z: -100..100
}

double horizontal_distance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

// A server and two clients on one in-memory network. Client A has 50 ms of
// latency each way plus jitter and 5% loss; client B 80 ms and 10% loss.
struct Session {
    double now = 0;
    Heightfield ground = rolling_ground();
    World world{ground};
    LoopbackHub hub;
    Endpoint server_at{1, 1}, a_at{2, 1}, b_at{3, 1};
    LoopbackLink server_link{hub, server_at};
    LoopbackLink a_raw{hub, a_at};
    LoopbackLink b_raw{hub, b_at};
    ImpairedLink a_link{a_raw, {.latency = 0.05, .jitter = 0.01, .loss = 0.05, .seed = 11}, [this] { return now; }};
    ImpairedLink b_link{b_raw, {.latency = 0.08, .jitter = 0.0, .loss = 0.10, .seed = 22}, [this] { return now; }};
    GameServer server{server_link, world, ServerConfig{}};
    GameClient a{a_link, world, ClientConfig{.server = server_at, .name = "a"}};
    GameClient b{b_link, world, ClientConfig{.server = server_at, .name = "b"}};

    // Advances everyone in 120 Hz frames.
    template <typename Steer>
    void run(double seconds, Steer steer) {
        for (const double end = now + seconds; now < end; now += 1.0 / 120) {
            steer();
            server.update(now);
            a.update(now);
            b.update(now);
        }
    }
};

std::optional<GameClient::Remote> find_remote(const std::vector<GameClient::Remote>& remotes, uint32_t id) {
    for (const auto& remote : remotes) {
        if (remote.id == id) {
            return remote;
        }
    }
    return std::nullopt;
}

void test_session() {
    Session s;
    s.run(1.0, [] {});
    CHECK(s.a.status() == GameClient::Status::connected && s.b.status() == GameClient::Status::connected);
    CHECK(s.a.spawned() && s.b.spawned() && s.server.client_count() == 2);
    CHECK(s.a.entity_id() != s.b.entity_id());

    // A runs a wide circle, sprinting and jumping now and then. B chases what
    // it sees of A.
    double worst_view_error = 0;
    int frame = 0;
    s.run(8.0, [&] {
        ++frame;
        s.a.set_input(1, 0.3, s.now * 0.6, frame % 400 < 200, frame % 150 == 0);
        if (const auto seen = find_remote(s.b.remotes(), s.a.entity_id())) {
            const Vec3 to = seen->state.position - s.b.predicted().position;
            s.b.set_input(1, 0, std::atan2(-to.x, -to.z), false, false);
            // B sees A in the past: about the interpolation delay plus
            // latency behind, at up to sprint speed.
            const MoveState* truth = s.server.state_of(s.a.entity_id());
            worst_view_error = std::max(worst_view_error, horizontal_distance(seen->state.position, truth->position));
        }
    });
    CHECK(s.a.stats().ticks > 230 && s.b.stats().ticks > 230);
    // Prediction is exact: the server ran the same inputs through the same code.
    CHECK(s.a.stats().corrections == 0 && s.b.stats().corrections == 0);
    CHECK(worst_view_error > 0.1 && worst_view_error < 3.0);
    // Round trips: twice the one-way latency, plus up to a tick of server queuing.
    CHECK(s.a.stats().rtt > 0.09 && s.a.stats().rtt < 0.2);
    CHECK(s.b.stats().rtt > 0.15 && s.b.stats().rtt < 0.26);

    // Everyone stops; once things settle, all views agree exactly.
    s.run(1.5, [&] {
        s.a.set_input(0, 0, 0, false, false);
        s.b.set_input(0, 0, 0, false, false);
    });
    const MoveState* a_server = s.server.state_of(s.a.entity_id());
    const MoveState* b_server = s.server.state_of(s.b.entity_id());
    CHECK(a_server && b_server && s.a.predicted().position == a_server->position);
    CHECK(s.b.predicted().position == b_server->position);
    const auto a_seen_by_b = find_remote(s.b.remotes(), s.a.entity_id());
    const auto b_seen_by_a = find_remote(s.a.remotes(), s.b.entity_id());
    CHECK(a_seen_by_b && near(horizontal_distance(a_seen_by_b->state.position, a_server->position), 0));
    CHECK(b_seen_by_a && near(horizontal_distance(b_seen_by_a->state.position, b_server->position), 0));
    // B went after A, which circled at up to sprint speed.
    CHECK(horizontal_distance(b_server->position, {3, 0, 0}) > 5);
    CHECK(horizontal_distance(a_server->position, b_server->position) < 30);

    // The server moves A (a teleport): A's client is corrected and follows.
    MoveState teleported = *a_server;
    teleported.position = {50, s.ground.height_at(50, 50), 50};
    s.server.set_state(s.a.entity_id(), teleported);
    s.run(0.5, [] {});
    CHECK(s.a.stats().corrections >= 1 && s.a.stats().max_correction > 10);
    CHECK(s.a.predicted().position == teleported.position);
    // The visible jump is eased in, not instant...
    CHECK(horizontal_distance(s.a.presented().position, teleported.position) > 0.01);
    s.run(1.0, [] {});
    // ...and fades out.
    CHECK(horizontal_distance(s.a.presented().position, teleported.position) < 0.01);

    // B leaves cleanly: the server drops it at once and A stops seeing it.
    s.b.disconnect();
    s.run(0.5, [] {});
    CHECK(s.server.client_count() == 1 && !find_remote(s.a.remotes(), s.b.entity_id()));

    // A goes silent: the server times it out.
    for (const double end = s.now + 6; s.now < end; s.now += 1.0 / 120) {
        s.server.update(s.now);
    }
    CHECK(s.server.client_count() == 0);
    // The server goes silent: A gets the last delayed snapshots, then times out.
    for (const double end = s.now + 6; s.now < end; s.now += 1.0 / 120) {
        s.a.update(s.now);
    }
    CHECK(s.a.status() == GameClient::Status::timed_out);
}

void test_lost_welcome() {
    // Every packet to the client is lost at first, so its Hellos repeat. The
    // server answers each with a Welcome and spawns the player only once.
    double now = 0;
    std::vector<float> flat(4, 0.0f);
    const Heightfield ground(2, 10.0, 0, 0, flat);
    const World world(ground);
    LoopbackHub hub;
    LoopbackLink server_link(hub, {1, 1});
    LoopbackLink client_link(hub, {2, 1});
    GameServer server(server_link, world, ServerConfig{});
    GameClient client(client_link, world, ClientConfig{.server = {1, 1}});
    for (; now < 1; now += 0.01) {
        client.update(now);
        server.update(now);
        while (hub.take({2, 1})) {
        }
    }
    CHECK(client.status() == GameClient::Status::connecting && server.client_count() == 1);
    for (; now < 1.5; now += 0.01) {
        client.update(now);
        server.update(now);
    }
    CHECK(client.status() == GameClient::Status::connected && client.spawned() && server.client_count() == 1);
}

} // namespace

int main() {
    test_protocol();
    test_session();
    test_lost_welcome();
    return swm::test::finish("net_test");
}
