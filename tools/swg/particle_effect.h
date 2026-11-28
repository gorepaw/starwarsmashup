#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace swbfo::swg {

// A value over time (WVFM 0001): two flags (not read), a count, then each
// control point as four floats: when (0..1 through the emitter's loop, or
// a particle's life), the value, and how far it may randomly fall below
// and rise above it.
struct Waveform {
    struct Point {
        float t = 0, value = 0, random_below = 0, random_above = 0;
    };
    std::vector<Point> points;

    float mean() const;  // of the points' values
    float low() const;   // the least it reaches, randomly too
    float high() const;  // the most
};

// One emitter of a particle effect (.prt: PEFT > EMGP > EMTR 0011/0012).
// Not documented: read from Galaxies' effects by comparing them (steam,
// smoke, a campfire's smoke, embers and fire). ASSUMPTION, every field
// named below: an emitter's waveforms come in a fixed order, and these are
// the ones whose values fit their effects (a fire's 4 particles a second
// living 1 s, its smoke's 23 living 3 s, rising).
struct ParticleEmitter {
    std::string name;
    Waveform radius;    // 7th: meters around the emitter they start in
    Waveform spread;    // 9th: degrees off straight up they set off at
    Waveform rate;      // 10th: particles a second
    Waveform speed;     // 11th: meters a second they set off at
    Waveform lifetime;  // 15th: seconds
    Waveform gravity;   // 16th: times the world's; below 0, they rise
    // The particle (PTQD > PTCL 0002, and the quad's 0001), over its life:
    std::vector<std::array<float, 4>> color;  // CLRR: (t, r, g, b)
    Waveform alpha;                            // PTCL's first waveform
    Waveform speed_scale;                      // its second: times `speed`
    Waveform size;                             // the quad's second: meters across
    // Its texture (PTEX): a shader (.sht), and as a sprite sheet: frames,
    // `grid` across and down, played at `fps` (below 0: one frame).
    std::string shader;
    int frames = 1, grid = 1;
    float fps = -1;
};

// Throws std::runtime_error on malformed data.
std::vector<ParticleEmitter> parse_particle_effect(std::span<const uint8_t> bytes);

} // namespace swbfo::swg
