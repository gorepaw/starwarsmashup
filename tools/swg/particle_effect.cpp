#include "swg/particle_effect.h"

#include <algorithm>
#include <stdexcept>

#include "swg/iff.h"
#include "swg/manifest.h"

namespace swm::swg {

float Waveform::mean() const {
    if (points.empty()) {
        return 0;
    }
    float sum = 0;
    for (const Point& p : points) {
        sum += p.value;
    }
    return sum / float(points.size());
}

float Waveform::low() const {
    float out = points.empty() ? 0 : 1e30f;
    for (const Point& p : points) {
        out = std::min(out, p.value - p.random_below);
    }
    return out;
}

float Waveform::high() const {
    float out = points.empty() ? 0 : -1e30f;
    for (const Point& p : points) {
        out = std::max(out, p.value + p.random_above);
    }
    return out;
}

namespace {

// A WVFM form's points.
Waveform read_waveform(const IffNode& form) {
    const IffNode& chunk = form.expect("0001");
    IffReader r(chunk.data);
    r.u32();
    r.u32();
    const uint32_t count = r.u32();
    if (r.remaining() < size_t(count) * 16) {
        throw std::runtime_error("waveform: truncated");
    }
    Waveform out;
    for (uint32_t i = 0; i < count; ++i) {
        Waveform::Point p;
        p.t = r.f32();
        p.value = r.f32();
        p.random_below = r.f32();
        p.random_above = r.f32();
        out.points.push_back(p);
    }
    return out;
}

ParticleEmitter read_emitter(const IffNode& emitter) {
    const IffNode* version = emitter.child("0012");
    if (version == nullptr) {
        version = emitter.child("0011");
    }
    if (version == nullptr) {
        throw std::runtime_error("emitter: unknown version");
    }
    const std::vector<const IffNode*> waves = version->children_with("WVFM");
    if (waves.size() < 16) {
        throw std::runtime_error("emitter: fewer than 16 waveforms");
    }
    ParticleEmitter out;
    out.radius = read_waveform(*waves[6]);
    out.spread = read_waveform(*waves[8]);
    out.rate = read_waveform(*waves[9]);
    out.speed = read_waveform(*waves[10]);
    out.lifetime = read_waveform(*waves[14]);
    out.gravity = read_waveform(*waves[15]);
    if (const IffNode* settings = version->child("0000"); settings != nullptr && !settings->form) {
        out.name = IffReader(settings->data).cstring();
    }

    const IffNode& quad = version->expect("PTQD");
    const IffNode& particle = quad.expect("PTCL").expect("0002");
    if (const IffNode* colors = particle.child("CLRR")) {
        IffReader r(colors->expect("0001").data);
        r.u32();
        r.u32();
        const uint32_t count = r.u32();
        if (r.remaining() < size_t(count) * 16) {
            throw std::runtime_error("color ramp: truncated");
        }
        for (uint32_t i = 0; i < count; ++i) {
            const float t = r.f32(), red = r.f32(), green = r.f32(), blue = r.f32();
            out.color.push_back({t, red, green, blue});
        }
    }
    const std::vector<const IffNode*> particle_waves = particle.children_with("WVFM");
    if (particle_waves.size() >= 2) {
        out.alpha = read_waveform(*particle_waves[0]);
        out.speed_scale = read_waveform(*particle_waves[1]);
    }
    const IffNode& shape = quad.expect("0001");
    const std::vector<const IffNode*> shape_waves = shape.children_with("WVFM");
    if (shape_waves.size() >= 2) {
        out.size = read_waveform(*shape_waves[1]);
    }
    if (const IffNode* texture = shape.child("PTEX")) {
        IffReader r(texture->expect("0000").data);
        out.shader = normalize_path(r.cstring());
        if (r.remaining() >= 24) {
            out.frames = int(std::max<uint32_t>(1, r.u32()));
            r.u32();  // first frame
            r.u32();  // last
            r.f32();  // a frame's size in the texture (1 / grid)
            out.grid = int(std::max<uint32_t>(1, r.u32()));
            out.fps = r.f32();
        }
    }
    return out;
}

void collect(const IffNode& node, std::vector<ParticleEmitter>& out) {
    for (const IffNode& child : node.children) {
        if (!child.form) {
            continue;
        }
        if (child.tag == "EMTR") {
            out.push_back(read_emitter(child));
        } else {
            collect(child, out);
        }
    }
}

} // namespace

std::vector<ParticleEmitter> parse_particle_effect(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag != "PEFT") {
        throw std::runtime_error("not a particle effect (" + root.tag + ")");
    }
    std::vector<ParticleEmitter> out;
    collect(root, out);
    return out;
}

} // namespace swm::swg
