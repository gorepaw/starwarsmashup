#include "bf2/sound.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace swm::bf2 {

namespace {

uint32_t read_u32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

int16_t read_i16(const uint8_t* p) {
    int16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

float read_f32(const uint8_t* p) {
    float v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint64_t align4(uint64_t v) {
    return (v + 3) & ~uint64_t(3);
}

// Known hashes for SampleBank / emo_ Info headers.
constexpr uint32_t TAG_UCFB = 0x62666375;  // "ucfb"
constexpr uint32_t TAG_LVL  = 0x5f6c766c;  // "lvl_"
constexpr uint32_t TAG_SND  = 0x5f646e73;  // "snd_"
constexpr uint32_t TAG_EMO  = 0x5f6f6d65;  // "emo_"
constexpr uint32_t TAG_DATA = 0x41544144;  // "DATA"
constexpr uint32_t TAG_SCOP = 0x504f4353;  // "SCOP"

constexpr uint32_t HASH_SAMPLE_BANK_WRAPPER = 0x5e262256;
constexpr uint32_t HASH_SAMPLE_BANK         = 0x9a644d15;
constexpr uint32_t HASH_STREAM_LIST         = 0x5bb97f21;
constexpr uint32_t HASH_INFO                = 0x0fb40705;
constexpr uint32_t HASH_BANK_END            = 0x8ebf7143;
constexpr uint32_t HASH_STREAM_END          = 0xb969be96;
constexpr uint32_t HASH_ENTRY_START         = 0x96e382a7;
constexpr uint32_t HASH_AUDIO_DATA          = 0xd872e2a5;

constexpr uint32_t HASH_NAME        = 0x8d39bde6;
constexpr uint32_t HASH_FORMAT      = 0xb99d8552;
constexpr uint32_t HASH_CHANNELS    = 0x7816084b;
constexpr uint32_t HASH_NUM_ITEMS   = 0x40fbdebd;
constexpr uint32_t HASH_NUM_SAMPLES = 0x98b889ce;
constexpr uint32_t HASH_SUBSTREAMS  = 0x7aaf1a1c;
constexpr uint32_t HASH_INTERLEAVE  = 0x740fdb0c;

constexpr std::array<int, 89> IMA_STEP_TABLE = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

constexpr std::array<int, 16> IMA_INDEX_TABLE = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

int16_t decode_nibble(uint8_t nibble, int& pred, int& idx) {
    const int step = IMA_STEP_TABLE[size_t(idx)];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    pred = std::clamp((nibble & 8) ? (pred - diff) : (pred + diff), -32768, 32767);
    idx = std::clamp(idx + IMA_INDEX_TABLE[nibble & 0x0f], 0, 88);
    return int16_t(pred);
}

// Reads a 48-byte sample/segment entry from an Info chunk and advances `cur_offset`.
bool parse_clip_entry(const uint8_t* p, const std::filesystem::path& file, uint64_t data_base_offset,
                      uint32_t stream_hash, uint16_t channels, uint16_t format, uint32_t substreams,
                      uint32_t interleave, uint64_t& cur_offset, SoundClipRef& out) {
    if (read_u32(p) != HASH_ENTRY_START) {
        return false;
    }
    const uint32_t sample_hash = read_u32(p + 8);
    const uint32_t sample_rate = read_u32(p + 16);
    const uint32_t byte_size = read_u32(p + 24);
    const uint32_t num_samples = read_u32(p + 32);
    const uint32_t padding = read_u32(p + 40);

    out.file = file;
    out.offset = data_base_offset + cur_offset;
    out.byte_size = byte_size;
    out.sample_rate = sample_rate > 0 ? sample_rate : 22050;
    out.num_samples = num_samples;
    out.channels = channels;
    out.format = format;
    out.substreams = substreams;
    out.interleave = interleave;
    out.stream_hash = stream_hash;
    out.sample_hash = sample_hash;

    cur_offset += uint64_t(byte_size) + uint64_t(padding);
    return true;
}

struct CfgChunk {
    uint32_t tag = 0;
    std::span<const uint8_t> payload;
};

std::vector<CfgChunk> read_cfg_chunks(std::span<const uint8_t> buf) {
    std::vector<CfgChunk> out;
    size_t pos = 0;
    while (pos + 8 <= buf.size()) {
        const uint32_t tag = read_u32(buf.data() + pos);
        const uint32_t sz = read_u32(buf.data() + pos + 4);
        if (sz > buf.size() - pos - 8) {
            break;
        }
        out.push_back({tag, buf.subspan(pos + 8, sz)});
        pos = size_t(std::min<uint64_t>(buf.size(), align4(pos + 8 + sz)));
    }
    return out;
}

struct DataFields {
    uint32_t keyword = 0;
    std::vector<uint32_t> u32s;
    std::vector<float> f32s;
};

bool parse_data_fields(std::span<const uint8_t> payload, DataFields& out) {
    if (payload.size() < 5) {
        return false;
    }
    out.keyword = read_u32(payload.data());
    const uint8_t count = payload[4];
    if (5 + size_t(count) * 4 > payload.size()) {
        return false;
    }
    out.u32s.resize(count);
    out.f32s.resize(count);
    for (size_t i = 0; i < count; ++i) {
        out.u32s[i] = read_u32(payload.data() + 5 + i * 4);
        out.f32s[i] = read_f32(payload.data() + 5 + i * 4);
    }
    return true;
}

void parse_snd_scop(std::span<const uint8_t> scop, SoundProp& prop) {
    static const uint32_t H_Name = sound_name_hash("Name");
    static const uint32_t H_Inherit = sound_name_hash("Inherit");
    static const uint32_t H_Gain = sound_name_hash("Gain");
    static const uint32_t H_GainDev = sound_name_hash("GainDev");
    static const uint32_t H_Pitch = sound_name_hash("Pitch");
    static const uint32_t H_PitchDev = sound_name_hash("PitchDev");
    static const uint32_t H_MinDistance = sound_name_hash("MinDistance");
    static const uint32_t H_MaxDistance = sound_name_hash("MaxDistance");
    static const uint32_t H_MuteDistance = sound_name_hash("MuteDistance");
    static const uint32_t H_RollOff = sound_name_hash("RollOff");
    static const uint32_t H_Looping = sound_name_hash("Looping");
    static const uint32_t H_Mode3D = sound_name_hash("Mode3D");
    static const uint32_t H_PlayProbability = sound_name_hash("PlayProbability");
    static const uint32_t H_Bus = sound_name_hash("Bus");
    static const uint32_t H_Stream = sound_name_hash("Stream");
    static const uint32_t H_SampleList = sound_name_hash("SampleList");
    static const uint32_t H_SegmentList = sound_name_hash("SegmentList");
    static const uint32_t H_Sample = sound_name_hash("Sample");
    static const uint32_t H_Segment = sound_name_hash("Segment");
    static const uint32_t H_Layer = sound_name_hash("Layer");

    const auto chunks = read_cfg_chunks(scop);
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].tag != TAG_DATA) {
            continue;
        }
        DataFields df;
        if (!parse_data_fields(chunks[i].payload, df) || df.u32s.empty()) {
            // Check for list headers with count == 0
            if (parse_data_fields(chunks[i].payload, df) &&
                (df.keyword == H_SampleList || df.keyword == H_SegmentList) &&
                i + 1 < chunks.size() && chunks[i + 1].tag == TAG_SCOP) {
                for (const CfgChunk& sub : read_cfg_chunks(chunks[i + 1].payload)) {
                    if (sub.tag != TAG_DATA) {
                        continue;
                    }
                    DataFields sf;
                    if (parse_data_fields(sub.payload, sf) && !sf.u32s.empty() &&
                        (sf.keyword == H_Sample || sf.keyword == H_Segment)) {
                        const float weight = sf.f32s.size() >= 2 ? sf.f32s[1] : 1.0f;
                        prop.entries.emplace_back(sf.u32s[0], weight);
                    }
                }
                ++i;
            }
            continue;
        }
        if (df.keyword == H_Name) {
            prop.name_hash = df.u32s[0];
        } else if (df.keyword == H_Inherit) {
            prop.inherit_hash = df.u32s[0];
        } else if (df.keyword == H_Gain) {
            prop.gain = df.f32s[0];
            prop.has_gain = true;
        } else if (df.keyword == H_GainDev) {
            prop.gain_dev = df.f32s[0];
            prop.has_gain_dev = true;
        } else if (df.keyword == H_Pitch) {
            prop.pitch = df.f32s[0];
            prop.has_pitch = true;
        } else if (df.keyword == H_PitchDev) {
            prop.pitch_dev = df.f32s[0];
            prop.has_pitch_dev = true;
        } else if (df.keyword == H_MinDistance) {
            prop.min_distance = df.f32s[0];
            prop.has_min_distance = true;
        } else if (df.keyword == H_MaxDistance) {
            prop.max_distance = df.f32s[0];
            prop.has_max_distance = true;
        } else if (df.keyword == H_MuteDistance) {
            prop.mute_distance = df.f32s[0];
            prop.has_mute_distance = true;
        } else if (df.keyword == H_RollOff) {
            prop.rolloff = df.f32s[0];
            prop.has_rolloff = true;
        } else if (df.keyword == H_Looping) {
            prop.looping = df.f32s[0] != 0.0f;
            prop.has_looping = true;
        } else if (df.keyword == H_Mode3D) {
            prop.mode_3d = df.f32s[0] != 0.0f;
            prop.has_mode_3d = true;
        } else if (df.keyword == H_PlayProbability) {
            prop.play_probability = df.f32s[0];
            prop.has_play_probability = true;
        } else if (df.keyword == H_Bus) {
            prop.bus_hash = df.u32s[0];
            prop.has_bus = true;
        } else if (df.keyword == H_Stream) {
            prop.stream_hash = df.u32s[0];
            prop.has_stream = true;
        } else if (df.keyword == H_Layer && df.u32s.size() >= 2) {
            const float delay = df.f32s.size() >= 3 ? df.f32s[2] : 0.0f;
            prop.layers.push_back({df.u32s[1], delay, df.u32s[0]});
        }
    }
}

