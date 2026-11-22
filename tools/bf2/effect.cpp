#include "bf2/effect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <ostream>

#include "bf2/hud.h"

namespace swm::bf2 {

namespace {

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    }
    return out + "\"";
}

// Convert Battlefront II HSV (each 0..255) to linear-ish sRGB (0..1).
std::array<double, 3> hsv_to_rgb(double h255, double s255, double v255) {
    const double h = std::fmod(std::fmod(h255 / 255.0, 1.0) + 1.0, 1.0) * 6.0;
    const double s = std::clamp(s255 / 255.0, 0.0, 1.0);
    const double v = std::clamp(v255 / 255.0, 0.0, 1.0);
    const int sector = int(std::floor(h)) % 6;
    const double f = h - std::floor(h);
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - s * f);
    const double t = v * (1.0 - s * (1.0 - f));
    switch (sector) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
    }
}

const HudNode* find_child(const HudNode& parent, uint32_t hash) {
    for (const HudNode& child : parent.children) {
        if (child.hash == hash) {
            return &child;
        }
    }
    return nullptr;
}

double num(const HudNode& node, size_t index, double fallback = 0) {
    return index < node.values.size() && !node.values[index].is_text ? node.values[index].number : fallback;
}

std::array<double, 2> pair_vals(const HudNode& node, size_t first, double fallback = 0) {
    const double a = num(node, first, fallback);
    const double b = num(node, first + 1, a);
    return {a, b};
}

// Precomputed fx__ keyword hashes.
const uint32_t H_ParticleEmitter = effect_name_hash("ParticleEmitter");
const uint32_t H_MaxParticles = effect_name_hash("MaxParticles");
const uint32_t H_StartDelay = effect_name_hash("StartDelay");
const uint32_t H_BurstDelay = effect_name_hash("BurstDelay");
const uint32_t H_BurstCount = effect_name_hash("BurstCount");
const uint32_t H_Spawner = effect_name_hash("Spawner");
const uint32_t H_Transformer = effect_name_hash("Transformer");
const uint32_t H_Geometry = effect_name_hash("Geometry");
const uint32_t H_Circle = effect_name_hash("Circle");
const uint32_t H_Spread = effect_name_hash("Spread");
const uint32_t H_Offset = effect_name_hash("Offset");
const uint32_t H_PositionX = effect_name_hash("PositionX");
const uint32_t H_PositionY = effect_name_hash("PositionY");
const uint32_t H_PositionZ = effect_name_hash("PositionZ");
const uint32_t H_PositionScale = effect_name_hash("PositionScale");
const uint32_t H_VelocityScale = effect_name_hash("VelocityScale");
const uint32_t H_InheritVelocityFactor = effect_name_hash("InheritVelocityFactor");
const uint32_t H_Size = effect_name_hash("Size");
const uint32_t H_Red = effect_name_hash("Red");
const uint32_t H_Green = effect_name_hash("Green");
const uint32_t H_Blue = effect_name_hash("Blue");
const uint32_t H_Hue = effect_name_hash("Hue");
const uint32_t H_Saturation = effect_name_hash("Saturation");
const uint32_t H_Value = effect_name_hash("Value");
const uint32_t H_Alpha = effect_name_hash("Alpha");
const uint32_t H_StartRotation = effect_name_hash("StartRotation");
const uint32_t H_RotationVelocity = effect_name_hash("RotationVelocity");
const uint32_t H_LifeTime = effect_name_hash("LifeTime");
const uint32_t H_Position = effect_name_hash("Position");
const uint32_t H_Color = effect_name_hash("Color");
const uint32_t H_Scale = effect_name_hash("Scale");
const uint32_t H_Accelerate = effect_name_hash("Accelerate");
const uint32_t H_Next = effect_name_hash("Next");
const uint32_t H_Reach = effect_name_hash("Reach");
const uint32_t H_Move = effect_name_hash("Move");
const uint32_t H_BlendMode = effect_name_hash("BlendMode");
const uint32_t H_Type = effect_name_hash("Type");
const uint32_t H_Texture = effect_name_hash("Texture");
const uint32_t H_SparkLength = effect_name_hash("SparkLength");

