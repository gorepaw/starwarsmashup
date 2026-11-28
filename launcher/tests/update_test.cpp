// Update manifests, signatures and planning (no network).

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "update.h"

namespace fs = std::filesystem;
using namespace swm::launcher;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

template <size_t N>
std::array<uint8_t, N> bytes(const std::string& hex) {
    const auto v = from_hex(hex);
    std::array<uint8_t, N> out{};
    std::copy(v.begin(), v.end(), out.begin());
    return out;
}

// RFC 8032, 7.1, TEST 1 and TEST 2: our signatures are standard Ed25519.
void test_rfc8032_vectors() {
    const auto secret = bytes<32>("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    const auto pub = bytes<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const auto sig = bytes<64>("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                               "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    CHECK(public_key_of(secret) == pub);
    CHECK(sign("", secret) == sig);
    CHECK(verify_signature("", sig, pub));

    const auto secret2 = bytes<32>("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
    const auto pub2 = bytes<32>("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
    const auto sig2 = bytes<64>("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                                "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
    CHECK(public_key_of(secret2) == pub2);
    CHECK(sign(std::string(1, '\x72'), secret2) == sig2);
}

void test_sign_round_trip() {
    const SecretKey k = random_secret_key();
    const std::string msg = R"({"format":1})";
    const Signature s = sign(msg, k);
    CHECK(verify_signature(msg, s, public_key_of(k)));
    CHECK(!verify_signature(msg + " ", s, public_key_of(k)));
    Signature bad = s;
    bad[5] ^= 1;
    CHECK(!verify_signature(msg, bad, public_key_of(k)));
    CHECK(!verify_signature(msg, s, public_key_of(random_secret_key())));
}

void test_manifest_paths_stay_inside() {
    const std::string sha(64, 'a');
    const auto parses = [&](const std::string& path) {
        try {
            parse_manifest(R"({"format":1,"channel":"stable","version":"1","files":[{"path":")" + path +
                           R"(","sha256":")" + sha + R"(","size":1}]})");
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    CHECK(parses("game/project.godot"));
    CHECK(!parses("../evil.exe"));
    CHECK(!parses("game/../../evil.exe"));
    CHECK(!parses("C:/Windows/evil.exe"));
    CHECK(!parses("/evil.exe"));
    CHECK(!parses(""));
}

void test_manifest_round_trip() {
    ReleaseManifest m;
    m.format = 1;
    m.channel = "portable";
    m.version = "2026.11.28";
    m.min_launcher = 1;
    m.files = {{"launcher.exe", std::string(64, 'b'), 10}, {"game/a b.txt", std::string(64, 'c'), 0}};
    const ReleaseManifest back = parse_manifest(manifest_json(m));
    CHECK(back.channel == "portable" && back.version == "2026.11.28" && back.min_launcher == 1);
    CHECK(back.files.size() == 2 && back.files[1].path == "game/a b.txt" && back.files[0].size == 10);
}

void test_plan() {
    const fs::path dir = fs::temp_directory_path() / "swm_update_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "game");
    const auto write = [&](const std::string& rel, const std::string& text) {
        std::ofstream(dir / rel, std::ios::binary) << text;
    };
    write("game/same.txt", "same");
    write("game/changed.txt", "old");
    write("game/gone.txt", "bye");
    write("launcher.exe", "v1");

    ReleaseManifest installed;
    installed.format = 1;
    installed.files = {{"game/same.txt", sha256_hex("same"), 4},
                       {"game/changed.txt", sha256_hex("old"), 3},
                       {"game/gone.txt", sha256_hex("bye"), 3},
                       {"launcher.exe", sha256_hex("v1"), 2}};
    ReleaseManifest next;
    next.format = 1;
    next.files = {{"game/same.txt", sha256_hex("same"), 4},
                  {"game/changed.txt", sha256_hex("new!"), 4},
                  {"game/added.txt", sha256_hex("add"), 3},
                  {"launcher.exe", sha256_hex("v2"), 2}};
    UpdatePlan plan = plan_update(next, installed, dir, "launcher.exe");
    CHECK(plan.download.size() == 3);  // changed, added, launcher
    CHECK(plan.remove == std::vector<std::string>{"game/gone.txt"});
    CHECK(plan.replaces_launcher);
    CHECK(plan.bytes == 9);

    // No installed record: files already right (by hash) aren't downloaded.
    plan = plan_update(installed, std::nullopt, dir, "launcher.exe");
    CHECK(plan.empty());
    // A file edited behind our back, same size, recorded as current: trusted
    // (the record is ours); a different size is caught.
    write("game/same.txt", "SAME");
    CHECK(plan_update(installed, installed, dir, "launcher.exe").empty());
    write("game/same.txt", "same, longer");
    CHECK(plan_update(installed, installed, dir, "launcher.exe").download.size() == 1);
    fs::remove_all(dir);
}

} // namespace

int main() {
    try {
        test_rfc8032_vectors();
        test_sign_round_trip();
        test_manifest_paths_stay_inside();
        test_manifest_round_trip();
        test_plan();
    } catch (const std::exception& e) {
        std::printf("FAIL: exception: %s\n", e.what());
        ++failures;
    }
    if (failures == 0) {
        std::printf("update_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
