#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace swm::launcher {

// A channel's release manifest (docs/DISTRIBUTION.md, Updates):
//   channels/<channel>/latest.json      this, signed by latest.json.sig (Ed25519, hex)
//   blobs/<sha256[0:2]>/<sha256>        each file's contents
struct ManifestFile {
    std::string path;  // relative to the install dir, forward slashes
    std::string sha256;
    uint64_t size = 0;
};

struct ReleaseManifest {
    int format = 0;
    std::string channel;
    std::string version;
    int min_launcher = 0;  // launchers older than this update themselves first
    std::vector<ManifestFile> files;
};

// The launcher's own build number; bump when a manifest needs a newer one.
inline constexpr int launcher_build = 1;

// Throws std::runtime_error.
ReleaseManifest parse_manifest(const std::string& json);
std::string manifest_json(const ReleaseManifest& manifest);

using PublicKey = std::array<uint8_t, 32>;
using SecretKey = std::array<uint8_t, 32>;  // the Ed25519 seed
using Signature = std::array<uint8_t, 64>;

bool verify_signature(const std::string& message, const Signature& signature, const PublicKey& key);
Signature sign(const std::string& message, const SecretKey& secret);
PublicKey public_key_of(const SecretKey& secret);
SecretKey random_secret_key();
std::string to_hex(const uint8_t* data, size_t size);
// Throws on malformed hex or the wrong length.
std::vector<uint8_t> from_hex(const std::string& hex);

std::string sha256_hex(const std::string& data);
std::string sha256_file(const std::filesystem::path& file);  // empty if unreadable

struct Remote {
    std::string base_url;  // e.g. https://pub-....r2.dev or https://swm-private.<account>.workers.dev
    std::string channel;   // stable, portable
    std::string token;     // bearer token for the private channel, or empty
    std::string manifest_url() const { return base_url + "/channels/" + channel + "/latest.json"; }
    std::string blob_url(const std::string& sha256) const {
        return base_url + "/blobs/" + sha256.substr(0, 2) + "/" + sha256;
    }
};

struct HttpResponse {
    int status = 0;  // 0: no response (error says why)
    std::string body;
    std::string error;
};
HttpResponse http_get(const std::string& url, const std::string& bearer = {});
// Streams a URL to `dest`, calling on_bytes with each chunk's size. Returns an
// error message, or empty.
std::string http_download(const std::string& url, const std::string& bearer, const std::filesystem::path& dest,
                          const std::function<void(uint64_t)>& on_bytes = {},
                          const std::atomic<bool>* cancel = nullptr);

// The newest signed manifest, verified, and its exact bytes (kept as the
// installed record). Throws std::runtime_error with a reason for the player.
struct FetchedManifest {
    ReleaseManifest manifest;
    std::string json;
};
FetchedManifest fetch_manifest(const Remote& remote, const PublicKey& key);

struct UpdatePlan {
    std::vector<ManifestFile> download;
    std::vector<std::string> remove;  // in the installed manifest, not the new one
    uint64_t bytes = 0;
    bool replaces_launcher = false;
    bool empty() const { return download.empty() && remove.empty(); }
};

// What `next` needs over what's installed (state/installed.json's manifest).
// A file counts as current if the installed record says so and its size
// matches, or if its contents hash to the new value.
UpdatePlan plan_update(const ReleaseManifest& next, const std::optional<ReleaseManifest>& installed,
                       const std::filesystem::path& install_dir, const std::string& launcher_path);

struct UpdateProgress {
    std::atomic<uint64_t> bytes{0};
    std::atomic<size_t> files{0};
};

// Downloads the plan's files to <install>/state/staging (verifying each), then
// moves them into place, removes the plan's leftovers and records `json` as
// installed. The running launcher is renamed to launcher.old.exe to be
// replaced. Returns an error, or empty.
std::string apply_update(const Remote& remote, const UpdatePlan& plan, const std::string& json,
                         const std::filesystem::path& install_dir, const std::filesystem::path& launcher_exe,
                         UpdateProgress& progress, const std::atomic<bool>* cancel = nullptr, int threads = 8);

std::optional<ReleaseManifest> installed_manifest(const std::filesystem::path& install_dir);

} // namespace swm::launcher
