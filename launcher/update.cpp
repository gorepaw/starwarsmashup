#include "update.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

extern "C" {
#include <monocypher-ed25519.h>
#include <monocypher.h>
}

#include "extract/process.h"

namespace fs = std::filesystem;
using swbfo::extract::narrow;
using swbfo::extract::path_utf8;
using swbfo::extract::utf8_path;
using swbfo::extract::widen;

namespace swbfo::launcher {

ReleaseManifest parse_manifest(const std::string& text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("manifest: ") + e.what());
    }
    ReleaseManifest m;
    m.format = j.value("format", 0);
    if (m.format != 1) {
        throw std::runtime_error("manifest: format " + std::to_string(m.format) + " not supported");
    }
    m.channel = j.value("channel", "");
    m.version = j.value("version", "");
    m.min_launcher = j.value("min_launcher", 0);
    std::set<std::string> seen;
    for (const auto& f : j.at("files")) {
        ManifestFile file{f.at("path").get<std::string>(), f.at("sha256").get<std::string>(), f.at("size").get<uint64_t>()};
        // Paths stay inside the install dir.
        const fs::path p = utf8_path(file.path);
        bool bad = file.path.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory() ||
                   file.sha256.size() != 64;
        for (const auto& part : p) {
            bad = bad || part == "..";
        }
        if (bad || !seen.insert(file.path).second) {
            throw std::runtime_error("manifest: bad file entry " + file.path);
        }
        m.files.push_back(std::move(file));
    }
    return m;
}

std::string manifest_json(const ReleaseManifest& m) {
    nlohmann::ordered_json j;
    j["format"] = m.format;
    j["channel"] = m.channel;
    j["version"] = m.version;
    j["min_launcher"] = m.min_launcher;
    j["files"] = nlohmann::ordered_json::array();
    for (const auto& f : m.files) {
        j["files"].push_back({{"path", f.path}, {"sha256", f.sha256}, {"size", f.size}});
    }
    return j.dump(1) + "\n";
}

bool verify_signature(const std::string& message, const Signature& signature, const PublicKey& key) {
    return crypto_ed25519_check(signature.data(), key.data(), reinterpret_cast<const uint8_t*>(message.data()),
                                message.size()) == 0;
}

namespace {

std::array<uint8_t, 64> expanded(const SecretKey& secret, PublicKey& pub) {
    std::array<uint8_t, 64> sk{};
    SecretKey seed = secret;  // key_pair wipes the seed
    crypto_ed25519_key_pair(sk.data(), pub.data(), seed.data());
    return sk;
}

} // namespace

Signature sign(const std::string& message, const SecretKey& secret) {
    PublicKey pub{};
    std::array<uint8_t, 64> sk = expanded(secret, pub);
    Signature sig{};
    crypto_ed25519_sign(sig.data(), sk.data(), reinterpret_cast<const uint8_t*>(message.data()), message.size());
    crypto_wipe(sk.data(), sk.size());
    return sig;
}

PublicKey public_key_of(const SecretKey& secret) {
    PublicKey pub{};
    std::array<uint8_t, 64> sk = expanded(secret, pub);
    crypto_wipe(sk.data(), sk.size());
    return pub;
}

SecretKey random_secret_key() {
    SecretKey k{};
    if (BCryptGenRandom(nullptr, k.data(), ULONG(k.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw std::runtime_error("no random numbers from the system");
    }
    return k;
}

std::string to_hex(const uint8_t* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

std::vector<uint8_t> from_hex(const std::string& hex_in) {
    std::string hex;
    for (const char c : hex_in) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            hex += c;
        }
    }
    if (hex.size() % 2) {
        throw std::runtime_error("bad hex");
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("bad hex");
    };
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = uint8_t(nibble(hex[2 * i]) << 4 | nibble(hex[2 * i + 1]));
    }
    return out;
}

namespace {

struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Sha256() {
        BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void update(const void* data, size_t size) {
        BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(data)), ULONG(size), 0);
    }
    std::string hex() {
        uint8_t digest[32];
        BCryptFinishHash(hash, digest, 32, 0);
        return to_hex(digest, 32);
    }
};

} // namespace

std::string sha256_hex(const std::string& data) {
    Sha256 h;
    h.update(data.data(), data.size());
    return h.hex();
}

std::string sha256_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    Sha256 h;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), std::streamsize(buf.size()));
        if (in.gcount() > 0) {
            h.update(buf.data(), size_t(in.gcount()));
        }
    }
    return h.hex();
}

