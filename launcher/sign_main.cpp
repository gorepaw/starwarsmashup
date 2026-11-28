// swbfo_sign: the release signing key and manifest signatures (docs/DISTRIBUTION.md).
//
//   swbfo_sign keygen <secret.key> <release_key.h>   a new key pair: the secret (hex) stays outside
//                                                  the repo; the header holds the public key
//   swbfo_sign sign <secret.key> <file>              writes <file>.sig (hex Ed25519 signature)
//   swbfo_sign verify <file>                         checks <file>.sig against the built-in public key

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "release_key.h"
#include "update.h"

namespace fs = std::filesystem;
using namespace swbfo::launcher;

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + p.string());
    }
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

SecretKey read_secret(const fs::path& p) {
    const std::vector<uint8_t> bytes = from_hex(read_file(p));
    if (bytes.size() != 32) {
        throw std::runtime_error(p.string() + " isn't a secret key");
    }
    SecretKey k{};
    std::copy(bytes.begin(), bytes.end(), k.begin());
    return k;
}

int keygen(const fs::path& secret_file, const fs::path& header) {
    if (fs::exists(secret_file)) {
        std::cerr << secret_file.string() << " exists: not replacing a release key\n";
        return 1;
    }
    const SecretKey secret = random_secret_key();
    const PublicKey pub = public_key_of(secret);
    fs::create_directories(fs::absolute(secret_file).parent_path());
    std::ofstream(secret_file, std::ios::binary) << to_hex(secret.data(), secret.size()) << "\n";
    std::ofstream h(header, std::ios::binary);
    h << "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
         "// The release signing key's public half (swbfo_sign keygen). Launchers only\n"
         "// install updates whose manifest it signed. The secret half never enters the repo.\n"
         "namespace swbfo::launcher {\n\ninline constexpr std::array<uint8_t, 32> release_public_key = {";
    for (size_t i = 0; i < pub.size(); ++i) {
        h << (i % 8 == 0 ? "\n    " : " ") << "0x" << to_hex(&pub[i], 1) << ",";
    }
    h << "\n};\n\n} // namespace swbfo::launcher\n";
    std::cout << "public key " << to_hex(pub.data(), pub.size()) << "\nsecret key in " << secret_file.string()
              << " (back it up; never commit it)\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "keygen" && argc == 4) {
            return keygen(argv[2], argv[3]);
        }
        if (cmd == "sign" && argc == 4) {
            const SecretKey secret = read_secret(argv[2]);
            const std::string message = read_file(argv[3]);
            const Signature sig = sign(message, secret);
            if (!verify_signature(message, sig, public_key_of(secret))) {
                std::cerr << "signature didn't verify\n";
                return 1;
            }
            std::ofstream(std::string(argv[3]) + ".sig", std::ios::binary) << to_hex(sig.data(), sig.size()) << "\n";
            if (public_key_of(secret) != release_public_key) {
                std::cerr << "warning: this key isn't the one launchers are built with (release_key.h)\n";
            }
            return 0;
        }
        if (cmd == "verify" && argc == 3) {
            const std::vector<uint8_t> bytes = from_hex(read_file(std::string(argv[2]) + ".sig"));
            Signature sig{};
            if (bytes.size() != sig.size()) {
                std::cerr << "malformed signature\n";
                return 1;
            }
            std::copy(bytes.begin(), bytes.end(), sig.begin());
            const bool ok = verify_signature(read_file(argv[2]), sig, release_public_key);
            std::cout << (ok ? "good signature\n" : "BAD signature\n");
            return ok ? 0 : 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cerr << "usage:\n  swbfo_sign keygen <secret.key> <release_key.h>\n  swbfo_sign sign <secret.key> <file>\n"
                 "  swbfo_sign verify <file>\n";
    return 2;
}