// A SoundParameterized's scope: Name, SoundLayered, and Layer() scopes of a
// Name and ParameterGraph(parameter, graph, target) entries.
void parse_parameterized_scop(std::span<const uint8_t> scop, SoundParameterized& out) {
    static const uint32_t H_Name = sound_name_hash("Name");
    static const uint32_t H_SoundLayered = sound_name_hash("SoundLayered");
    static const uint32_t H_Layer = sound_name_hash("Layer");
    static const uint32_t H_ParameterGraph = sound_name_hash("ParameterGraph");
    const auto chunks = read_cfg_chunks(scop);
    for (size_t i = 0; i < chunks.size(); ++i) {
        DataFields df;
        if (chunks[i].tag != TAG_DATA || !parse_data_fields(chunks[i].payload, df)) {
            continue;
        }
        if (df.keyword == H_Name && !df.u32s.empty()) {
            out.name_hash = df.u32s[0];
        } else if (df.keyword == H_SoundLayered && !df.u32s.empty()) {
            out.layered_hash = df.u32s[0];
        } else if (df.keyword == H_Layer && i + 1 < chunks.size() && chunks[i + 1].tag == TAG_SCOP) {
            SoundParameterized::Layer layer;
            for (const CfgChunk& sub : read_cfg_chunks(chunks[i + 1].payload)) {
                DataFields sf;
                if (sub.tag != TAG_DATA || !parse_data_fields(sub.payload, sf)) {
                    continue;
                }
                if (sf.keyword == H_Name && !sf.u32s.empty()) {
                    layer.name_hash = sf.u32s[0];
                } else if (sf.keyword == H_ParameterGraph && sf.u32s.size() >= 3) {
                    layer.controls.push_back({sf.u32s[0], sf.u32s[1], sf.u32s[2]});
                }
            }
            out.layers.push_back(std::move(layer));
            ++i;
        }
    }
}