namespace {

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

std::string last_error(const char* what) {
    return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

// GET `url`; for each chunk of the body, on_chunk. Returns status or 0 + error.
int http_request(const std::string& url, const std::string& bearer,
                 const std::function<bool(const char*, size_t)>& on_chunk, std::string& error,
                 const std::atomic<bool>* cancel) {
    const std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        error = "bad URL " + url;
        return 0;
    }
    Handle session(WinHttpOpen(L"SwbfoLauncher/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        error = last_error("WinHttpOpen");
        return 0;
    }
    Handle connect(WinHttpConnect(session.h, host, parts.nPort, 0));
    if (!connect) {
        error = "could not connect to " + narrow(host) + " (error " + std::to_string(GetLastError()) + ")";
        return 0;
    }
    const bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;
    Handle request(WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        error = last_error("WinHttpOpenRequest");
        return 0;
    }
    std::wstring headers;
    if (!bearer.empty()) {
        headers = L"Authorization: Bearer " + widen(bearer) + L"\r\n";
    }
    if (!WinHttpSendRequest(request.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = "could not reach " + narrow(host) + " (error " + std::to_string(GetLastError()) + ")";
        return 0;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    std::vector<char> buf(1 << 16);
    for (;;) {
        if (cancel && cancel->load()) {
            error = "cancelled";
            return 0;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request.h, buf.data(), DWORD(buf.size()), &read)) {
            error = last_error("reading");
            return 0;
        }
        if (read == 0) {
            break;
        }
        if (status == 200 && !on_chunk(buf.data(), read)) {
            error = "could not write";
            return 0;
        }
    }
    return int(status);
}

} // namespace

HttpResponse http_get(const std::string& url, const std::string& bearer) {
    HttpResponse r;
    r.status = http_request(
        url, bearer,
        [&](const char* data, size_t n) {
            r.body.append(data, n);
            return true;
        },
        r.error, nullptr);
    return r;
}

std::string http_download(const std::string& url, const std::string& bearer, const fs::path& dest,
                          const std::function<void(uint64_t)>& on_bytes, const std::atomic<bool>* cancel) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "cannot write " + path_utf8(dest);
    }
    std::string error;
    const int status = http_request(
        url, bearer,
        [&](const char* data, size_t n) {
            out.write(data, std::streamsize(n));
            if (on_bytes) {
                on_bytes(n);
            }
            return bool(out);
        },
        error, cancel);
    out.close();
    if (status == 200 && error.empty()) {
        return {};
    }
    fs::remove(dest, ec);
    return error.empty() ? "HTTP " + std::to_string(status) + " for " + url : error;
}

FetchedManifest fetch_manifest(const Remote& remote, const PublicKey& key) {
    if (std::all_of(key.begin(), key.end(), [](uint8_t b) { return b == 0; })) {
        throw std::runtime_error("this launcher was built without a release key, so it can't check updates");
    }
    // The manifest and its signature can't be published atomically: a mismatch
    // may be a publish in progress, so it's tried once more before refusing.
    HttpResponse m;
    for (int attempt = 0;; ++attempt) {
        m = http_get(remote.manifest_url(), remote.token);
        if (m.status != 200) {
            throw std::runtime_error(m.status ? "the update server answered HTTP " + std::to_string(m.status) +
                                                    (m.status == 401 || m.status == 403 ? " (is the token right?)" : "")
                                              : m.error);
        }
        const HttpResponse s = http_get(remote.manifest_url() + ".sig", remote.token);
        if (s.status != 200) {
            throw std::runtime_error("no signature for the update manifest (HTTP " + std::to_string(s.status) + ")");
        }
        Signature sig{};
        const std::vector<uint8_t> bytes = from_hex(s.body);
        if (bytes.size() != sig.size()) {
            throw std::runtime_error("malformed update signature");
        }
        std::copy(bytes.begin(), bytes.end(), sig.begin());
        if (verify_signature(m.body, sig, key)) {
            break;
        }
        if (attempt == 1) {
            throw std::runtime_error("the update manifest's signature doesn't match: not installing it");
        }
        Sleep(3000);
    }
    FetchedManifest out{parse_manifest(m.body), m.body};
    if (out.manifest.channel != remote.channel) {
        throw std::runtime_error("the update manifest is for channel " + out.manifest.channel);
    }
    return out;
}

