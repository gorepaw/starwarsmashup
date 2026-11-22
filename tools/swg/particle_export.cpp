#include "swg/particle_export.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <ostream>

#include "swg/appearance.h"
#include "swg/image.h"
#include "swg/particle_effect.h"

namespace swm::swg {

namespace {

std::string points(const Waveform& wave) {
    std::string out = "[";
    for (size_t i = 0; i < wave.points.size(); ++i) {
        out += std::format("{}[{}, {}]", i ? ", " : "", wave.points[i].t, wave.points[i].value);
    }
    return out + "]";
}

std::string json_text(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += (unsigned char)c < 0x20 ? ' ' : c;
    }
    return out + "\"";
}

} // namespace

std::string export_particle_effect(TreLibrary& library, const std::string& prt_path,
                                   const std::filesystem::path& out_dir, std::ostream& log) {
    const std::vector<ParticleEmitter> emitters = parse_particle_effect(library.read(prt_path));
    std::filesystem::create_directories(out_dir);
    std::string json = "{\"emitters\": [";
    for (size_t i = 0; i < emitters.size(); ++i) {
        const ParticleEmitter& e = emitters[i];
        // Its texture and how it's blended: the shader's.
        std::string texture, blend = "alpha";
        if (!e.shader.empty() && library.contains(e.shader)) {
            const std::vector<uint8_t> shader = library.read(e.shader);
            const std::string texture_path = shader_main_texture(shader);
            if (!texture_path.empty() && library.contains(texture_path)) {
                texture = std::filesystem::path(texture_path).stem().string() + ".png";
                if (!std::filesystem::exists(out_dir / texture)) {
                    write_png(out_dir / texture, decode_dds(library.read(texture_path)));
                }
            }
            const std::string effect = shader_effect(shader);
            if (!effect.empty() && library.contains(effect) &&
                effect_blend(library.read(effect)) == EffectBlend::additive) {
                blend = "add";
            }
        }
        std::string color = "[";
        for (size_t c = 0; c < e.color.size(); ++c) {
            color += std::format("{}[{}, {}, {}, {}]", c ? ", " : "", e.color[c][0], e.color[c][1], e.color[c][2],
                                 e.color[c][3]);
        }
        color += "]";
        const float spread = std::max(std::abs(e.spread.low()), std::abs(e.spread.high()));
        json += std::format("{}\n  {{\"name\": {}, \"rate\": {}, \"speed\": {}, \"lifetime\": [{}, {}], ",
                            i ? "," : "", json_text(e.name), e.rate.mean(), e.speed.mean(),
                            std::max(0.05f, e.lifetime.low()), std::max(0.05f, e.lifetime.high()));
        json += std::format("\"gravity\": {}, \"radius\": {}, \"spread\": {},\n   ", e.gravity.mean(), e.radius.mean(),
                            spread);
        json += std::format("\"color\": {}, \"alpha\": {}, \"speed_scale\": {}, \"size\": {},\n   ", color,
                            points(e.alpha), points(e.speed_scale), points(e.size));
        json += std::format("\"texture\": {}, \"frames\": {}, \"grid\": {}, \"fps\": {}, ", json_text(texture), e.frames,
                            e.grid, e.fps);
        json += "\"blend\": " + json_text(blend) + "}";
    }
    json += "\n]}\n";
    const std::string file = std::filesystem::path(prt_path).stem().string() + ".json";
    std::ofstream(out_dir / file, std::ios::binary) << json;
    log << std::format("  particles {}: {} emitter(s)\n", prt_path, emitters.size());
    return file;
}

} // namespace swm::swg