// A ParameterGraph record's scope: Name and its ControlPoint(x, y)s.
std::pair<uint32_t, std::vector<std::pair<float, float>>> parse_graph_scop(std::span<const uint8_t> scop) {
    static const uint32_t H_Name = sound_name_hash("Name");
    static const uint32_t H_ControlPoint = sound_name_hash("ControlPoint");
    std::pair<uint32_t, std::vector<std::pair<float, float>>> out;
    for (const CfgChunk& chunk : read_cfg_chunks(scop)) {
        DataFields df;
        if (chunk.tag != TAG_DATA || !parse_data_fields(chunk.payload, df)) {
            continue;
        }
        if (df.keyword == H_Name && !df.u32s.empty()) {
            out.first = df.u32s[0];
        } else if (df.keyword == H_ControlPoint && df.f32s.size() >= 2) {
            out.second.emplace_back(df.f32s[0], df.f32s[1]);
        }
    }
    return out;
}

} // namespace

uint32_t sound_name_hash(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash ^= uint32_t(uint8_t(c) | 0x20u);
        hash *= 16777619u;
    }
    return hash;
}

void SoundLibrary::add_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("sound: cannot open " + path.string());
    }
    uint8_t root_hdr[8]{};
    in.read(reinterpret_cast<char*>(root_hdr), 8);
    if (!in || read_u32(root_hdr) != TAG_UCFB) {
        throw std::runtime_error("sound: not a ucfb file: " + path.string());
    }
    const uint64_t root_end = 8 + uint64_t(read_u32(root_hdr + 4));

    auto parse_bank_or_stream = [&](uint64_t chunk_start, uint64_t chunk_end, bool is_stream) {
        in.seekg(std::streamoff(chunk_start));
        uint8_t inner_hdr[16]{};
        if (chunk_end - chunk_start < 16) {
            return;
        }
        in.read(reinterpret_cast<char*>(inner_hdr), 16);
        const uint32_t inner_tag = read_u32(inner_hdr);
        const uint32_t info_tag = read_u32(inner_hdr + 8);
        const uint32_t info_size = read_u32(inner_hdr + 12);
        if (is_stream && inner_tag != TAG_EMO) {
            return;
        }
        if (!is_stream && inner_tag != HASH_SAMPLE_BANK) {
            return;
        }
        if (info_tag != HASH_INFO || chunk_start + 16 + info_size > chunk_end) {
            return;
        }

        std::vector<uint8_t> info(info_size);
        in.read(reinterpret_cast<char*>(info.data()), std::streamsize(info_size));
        if (!in) {
            return;
        }

        // Data chunk header follows Info immediately (8 bytes: 0xd872e2a5 + data_size).
        const uint64_t data_hdr_pos = chunk_start + 16 + info_size;
        if (data_hdr_pos + 8 > chunk_end) {
            return;
        }
        in.seekg(std::streamoff(data_hdr_pos));
        uint8_t data_hdr[8]{};
        in.read(reinterpret_cast<char*>(data_hdr), 8);
        if (read_u32(data_hdr) != HASH_AUDIO_DATA) {
            return;
        }
        const uint64_t data_base = data_hdr_pos + 8;

        // Parse header fields up to 4-byte end marker.
        const uint32_t end_marker = is_stream ? HASH_STREAM_END : HASH_BANK_END;
        uint32_t stream_hash = 0;
        uint16_t format = is_stream ? 5 : 2;
        uint16_t channels = 1;
        uint32_t substreams = 1;
        uint32_t interleave = 4;
        size_t pos = 0;
        while (pos + 4 <= info.size()) {
            const uint32_t key = read_u32(info.data() + pos);
            if (key == end_marker) {
                pos += 4;
                break;
            }
            if (pos + 8 > info.size()) {
                return;
            }
            const uint32_t val = read_u32(info.data() + pos + 4);
            if (key == HASH_NAME) stream_hash = val;
            else if (key == HASH_FORMAT) format = uint16_t(val);
            else if (key == HASH_CHANNELS) channels = uint16_t(val);
            else if (key == HASH_SUBSTREAMS) substreams = val;
            else if (key == HASH_INTERLEAVE) interleave = val;
            pos += 8;
        }

        uint64_t cur_offset = 0;
        while (pos + 48 <= info.size()) {
            SoundClipRef clip;
            if (!parse_clip_entry(info.data() + pos, path, data_base, is_stream ? stream_hash : 0, channels, format,
                                  substreams, interleave, cur_offset, clip)) {
                break;
            }
            if (is_stream) {
                segments_.try_emplace({stream_hash, clip.sample_hash}, clip);
                segments_by_hash_.try_emplace(clip.sample_hash, clip);
            } else {
                samples_.try_emplace(clip.sample_hash, clip);
            }
            pos += 48;
        }
    };

    auto parse_snd_chunk = [&](uint64_t chunk_start, uint64_t chunk_size) {
        static const uint32_t H_SoundProperties = sound_name_hash("SoundProperties");
        static const uint32_t H_SoundStreamProperties = sound_name_hash("SoundStreamProperties");
        static const uint32_t H_SoundStream = sound_name_hash("SoundStream");
        static const uint32_t H_SoundLayered = sound_name_hash("SoundLayered");
        static const uint32_t H_SoundParameterized = sound_name_hash("SoundParameterized");
        static const uint32_t H_ParameterGraph = sound_name_hash("ParameterGraph");

        std::vector<uint8_t> buf(chunk_size);
        in.seekg(std::streamoff(chunk_start));
        in.read(reinterpret_cast<char*>(buf.data()), std::streamsize(chunk_size));
        if (!in) {
            return;
        }
        const auto chunks = read_cfg_chunks(buf);
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (chunks[i].tag != TAG_DATA || i + 1 >= chunks.size() || chunks[i + 1].tag != TAG_SCOP) {
                continue;
            }
            DataFields df;
            if (!parse_data_fields(chunks[i].payload, df)) {
                continue;
            }
            if (df.keyword == H_SoundParameterized) {
                SoundParameterized p;
                parse_parameterized_scop(chunks[i + 1].payload, p);
                if (p.name_hash != 0) {
                    parameterized_.try_emplace(p.name_hash, std::move(p));
                }
                ++i;
                continue;
            }
            if (df.keyword == H_ParameterGraph) {
                auto [name, points] = parse_graph_scop(chunks[i + 1].payload);
                if (name != 0 && !points.empty()) {
                    graphs_.try_emplace(name, std::move(points));
                }
                ++i;
                continue;
            }
            SoundProp prop;
            if (df.keyword == H_SoundProperties) {
                prop.kind = SoundProp::Kind::sample;
            } else if (df.keyword == H_SoundStreamProperties || df.keyword == H_SoundStream) {
                prop.kind = SoundProp::Kind::stream;
            } else if (df.keyword == H_SoundLayered) {
                prop.kind = SoundProp::Kind::layered;
            } else {
                continue;
            }
            parse_snd_scop(chunks[i + 1].payload, prop);
            if (prop.name_hash != 0) {
                auto it = props_.find(prop.name_hash);
                if (it == props_.end()) {
                    props_.emplace(prop.name_hash, std::move(prop));
                } else if (it->second.entries.empty() && !prop.entries.empty()) {
                    it->second = std::move(prop);
                }
            }
            ++i;
        }
    };

    auto walk = [&](auto& self, uint64_t start, uint64_t end) -> void {
        uint64_t pos = start;
        while (pos + 8 <= end) {
            in.seekg(std::streamoff(pos));
            uint8_t hdr[8]{};
            in.read(reinterpret_cast<char*>(hdr), 8);
            if (!in) {
                break;
            }
            const uint32_t tag = read_u32(hdr);
            const uint32_t sz = read_u32(hdr + 4);
            const uint64_t payload_start = pos + 8;
            const uint64_t payload_end = std::min(end, payload_start + uint64_t(sz));

            if (tag == TAG_LVL && payload_end - payload_start >= 8) {
                uint8_t lhdr[8]{};
                in.read(reinterpret_cast<char*>(lhdr), 8);
                const uint32_t inner_sz = read_u32(lhdr + 4);
                self(self, payload_start + 8, std::min(payload_end, payload_start + 8 + uint64_t(inner_sz)));
            } else if (tag == HASH_SAMPLE_BANK_WRAPPER) {
                parse_bank_or_stream(payload_start, payload_end, false);
            } else if (tag == HASH_STREAM_LIST) {
                parse_bank_or_stream(payload_start, payload_end, true);
            } else if (tag == TAG_SND) {
                parse_snd_chunk(payload_start, payload_end - payload_start);
            }

            const uint64_t next = align4(payload_start + uint64_t(sz));
            if (next <= pos) {
                break;
            }
            pos = next;
        }
    };

    walk(walk, 8, root_end);
}