std::optional<ReleaseManifest> installed_manifest(const fs::path& install_dir) {
    std::ifstream in(install_dir / "state" / "installed.json", std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();
    try {
        return parse_manifest(text.str());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

UpdatePlan plan_update(const ReleaseManifest& next, const std::optional<ReleaseManifest>& installed,
                       const fs::path& install_dir, const std::string& launcher_path) {
    std::map<std::string, std::string> had;
    if (installed) {
        for (const auto& f : installed->files) {
            had[f.path] = f.sha256;
        }
    }
    UpdatePlan plan;
    std::set<std::string> wanted;
    for (const ManifestFile& f : next.files) {
        wanted.insert(f.path);
        const fs::path local = install_dir / utf8_path(f.path);
        std::error_code ec;
        const bool exists = fs::is_regular_file(local, ec);
        const uint64_t size = exists ? fs::file_size(local, ec) : 0;
        bool current = false;
        if (exists && size == f.size) {
            const auto it = had.find(f.path);
            current = (it != had.end() && it->second == f.sha256) || sha256_file(local) == f.sha256;
        }
        if (!current) {
            plan.download.push_back(f);
            plan.bytes += f.size;
            plan.replaces_launcher = plan.replaces_launcher || f.path == launcher_path;
        }
    }
    for (const auto& [path, sha] : had) {
        std::error_code ec;
        if (!wanted.contains(path) && fs::exists(install_dir / utf8_path(path), ec)) {
            plan.remove.push_back(path);
        }
    }
    return plan;
}

std::string apply_update(const Remote& remote, const UpdatePlan& plan, const std::string& json,
                         const fs::path& install_dir, const fs::path& launcher_exe, UpdateProgress& progress,
                         const std::atomic<bool>* cancel, int threads) {
    const fs::path staging = install_dir / "state" / "staging";
    std::error_code ec;
    fs::create_directories(staging, ec);

    // Download (identical contents once), verifying each.
    std::vector<const ManifestFile*> blobs;
    std::set<std::string> queued;
    for (const auto& f : plan.download) {
        if (queued.insert(f.sha256).second) {
            blobs.push_back(&f);
        }
    }
    std::atomic<size_t> next{0};
    std::mutex error_mutex;
    std::string first_error;
    const auto worker = [&] {
        for (;;) {
            const size_t i = next++;
            if (i >= blobs.size() || (cancel && cancel->load())) {
                return;
            }
            {
                std::lock_guard lock(error_mutex);
                if (!first_error.empty()) {
                    return;
                }
            }
            const ManifestFile& f = *blobs[i];
            const fs::path dest = staging / f.sha256;
            std::error_code e;
            if (fs::is_regular_file(dest, e) && fs::file_size(dest, e) == f.size && sha256_file(dest) == f.sha256) {
                progress.bytes += f.size;  // left from an interrupted update
                ++progress.files;
                continue;
            }
            std::string err = http_download(remote.blob_url(f.sha256), remote.token, dest,
                                            [&](uint64_t n) { progress.bytes += n; }, cancel);
            if (err.empty() && sha256_file(dest) != f.sha256) {
                err = "a download was corrupted: " + f.path;
                fs::remove(dest, e);
            }
            if (!err.empty()) {
                std::lock_guard lock(error_mutex);
                if (first_error.empty()) {
                    first_error = err;
                }
                return;
            }
            ++progress.files;
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, threads); ++t) {
        pool.emplace_back(worker);
    }
    for (auto& t : pool) {
        t.join();
    }
    if (cancel && cancel->load()) {
        return "cancelled";
    }
    if (!first_error.empty()) {
        return first_error;
    }

    // Into place. The running launcher can be renamed, not overwritten.
    const fs::path old_launcher = launcher_exe.parent_path() / "launcher.old.exe";
    for (const auto& f : plan.download) {
        const fs::path target = install_dir / utf8_path(f.path);
        fs::create_directories(target.parent_path(), ec);
        if (fs::equivalent(target, launcher_exe, ec)) {
            fs::remove(old_launcher, ec);
            fs::rename(launcher_exe, old_launcher, ec);
            if (ec) {
                return "could not replace the launcher: " + ec.message();
            }
        }
        // Copy, not move: the same blob can serve several paths.
        if (!CopyFileW((staging / f.sha256).wstring().c_str(), target.wstring().c_str(), FALSE)) {
            return "could not write " + f.path + " (is the game running?) (error " + std::to_string(GetLastError()) +
                   ")";
        }
    }
    for (const auto& p : plan.remove) {
        fs::remove(install_dir / utf8_path(p), ec);
    }
    {
        std::ofstream out(install_dir / "state" / "installed.json", std::ios::binary | std::ios::trunc);
        out << json;
        if (!out) {
            return "could not record the update";
        }
    }
    fs::remove_all(staging, ec);
    return {};
}

} // namespace swbfo::launcher
