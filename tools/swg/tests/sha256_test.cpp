#include <cstdio>
#include <string>

#include "swg/sha256.h"

using swm::swg::Sha256;

namespace {

int failures = 0;

void check(const std::string& label, const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        std::printf("FAIL %s\n  expected %s\n  actual   %s\n", label.c_str(), expected.c_str(), actual.c_str());
        ++failures;
    }
}

} // namespace

int main() {
    // FIPS 180-2 / NIST test vectors.
    check("empty", Sha256::hex_of("", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("abc", Sha256::hex_of("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check("448 bits", Sha256::hex_of(two_blocks.data(), two_blocks.size()),
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // One million 'a', fed in uneven chunks to exercise buffering.
    const std::string chunk(997, 'a');
    Sha256 hasher;
    size_t remaining = 1000000;
    while (remaining > 0) {
        const size_t n = remaining < chunk.size() ? remaining : chunk.size();
        hasher.update(chunk.data(), n);
        remaining -= n;
    }
    check("million a", Sha256::to_hex(hasher.finish()),
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    if (failures == 0) {
        std::printf("sha256_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