std::optional<SoundProp> SoundLibrary::resolve(uint32_t name_hash) const {
    std::vector<const SoundProp*> chain;
    uint32_t cur = name_hash;
    for (int depth = 0; depth < 16 && cur != 0; ++depth) {
        const auto it = props_.find(cur);
        if (it == props_.end()) {
            break;
        }
        chain.push_back(&it->second);
        if (it->second.inherit_hash == cur) {
            break;
        }
        cur = it->second.inherit_hash;
    }
    if (chain.empty()) {
        return std::nullopt;
    }

    SoundProp out = *chain.back();
    out.name_hash = chain.front()->name_hash;
    out.kind = chain.front()->kind;
    for (auto it = chain.rbegin() + 1; it != chain.rend(); ++it) {
        const SoundProp& p = **it;
        if (p.has_gain) { out.gain = p.gain; out.has_gain = true; }
        if (p.has_gain_dev) { out.gain_dev = p.gain_dev; out.has_gain_dev = true; }
        if (p.has_pitch) { out.pitch = p.pitch; out.has_pitch = true; }
        if (p.has_pitch_dev) { out.pitch_dev = p.pitch_dev; out.has_pitch_dev = true; }
        if (p.has_min_distance) { out.min_distance = p.min_distance; out.has_min_distance = true; }
        if (p.has_max_distance) { out.max_distance = p.max_distance; out.has_max_distance = true; }
        if (p.has_mute_distance) { out.mute_distance = p.mute_distance; out.has_mute_distance = true; }
        if (p.has_rolloff) { out.rolloff = p.rolloff; out.has_rolloff = true; }
        if (p.has_play_probability) { out.play_probability = p.play_probability; out.has_play_probability = true; }
        if (p.has_looping) { out.looping = p.looping; out.has_looping = true; }
        if (p.has_mode_3d) { out.mode_3d = p.mode_3d; out.has_mode_3d = true; }
        if (p.has_bus) { out.bus_hash = p.bus_hash; out.has_bus = true; }
        if (p.has_stream) { out.stream_hash = p.stream_hash; out.has_stream = true; }
        if (!p.entries.empty()) { out.entries = p.entries; }
        if (!p.layers.empty()) { out.layers = p.layers; }
    }
    return out;
}

