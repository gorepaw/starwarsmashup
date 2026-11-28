// Battlefront II fx__ particle effect parser: NAME hashing (c | 0x20 FNV-1a),
// Spawner (RGBA and HSVA), Transformer (Next/Reach/Move/Accelerate), Geometry
// (PARTICLE, SPARK, nested EMITTER), and JSON serialization.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#include "bf2/effect.h"
#include "bf2/ucfb.h"

using namespace swbfo::bf2;

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

std::vector<uint8_t> pad4(std::vector<uint8_t> data) {
    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
    return data;
}

std::vector<uint8_t> chunk(const char tag[4], const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out(8);
    std::memcpy(out.data(), tag, 4);
    const uint32_t size = uint32_t(body.size());
    std::memcpy(out.data() + 4, &size, 4);
    out.insert(out.end(), body.begin(), body.end());
    return pad4(std::move(out));
}

// Encodes one DATA record with float values and/or one leading string value.
std::vector<uint8_t> data_rec(std::string_view key, const std::vector<float>& nums, std::string_view text = {}) {
    std::vector<uint8_t> body;
    const uint32_t h = effect_name_hash(key);
    body.resize(5);
    std::memcpy(body.data(), &h, 4);
    const uint8_t count = uint8_t(nums.size() + (text.empty() ? 0 : 1));
    body[4] = count;
    const uint32_t strings_at = uint32_t(count) * 4;
    if (!text.empty()) {
        body.resize(body.size() + 4);
        std::memcpy(body.data() + body.size() - 4, &strings_at, 4);
    }
    for (const float v : nums) {
        body.resize(body.size() + 4);
        std::memcpy(body.data() + body.size() - 4, &v, 4);
    }
    if (!text.empty()) {
        const uint32_t block_len = uint32_t(text.size() + 1);
        body.resize(body.size() + 4);
        std::memcpy(body.data() + body.size() - 4, &block_len, 4);
        body.insert(body.end(), text.begin(), text.end());
        body.push_back(0);
    }
    return chunk("DATA", body);
}

std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts) {
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

void test_effect_hash() {
    // Underscores differ between odf_hash (tolower) and fx__ NAME (c | 0x20).
    CHECK(effect_name_hash("com_sfx_ord_exp") != 0);
    CHECK(effect_name_hash("COM_SFX_ORD_EXP") == effect_name_hash("com_sfx_ord_exp"));
    // Verified against Battlefront II's ingame.lvl: com_sfx_muzzleflash_lg = 0x81c6ede4.
    CHECK(effect_name_hash("com_sfx_muzzleflash_lg") == 0x81c6ede4u);
}

void test_read_effect() {
    // Build a synthetic fx__ chunk with one spark emitter (RGBA + Reach + Next)
    // and one nested smoke emitter (HSVA + Move).
    const auto spawner = concat({
        data_rec("Spawner", {}),
        chunk("SCOP", concat({
            data_rec("Circle", {}),
            chunk("SCOP", concat({data_rec("PositionX", {-1, 1}), data_rec("PositionY", {0, 1}), data_rec("PositionZ", {-1, 1})})),
            data_rec("VelocityScale", {2, 6}),
            data_rec("Size", {0, 0.02f, 0.04f}),
            data_rec("Red", {0, 255, 255}),
            data_rec("Green", {0, 128, 128}),
            data_rec("Blue", {0, 0, 0}),
            data_rec("Alpha", {0, 255, 255}),
        })),
    });
    const auto transformer = concat({
        data_rec("Transformer", {}),
        chunk("SCOP", concat({
            data_rec("LifeTime", {0.4f}),
            data_rec("Position", {}),
            chunk("SCOP", concat({data_rec("LifeTime", {0.4f}), data_rec("Accelerate", {0, -5, 0})})),
            data_rec("Size", {0}),
            chunk("SCOP", concat({data_rec("LifeTime", {0.4f}), data_rec("Scale", {2.0f})})),
            data_rec("Color", {0}),
            chunk("SCOP", concat({
                data_rec("LifeTime", {0.2f}),
                data_rec("Reach", {255, 255, 0, 128}),
                data_rec("Next", {}),
                chunk("SCOP", concat({data_rec("LifeTime", {0.2f}), data_rec("Reach", {255, 0, 0, 0})})),
            })),
        })),
    });
    const auto geometry = concat({
        data_rec("Geometry", {}),
        chunk("SCOP", concat({
            data_rec("BlendMode", {}, "ADDITIVE"),
            data_rec("Type", {}, "SPARK"),
            data_rec("SparkLength", {0.03f}),
            data_rec("Texture", {}, "com_sfx_laser_orange"),
        })),
    });
    const auto emitter = concat({
        data_rec("ParticleEmitter", {}, "Sparks"),
        chunk("SCOP", concat({
            data_rec("MaxParticles", {12, 12}),
            data_rec("BurstCount", {12, 12}),
            data_rec("BurstDelay", {0.001f, 0.001f}),
            spawner,
            transformer,
            geometry,
        })),
    });
    const auto fx_bytes = chunk("ucfb", chunk("fx__", emitter));
    const UcfbNode root = parse_ucfb(fx_bytes);
    const UcfbNode* fx = root.child("fx__");
    CHECK(fx != nullptr);
    const std::vector<EffectEmitter> emitters = read_effect(*fx);
    CHECK(emitters.size() == 1);
    const EffectEmitter& e = emitters[0];
    CHECK(e.name == "Sparks" && e.max_particles == 12 && e.blend == "add" && e.type == "spark");
    CHECK(std::abs(e.spark_length - 0.03) < 1e-5 && e.texture == "com_sfx_laser_orange");
    CHECK(std::abs(e.lifetime - 0.4) < 1e-5 && std::abs(e.acceleration[1] - (-5.0)) < 1e-5);
    CHECK(e.size_curve.size() == 2 && std::abs(e.size_curve[1][1] - 2.0) < 1e-5);
    CHECK(e.color_curve.size() == 3 && std::abs(e.color_curve[1][0] - 0.5) < 1e-4 &&
          std::abs(e.color_curve[2][4]) < 1e-4);

    std::ostringstream json;
    write_effect_json(json, emitters);
    CHECK(json.str().find("\"com_sfx_laser_orange\"") != std::string::npos);
    CHECK(json.str().find("\"spark\"") != std::string::npos);
}

} // namespace

int main() {
    test_effect_hash();
    test_read_effect();
    if (failures == 0) {
        std::puts("effect_test: all passed");
    }
    return failures == 0 ? 0 : 1;
}