void parse_emitter(const HudNode& node, std::vector<EffectEmitter>& out) {
    EffectEmitter e;
    e.name = node.text;
    if (const HudNode* n = find_child(node, H_MaxParticles)) {
        e.max_particles = int(std::lround(num(*n, 0, 1)));
    }
    if (const HudNode* n = find_child(node, H_StartDelay)) {
        e.start_delay = std::max(0.0, num(*n, 0, 0));
    }
    if (const HudNode* n = find_child(node, H_BurstDelay)) {
        e.burst_delay = pair_vals(*n, 0, 0.05);
    }
    if (const HudNode* n = find_child(node, H_BurstCount)) {
        e.burst_count = pair_vals(*n, 0, 1);
    }

    // Emitter-level scale multiplier (Size(1, 1) on ParticleEmitter).
    double emitter_scale = 1.0;
    if (const HudNode* n = find_child(node, H_Size)) {
        emitter_scale = std::max(0.01, (num(*n, 0, 1) + num(*n, 1, num(*n, 0, 1))) * 0.5);
    }

    // Spawner
    bool uses_hsv = false;
    std::array<double, 4> hsv_mid{0, 0, 255, 255};
    if (const HudNode* spawner = find_child(node, H_Spawner)) {
        const HudNode* shape_node = find_child(*spawner, H_Circle);
        if (shape_node != nullptr) {
            e.shape = "circle";
        } else if ((shape_node = find_child(*spawner, H_Spread)) != nullptr) {
            e.shape = "spread";
        }
        if (shape_node != nullptr) {
            if (const HudNode* x = find_child(*shape_node, H_PositionX)) {
                e.pos_min[0] = num(*x, 0, 0);
                e.pos_max[0] = num(*x, 1, e.pos_min[0]);
            }
            if (const HudNode* y = find_child(*shape_node, H_PositionY)) {
                e.pos_min[1] = num(*y, 0, 0);
                e.pos_max[1] = num(*y, 1, e.pos_min[1]);
            }
            if (const HudNode* z = find_child(*shape_node, H_PositionZ)) {
                e.pos_min[2] = num(*z, 0, 0);
                e.pos_max[2] = num(*z, 1, e.pos_min[2]);
            }
        }
        if (const HudNode* offset = find_child(*spawner, H_Offset)) {
            if (const HudNode* x = find_child(*offset, H_PositionX)) {
                e.offset_min[0] = num(*x, 0, 0);
                e.offset_max[0] = num(*x, 1, e.offset_min[0]);
            }
            if (const HudNode* y = find_child(*offset, H_PositionY)) {
                e.offset_min[1] = num(*y, 0, 0);
                e.offset_max[1] = num(*y, 1, e.offset_min[1]);
            }
            if (const HudNode* z = find_child(*offset, H_PositionZ)) {
                e.offset_min[2] = num(*z, 0, 0);
                e.offset_max[2] = num(*z, 1, e.offset_min[2]);
            }
        }
        if (const HudNode* n = find_child(*spawner, H_PositionScale)) {
            e.pos_scale = pair_vals(*n, 0, 0);
        }
        if (const HudNode* n = find_child(*spawner, H_VelocityScale)) {
            e.vel_scale = pair_vals(*n, 0, 0);
        }
        if (const HudNode* n = find_child(*spawner, H_InheritVelocityFactor)) {
            e.inherit_velocity = num(*n, 0, 0);
        }
        // Spawner properties have a leading 0 mode value: Size(0, min, max).
        if (const HudNode* n = find_child(*spawner, H_Size)) {
            const size_t base = n->values.size() >= 3 ? 1 : 0;
            e.size = {num(*n, base, 0.5) * emitter_scale, num(*n, base + 1, num(*n, base, 0.5)) * emitter_scale};
        }
        const HudNode* r_node = find_child(*spawner, H_Red);
        const HudNode* g_node = find_child(*spawner, H_Green);
        const HudNode* b_node = find_child(*spawner, H_Blue);
        const HudNode* h_node = find_child(*spawner, H_Hue);
        const HudNode* s_node = find_child(*spawner, H_Saturation);
        const HudNode* v_node = find_child(*spawner, H_Value);
        const HudNode* a_node = find_child(*spawner, H_Alpha);
        auto range3 = [](const HudNode* n, double fallback) -> std::array<double, 2> {
            if (n == nullptr) {
                return {fallback, fallback};
            }
            const size_t base = n->values.size() >= 3 ? 1 : 0;
            const double lo = num(*n, base, fallback);
            return {lo, num(*n, base + 1, lo)};
        };
        const auto alpha = range3(a_node, 255);
        if (r_node != nullptr || g_node != nullptr || b_node != nullptr) {
            const auto r = range3(r_node, 255);
            const auto g = range3(g_node, 255);
            const auto b = range3(b_node, 255);
            e.color_min = {std::clamp(r[0] / 255.0, 0.0, 1.0), std::clamp(g[0] / 255.0, 0.0, 1.0),
                           std::clamp(b[0] / 255.0, 0.0, 1.0), std::clamp(alpha[0] / 255.0, 0.0, 1.0)};
            e.color_max = {std::clamp(r[1] / 255.0, 0.0, 1.0), std::clamp(g[1] / 255.0, 0.0, 1.0),
                           std::clamp(b[1] / 255.0, 0.0, 1.0), std::clamp(alpha[1] / 255.0, 0.0, 1.0)};
        } else if (h_node != nullptr || s_node != nullptr || v_node != nullptr) {
            uses_hsv = true;
            const auto h = range3(h_node, 0);
            const auto s = range3(s_node, 0);
            const auto v = range3(v_node, 255);
            hsv_mid = {(h[0] + h[1]) * 0.5, (s[0] + s[1]) * 0.5, (v[0] + v[1]) * 0.5, (alpha[0] + alpha[1]) * 0.5};
            const auto c0 = hsv_to_rgb(h[0], s[0], v[0]);
            const auto c1 = hsv_to_rgb(h[1], s[1], v[1]);
            e.color_min = {c0[0], c0[1], c0[2], std::clamp(alpha[0] / 255.0, 0.0, 1.0)};
            e.color_max = {c1[0], c1[1], c1[2], std::clamp(alpha[1] / 255.0, 0.0, 1.0)};
        } else {
            e.color_min[3] = std::clamp(alpha[0] / 255.0, 0.0, 1.0);
            e.color_max[3] = std::clamp(alpha[1] / 255.0, 0.0, 1.0);
        }
        if (const HudNode* n = find_child(*spawner, H_StartRotation)) {
            const size_t base = n->values.size() >= 3 ? 1 : 0;
            e.start_rotation = pair_vals(*n, base, 0);
        }
        if (const HudNode* n = find_child(*spawner, H_RotationVelocity)) {
            const size_t base = n->values.size() >= 3 ? 1 : 0;
            e.rotation_velocity = pair_vals(*n, base, 0);
        }
    }

    // Initial midpoint color for building the color curve.
    std::array<double, 4> cur_rgba{(e.color_min[0] + e.color_max[0]) * 0.5, (e.color_min[1] + e.color_max[1]) * 0.5,
                                   (e.color_min[2] + e.color_max[2]) * 0.5, (e.color_min[3] + e.color_max[3]) * 0.5};
    std::array<double, 4> cur_hsv = hsv_mid;

    // Transformer
    e.size_curve.push_back({0.0, 1.0});
    e.color_curve.push_back({0.0, cur_rgba[0], cur_rgba[1], cur_rgba[2], cur_rgba[3]});
    if (const HudNode* xf = find_child(node, H_Transformer)) {
        if (const HudNode* lt = find_child(*xf, H_LifeTime)) {
            e.lifetime = std::max(0.05, num(*lt, 0, 1.0));
        }
        if (const HudNode* pos = find_child(*xf, H_Position)) {
            if (const HudNode* acc = find_child(*pos, H_Accelerate)) {
                e.acceleration = {num(*acc, 0, 0), num(*acc, 1, 0), num(*acc, 2, 0)};
            }
            if (const HudNode* sc = find_child(*pos, H_Scale)) {
                e.damped = num(*sc, 0, 1) < 0.5;
            }
        }
        // Chained Size keyframes (each node has LifeTime, Scale, optional Next).
        if (const HudNode* sz = find_child(*xf, H_Size)) {
            double elapsed = 0;
            double scale = 1.0;
            for (const HudNode* seg = sz; seg != nullptr; seg = find_child(*seg, H_Next)) {
                double seg_dur = e.lifetime;
                if (const HudNode* lt = find_child(*seg, H_LifeTime)) {
                    seg_dur = std::max(0.01, num(*lt, 0, e.lifetime));
                }
                elapsed += seg_dur;
                if (const HudNode* sc = find_child(*seg, H_Scale)) {
                    scale = std::max(0.0, num(*sc, 0, scale));
                }
                const double t = std::clamp(elapsed / e.lifetime, 0.0, 1.0);
                e.size_curve.push_back({t, scale});
                if (t >= 1.0) {
                    break;
                }
            }
        }
        // Chained Color keyframes (Reach(r,g,b,a) or Move(dh,ds,dv,da) + Next).
        if (const HudNode* col = find_child(*xf, H_Color)) {
            double elapsed = 0;
            for (const HudNode* seg = col; seg != nullptr; seg = find_child(*seg, H_Next)) {
                double seg_dur = e.lifetime;
                if (const HudNode* lt = find_child(*seg, H_LifeTime)) {
                    seg_dur = std::max(0.01, num(*lt, 0, e.lifetime));
                }
                elapsed += seg_dur;
                if (const HudNode* reach = find_child(*seg, H_Reach)) {
                    if (uses_hsv) {
                        cur_hsv = {num(*reach, 0, cur_hsv[0]), num(*reach, 1, cur_hsv[1]), num(*reach, 2, cur_hsv[2]),
                                   num(*reach, 3, cur_hsv[3])};
                        const auto rgb = hsv_to_rgb(cur_hsv[0], cur_hsv[1], cur_hsv[2]);
                        cur_rgba = {rgb[0], rgb[1], rgb[2], std::clamp(cur_hsv[3] / 255.0, 0.0, 1.0)};
                    } else {
                        cur_rgba = {std::clamp(num(*reach, 0, 255) / 255.0, 0.0, 1.0),
                                    std::clamp(num(*reach, 1, 255) / 255.0, 0.0, 1.0),
                                    std::clamp(num(*reach, 2, 255) / 255.0, 0.0, 1.0),
                                    std::clamp(num(*reach, 3, 255) / 255.0, 0.0, 1.0)};
                    }
                } else if (const HudNode* move = find_child(*seg, H_Move)) {
                    if (uses_hsv) {
                        cur_hsv[0] += num(*move, 0, 0);
                        cur_hsv[1] = std::clamp(cur_hsv[1] + num(*move, 1, 0), 0.0, 255.0);
                        cur_hsv[2] = std::clamp(cur_hsv[2] + num(*move, 2, 0), 0.0, 255.0);
                        cur_hsv[3] = std::clamp(cur_hsv[3] + num(*move, 3, 0), 0.0, 255.0);
                        const auto rgb = hsv_to_rgb(cur_hsv[0], cur_hsv[1], cur_hsv[2]);
                        cur_rgba = {rgb[0], rgb[1], rgb[2], std::clamp(cur_hsv[3] / 255.0, 0.0, 1.0)};
                    } else {
                        for (size_t k = 0; k < 4; ++k) {
                            cur_rgba[k] = std::clamp(cur_rgba[k] + num(*move, k, 0) / 255.0, 0.0, 1.0);
                        }
                    }
                }
                const double t = std::clamp(elapsed / e.lifetime, 0.0, 1.0);
                e.color_curve.push_back({t, cur_rgba[0], cur_rgba[1], cur_rgba[2], cur_rgba[3]});
                if (t >= 1.0) {
                    break;
                }
            }
        }
    }

    // Geometry + nested child ParticleEmitters.
    std::string geom_type = "particle";
    if (const HudNode* geom = find_child(node, H_Geometry)) {
        if (const HudNode* bm = find_child(*geom, H_BlendMode)) {
            const std::string mode = lowercase(bm->text);
            e.blend = mode == "additive" ? "add" : "alpha";
        }
        if (const HudNode* ty = find_child(*geom, H_Type)) {
            geom_type = lowercase(ty->text);
        }
        if (const HudNode* sl = find_child(*geom, H_SparkLength)) {
            e.spark_length = std::max(0.0, num(*sl, 0, 0));
        }
        if (const HudNode* tex = find_child(*geom, H_Texture)) {
            e.texture = lowercase(tex->text);
        }
        // Only emit drawable particle/spark layers (EMITTER is a parent shell
        // whose child ParticleEmitters hold the actual sprites).
        if (geom_type != "emitter" && !e.texture.empty()) {
            e.type = geom_type == "spark" ? "spark" : "particle";
            out.push_back(e);
        }
        for (const HudNode& child : geom->children) {
            if (child.hash == H_ParticleEmitter) {
                parse_emitter(child, out);
            }
        }
    }
    // Sibling child ParticleEmitters attached directly under this emitter.
    for (const HudNode& child : node.children) {
        if (child.hash == H_ParticleEmitter) {
            parse_emitter(child, out);
        }
    }
}

} // namespace