const SoundParameterized* SoundLibrary::find_parameterized(uint32_t name_hash) const {
    const auto it = parameterized_.find(name_hash);
    return it == parameterized_.end() ? nullptr : &it->second;
}

const std::vector<std::pair<float, float>>* SoundLibrary::find_graph(uint32_t name_hash) const {
    const auto it = graphs_.find(name_hash);
    return it == graphs_.end() ? nullptr : &it->second;
}

const SoundClipRef* SoundLibrary::find_sample(uint32_t sample_hash) const {
    const auto it = samples_.find(sample_hash);
    return it == samples_.end() ? nullptr : &it->second;
}

const SoundClipRef* SoundLibrary::find_segment(uint32_t stream_hash, uint32_t seg_hash) const {
    if (stream_hash != 0) {
        const auto it = segments_.find({stream_hash, seg_hash});
        if (it != segments_.end()) {
            return &it->second;
        }
    }
    const auto it = segments_by_hash_.find(seg_hash);
    return it == segments_by_hash_.end() ? nullptr : &it->second;
}

std::vector<int16_t> decode_ima_adpcm(std::span<const uint8_t> data, uint16_t channels, uint32_t substreams,
                                      uint32_t interleave, uint32_t expected_samples) {
    std::vector<int16_t> out;
    if (channels == 1) {
        out.reserve(expected_samples > 0 ? expected_samples : (data.size() / 36) * 64);
        for (size_t pos = 0; pos + 36 <= data.size(); pos += 36) {
            int pred = read_i16(data.data() + pos);
            int idx = std::clamp<int>(data[pos + 2], 0, 88);
            for (size_t i = 4; i < 36; ++i) {
                const uint8_t byte = data[pos + i];
                out.push_back(decode_nibble(byte & 0x0f, pred, idx));
                out.push_back(decode_nibble(byte >> 4, pred, idx));
            }
        }
    } else if (channels == 2 && substreams <= 1) {
        out.reserve(expected_samples > 0 ? size_t(expected_samples) * 2 : (data.size() / 72) * 128);
        for (size_t pos = 0; pos + 72 <= data.size(); pos += 72) {
            int pred_l = read_i16(data.data() + pos);
            int idx_l = std::clamp<int>(data[pos + 2], 0, 88);
            int pred_r = read_i16(data.data() + pos + 4);
            int idx_r = std::clamp<int>(data[pos + 6], 0, 88);
            for (size_t w = 0; w < 8; ++w) {
                std::array<int16_t, 8> left{};
                std::array<int16_t, 8> right{};
                const uint8_t* lp = data.data() + pos + 8 + w * 8;
                const uint8_t* rp = lp + 4;
                for (size_t b = 0; b < 4; ++b) {
                    left[b * 2] = decode_nibble(lp[b] & 0x0f, pred_l, idx_l);
                    left[b * 2 + 1] = decode_nibble(lp[b] >> 4, pred_l, idx_l);
                    right[b * 2] = decode_nibble(rp[b] & 0x0f, pred_r, idx_r);
                    right[b * 2 + 1] = decode_nibble(rp[b] >> 4, pred_r, idx_r);
                }
                for (size_t s = 0; s < 8; ++s) {
                    out.push_back(left[s]);
                    out.push_back(right[s]);
                }
            }
        }
    } else if (channels == 2 && substreams == 2 && interleave >= 36) {
        std::vector<int16_t> left;
        std::vector<int16_t> right;
        const size_t chunk = interleave;
        for (size_t pos = 0; pos + chunk * 2 <= data.size(); pos += chunk * 2) {
            for (size_t b = 0; b + 36 <= chunk; b += 36) {
                int pred = read_i16(data.data() + pos + b);
                int idx = std::clamp<int>(data[pos + b + 2], 0, 88);
                for (size_t i = 4; i < 36; ++i) {
                    const uint8_t byte = data[pos + b + i];
                    left.push_back(decode_nibble(byte & 0x0f, pred, idx));
                    left.push_back(decode_nibble(byte >> 4, pred, idx));
                }
            }
            for (size_t b = 0; b + 36 <= chunk; b += 36) {
                int pred = read_i16(data.data() + pos + chunk + b);
                int idx = std::clamp<int>(data[pos + chunk + b + 2], 0, 88);
                for (size_t i = 4; i < 36; ++i) {
                    const uint8_t byte = data[pos + chunk + b + i];
                    right.push_back(decode_nibble(byte & 0x0f, pred, idx));
                    right.push_back(decode_nibble(byte >> 4, pred, idx));
                }
            }
        }
        const size_t frames = std::min(left.size(), right.size());
        out.reserve(frames * 2);
        for (size_t i = 0; i < frames; ++i) {
            out.push_back(left[i]);
            out.push_back(right[i]);
        }
    }

    if (expected_samples > 0) {
        const size_t target = size_t(expected_samples) * channels;
        if (out.size() > target) {
            out.resize(target);
        }
    }
    return out;
}

