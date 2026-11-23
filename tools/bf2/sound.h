#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace swm::bf2 {

// 32-bit FNV-1a over `c | 0x20`, used for all Battlefront II sound tags,
// keywords, and sound/sample/stream names.
uint32_t sound_name_hash(std::string_view name);

// Reference to a single audio clip inside a SampleBank (.bnk/.lvl) or
// Stream (emo_ inside .lvl).
struct SoundClipRef {
    std::filesystem::path file;
    uint64_t offset = 0;         // byte offset of clip audio payload in `file`
    uint32_t byte_size = 0;      // encoded/raw payload size in bytes
    uint32_t sample_rate = 22050;
    uint32_t num_samples = 0;    // PCM frames per channel
    uint16_t channels = 1;       // 1 = mono, 2 = stereo
    uint16_t format = 2;         // 2 = PCM16 LE, 5 = IMA ADPCM
    uint32_t substreams = 1;     // 1 = mono or MS-interleaved stereo; 2 = planar interleaved stereo
    uint32_t interleave = 4;     // interleave block size in bytes for substreams == 2
    uint32_t stream_hash = 0;    // 0 for SampleBank clips
    uint32_t sample_hash = 0;
};

// Resolved or raw sound property record from `snd_` chunks
// (SoundProperties, SoundStreamProperties, SoundStream, SoundLayered).
struct SoundProp {
    enum class Kind { sample, stream, layered };

    Kind kind = Kind::sample;
    uint32_t name_hash = 0;
    uint32_t inherit_hash = 0;

    float gain = 1.0f;
    float gain_dev = 0.0f;
    float pitch = 1.0f;
    float pitch_dev = 0.0f;
    float min_distance = 10.0f;
    float max_distance = 100.0f;
    float mute_distance = 100.0f;
    float rolloff = 1.0f;
    float play_probability = 1.0f;
    bool looping = false;
    bool mode_3d = true;
    uint32_t bus_hash = 0;
    uint32_t stream_hash = 0;

    bool has_gain = false;
    bool has_gain_dev = false;
    bool has_pitch = false;
    bool has_pitch_dev = false;
    bool has_min_distance = false;
    bool has_max_distance = false;
    bool has_mute_distance = false;
    bool has_rolloff = false;
    bool has_play_probability = false;
    bool has_looping = false;
    bool has_mode_3d = false;
    bool has_bus = false;
    bool has_stream = false;

    // Sample or segment hashes with playback weights.
    std::vector<std::pair<uint32_t, float>> entries;

    struct Layer {
        uint32_t prop_hash = 0;
        float delay = 0.0f;
    };
    std::vector<Layer> layers;
};

// Streaming/seeking index of Battlefront II sound banks (.bnk), streams (emo_),
// and sound property configs (snd_).
class SoundLibrary {
public:
    // Scans a .bnk or .lvl file without loading audio payloads into memory.
    void add_file(const std::filesystem::path& path);

    // Resolves a sound property by name hash, walking any Inherit chain.
    std::optional<SoundProp> resolve(uint32_t name_hash) const;

    const SoundClipRef* find_sample(uint32_t sample_hash) const;
    const SoundClipRef* find_segment(uint32_t stream_hash, uint32_t seg_hash) const;

    size_t sample_count() const { return samples_.size(); }
    size_t segment_count() const { return segments_.size(); }
    size_t prop_count() const { return props_.size(); }

private:
    std::map<uint32_t, SoundClipRef> samples_;
    std::map<std::pair<uint32_t, uint32_t>, SoundClipRef> segments_;
    std::map<uint32_t, SoundClipRef> segments_by_hash_;
    std::map<uint32_t, SoundProp> props_;
};

// Decodes Battlefront II Format 5 (IMA ADPCM) audio into interleaved signed
// 16-bit PCM samples.
std::vector<int16_t> decode_ima_adpcm(std::span<const uint8_t> data, uint16_t channels, uint32_t substreams,
                                      uint32_t interleave, uint32_t expected_samples);

// Reads and decodes a SoundClipRef from disk into interleaved signed 16-bit PCM.
std::vector<int16_t> decode_clip(const SoundClipRef& clip);

// Writes a standard 44-byte RIFF/WAVE PCM16 file.
void write_wav(const std::filesystem::path& path, std::span<const int16_t> pcm, uint32_t sample_rate, uint16_t channels);

} // namespace swm::bf2