uint32_t effect_name_hash(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash ^= uint32_t(static_cast<unsigned char>(c) | 0x20u);
        hash *= 16777619u;
    }
    return hash;
}

std::vector<EffectEmitter> read_effect(const UcfbNode& fx_chunk) {
    std::vector<EffectEmitter> out;
    for (const HudNode& node : read_config_tree(fx_chunk)) {
        if (node.hash == H_ParticleEmitter) {
            parse_emitter(node, out);
        }
    }
    return out;
}

void write_effect_json(std::ostream& out, const std::vector<EffectEmitter>& emitters) {
    out << "{\n  \"emitters\": [";
    for (size_t i = 0; i < emitters.size(); ++i) {
        const EffectEmitter& e = emitters[i];
        out << (i ? ",\n    {" : "\n    {") << "\"name\": " << json_string(e.name)
            << std::format(", \"max_particles\": {}, \"start_delay\": {:.4g}", e.max_particles, e.start_delay)
            << std::format(", \"burst_delay\": [{:.4g}, {:.4g}], \"burst_count\": [{:.4g}, {:.4g}]", e.burst_delay[0],
                           e.burst_delay[1], e.burst_count[0], e.burst_count[1])
            << ", \"shape\": " << json_string(e.shape)
            << std::format(", \"pos_min\": [{:.4g}, {:.4g}, {:.4g}], \"pos_max\": [{:.4g}, {:.4g}, {:.4g}]",
                           e.pos_min[0], e.pos_min[1], e.pos_min[2], e.pos_max[0], e.pos_max[1], e.pos_max[2])
            << std::format(", \"offset_min\": [{:.4g}, {:.4g}, {:.4g}], \"offset_max\": [{:.4g}, {:.4g}, {:.4g}]",
                           e.offset_min[0], e.offset_min[1], e.offset_min[2], e.offset_max[0], e.offset_max[1],
                           e.offset_max[2])
            << std::format(", \"pos_scale\": [{:.4g}, {:.4g}], \"vel_scale\": [{:.4g}, {:.4g}]", e.pos_scale[0],
                           e.pos_scale[1], e.vel_scale[0], e.vel_scale[1])
            << std::format(", \"size\": [{:.4g}, {:.4g}], \"lifetime\": {:.4g}", e.size[0], e.size[1], e.lifetime)
            << std::format(", \"acceleration\": [{:.4g}, {:.4g}, {:.4g}], \"damped\": {}", e.acceleration[0],
                           e.acceleration[1], e.acceleration[2], e.damped ? "true" : "false")
            << std::format(", \"start_rotation\": [{:.4g}, {:.4g}], \"rotation_velocity\": [{:.4g}, {:.4g}]",
                           e.start_rotation[0], e.start_rotation[1], e.rotation_velocity[0], e.rotation_velocity[1])
            << ", \"size_curve\": [";
        for (size_t k = 0; k < e.size_curve.size(); ++k) {
            out << (k ? ", " : "") << std::format("[{:.4g}, {:.4g}]", e.size_curve[k][0], e.size_curve[k][1]);
        }
        out << "], \"color_curve\": [";
        for (size_t k = 0; k < e.color_curve.size(); ++k) {
            out << (k ? ", " : "")
                << std::format("[{:.4g}, {:.4g}, {:.4g}, {:.4g}, {:.4g}]", e.color_curve[k][0], e.color_curve[k][1],
                               e.color_curve[k][2], e.color_curve[k][3], e.color_curve[k][4]);
        }
        out << "], \"blend\": " << json_string(e.blend) << ", \"type\": " << json_string(e.type)
            << std::format(", \"spark_length\": {:.4g}", e.spark_length)
            << ", \"texture\": " << json_string(e.texture) << "}";
    }
    out << "\n  ]\n}\n";
}

} // namespace swm::bf2
