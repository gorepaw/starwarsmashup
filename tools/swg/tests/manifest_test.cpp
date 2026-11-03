#include <cstdio>
#include <sstream>
#include <string>

#include "swg/manifest.h"

using namespace swm::swg;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

void test_normalize_path() {
    CHECK(normalize_path("Appearance\\Mesh\\Foo.MSH") == "appearance/mesh/foo.msh");
    CHECK(normalize_path("/texture/a.dds") == "texture/a.dds");
    CHECK(normalize_path("") == "");
}

void test_content_groups() {
    CHECK(content_group("data_other_00.tre") == "base");
    CHECK(content_group("patch_15_02.tre") == "base");
    CHECK(content_group("data_sku1_03.tre") == "jtl");
    CHECK(content_group("patch_sku2_15_00.tre") == "rotw");
    CHECK(content_group("sub/dir/data_sku3_00.tre") == "tow");

    Manifest manifest;
    manifest.entries = {
        {"a", 1, "data_other_00.tre", "shared.iff"},
        {"b", 1, "patch_sku1_12_00.tre", "shared.iff"},  // also in base: still base
        {"c", 1, "data_sku1_00.tre", "ship.msh"},
        {"d", 1, "patch_sku1_14_00.tre", "both.iff"},
        {"e", 1, "patch_sku2_15_00.tre", "both.iff"},
    };
    const auto groups = path_content_groups(manifest);
    CHECK(groups.size() == 3);
    CHECK(groups.at("shared.iff") == "base");
    CHECK(groups.at("ship.msh") == "jtl");
    CHECK(groups.at("both.iff") == "jtl+rotw");
}

void test_round_trip() {
    Manifest manifest{"test-manifest", "unit test", {{"abc123", 42, "data_other_00.tre", "misc/file.iff"}}};
    std::stringstream buffer;
    write_manifest(manifest, buffer);
    const Manifest read = read_manifest(buffer);
    CHECK(read.name == "test-manifest");
    CHECK(read.source == "unit test");
    CHECK(read.entries.size() == 1);
    if (read.entries.size() == 1) {
        CHECK(read.entries[0].sha256 == "abc123");
        CHECK(read.entries[0].size == 42);
        CHECK(read.entries[0].archive == "data_other_00.tre");
        CHECK(read.entries[0].path == "misc/file.iff");
    }
}

} // namespace

int main() {
    test_normalize_path();
    test_content_groups();
    test_round_trip();

    if (failures == 0) {
        std::printf("manifest_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