std::vector<int16_t> decode_clip(const SoundClipRef& clip) {
    std::ifstream in(clip.file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("sound: cannot open " + clip.file.string());
    }
    in.seekg(std::streamoff(clip.offset));
    std::vector<uint8_t> raw(clip.byte_size);
    in.read(reinterpret_cast<char*>(raw.data()), std::streamsize(clip.byte_size));
    if (!in) {
        throw std::runtime_error("sound: short read in " + clip.file.string());
    }
    if (clip.format == 2) {
        const size_t count = raw.size() / 2;
        std::vector<int16_t> pcm(count);
        std::memcpy(pcm.data(), raw.data(), count * 2);
        return pcm;
    }
    if (clip.format == 5) {
        return decode_ima_adpcm(raw, clip.channels, clip.substreams, clip.interleave, clip.num_samples);
    }
    throw std::runtime_error("sound: unsupported format " + std::to_string(clip.format));
}

void write_wav(const std::filesystem::path& path, std::span<const int16_t> pcm, uint32_t sample_rate,
               uint16_t channels) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("sound: cannot write " + path.string());
    }
    const uint32_t data_bytes = uint32_t(pcm.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_bytes;
    const uint16_t block_align = uint16_t(channels * 2);
    const uint32_t byte_rate = sample_rate * block_align;
    const uint16_t audio_format = 1;  // PCM
    const uint32_t fmt_size = 16;
    const uint16_t bits_per_sample = 16;

    auto write_u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    auto write_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };

    out.write("RIFF", 4);
    write_u32(riff_size);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32(fmt_size);
    write_u16(audio_format);
    write_u16(channels);
    write_u32(sample_rate);
    write_u32(byte_rate);
    write_u16(block_align);
    write_u16(bits_per_sample);
    out.write("data", 4);
    write_u32(data_bytes);
    if (data_bytes > 0) {
        out.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(data_bytes));
    }
}

} // namespace swm::bf2
