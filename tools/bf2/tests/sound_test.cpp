#include "bf2/sound.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

using namespace swm::bf2;

void push_u32(std::vector<uint8_t>& out, uint32_t v) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &v, 4);
}

void push_i16(std::vector<uint8_t>& out, int16_t v) {
    const size_t at = out.size();
    out.resize(at + 2);
    std::memcpy(out.data() + at, &v, 2);
}

void push_f32(std::vector<uint8_t>& out, float v) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &v, 4);
}

void push_tag(std::vector<uint8_t>& out, const char* tag) {
    out.insert(out.end(), tag, tag + 4);
}

std::vector<uint8_t> make_chunk(uint32_t tag, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    push_u32(out, tag);
    push_u32(out, uint32_t(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
    return out;
}

std::vector<uint8_t> make_chunk_str(const char* tag, const std::vector<uint8_t>& payload) {
    uint32_t t = 0;
    std::memcpy(&t, tag, 4);
    return make_chunk(t, payload);
}

std::vector<uint8_t> make_data_u32(std::string_view key, const std::vector<uint32_t>& vals) {
    std::vector<uint8_t> p;
    push_u32(p, sound_name_hash(key));
    p.push_back(uint8_t(vals.size()));
    for (uint32_t v : vals) {
        push_u32(p, v);
    }
    return make_chunk_str("DATA", p);
}

std::vector<uint8_t> make_data_f32(std::string_view key, const std::vector<float>& vals) {
    std::vector<uint8_t> p;
    push_u32(p, sound_name_hash(key));
    p.push_back(uint8_t(vals.size()));
    for (float v : vals) {
        push_f32(p, v);
    }
    return make_chunk_str("DATA", p);
}

void append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

void test_hashes() {
    assert(sound_name_hash("SampleBank") == 0x9a644d15u);
    assert(sound_name_hash("StreamList") == 0x5bb97f21u);
    assert(sound_name_hash("Info") == 0x0fb40705u);
    assert(sound_name_hash("Name") == 0x8d39bde6u);
    assert(sound_name_hash("Format") == 0xb99d8552u);
}

void test_adpcm_and_wav() {
    // 1 mono block (36 bytes -> 64 samples), initial pred=100, idx=10, nibbles=0x0
    std::vector<uint8_t> mono(36, 0);
    std::memcpy(mono.data(), "\x64\x00\x0a\x00", 4);
    const std::vector<int16_t> pcm_m = decode_ima_adpcm(mono, 1, 1, 4, 64);
    assert(pcm_m.size() == 64);
    assert(pcm_m[0] != 0);

    // 1 stereo MS-interleaved block (72 bytes -> 64 stereo frames = 128 samples)
    std::vector<uint8_t> stereo(72, 0);
    std::memcpy(stereo.data(), "\x00\x00\x05\x00\x00\x00\x05\x00", 8);
    const std::vector<int16_t> pcm_s = decode_ima_adpcm(stereo, 2, 1, 4, 64);
    assert(pcm_s.size() == 128);

    const auto wav_path = std::filesystem::path("swm_sound_test.wav");
    write_wav(wav_path, pcm_s, 44100, 2);
    std::ifstream in(wav_path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    in.close();
    std::filesystem::remove(wav_path);
    assert(bytes.size() == 44 + pcm_s.size() * 2);
    assert(std::memcmp(bytes.data(), "RIFF", 4) == 0);
    assert(std::memcmp(bytes.data() + 8, "WAVEfmt ", 8) == 0);
}

void test_library_bank_stream_and_inherit() {
    // Build a synthetic ucfb with:
    // 1. SampleBank (1 PCM16 mono clip: 4 samples)
    // 2. emo_ Stream (1 mono IMA ADPCM segment: 1 block = 36 bytes)
    // 3. snd_ with base SoundProperties, derived SoundProperties (Inherit), SoundStreamProperties, SoundLayered
    std::vector<uint8_t> ucfb_body;

    // SampleBank
    {
        const uint32_t sample_hash = sound_name_hash("test_sfx_clip");
        std::vector<uint8_t> info;
        push_u32(info, sound_name_hash("Name")); push_u32(info, sound_name_hash("test_bank"));
        push_u32(info, sound_name_hash("Format")); push_u32(info, 2);
        push_u32(info, 0x8ebf7143u);  // end marker
        push_u32(info, 0x96e382a7u); push_u32(info, 0x37386ae0u);
        push_u32(info, sample_hash); push_u32(info, 0x2fb31c01u);
        push_u32(info, 22050); push_u32(info, 0x23a0d95cu);
        push_u32(info, 8); push_u32(info, 0x1d48feefu);
        push_u32(info, 4); push_u32(info, 0x809608b6u);
        push_u32(info, 0); push_u32(info, 0x2e789fb4u);

        std::vector<uint8_t> data_pay;
        for (int16_t s : {int16_t(100), int16_t(200), int16_t(-100), int16_t(-200)}) {
            push_i16(data_pay, s);
        }
        std::vector<uint8_t> inner;
        append(inner, make_chunk(0x0fb40705u, info));
        append(inner, make_chunk(0xd872e2a5u, data_pay));
        append(ucfb_body, make_chunk(0x5e262256u, make_chunk(0x9a644d15u, inner)));
    }

    // emo_ stream
    {
        const uint32_t stream_hash = sound_name_hash("test_vo_stream");
        const uint32_t seg_hash = sound_name_hash("test_vo_seg");
        std::vector<uint8_t> info;
        push_u32(info, sound_name_hash("Name")); push_u32(info, stream_hash);
        push_u32(info, sound_name_hash("Format")); push_u32(info, 5);
        push_u32(info, sound_name_hash("Channels")); push_u32(info, 1);
        push_u32(info, sound_name_hash("Substreams")); push_u32(info, 1);
        push_u32(info, sound_name_hash("Interleave")); push_u32(info, 4);
        push_u32(info, 0xb969be96u);  // end marker
        push_u32(info, 0x96e382a7u); push_u32(info, 0x37386ae0u);
        push_u32(info, seg_hash); push_u32(info, 0x2fb31c01u);
        push_u32(info, 22050); push_u32(info, 0x23a0d95cu);
        push_u32(info, 36); push_u32(info, 0x1d48feefu);
        push_u32(info, 64); push_u32(info, 0x809608b6u);
        push_u32(info, 0); push_u32(info, 0x2e789fb4u);

        std::vector<uint8_t> data_pay(36, 0);
        std::vector<uint8_t> inner;
        append(inner, make_chunk(0x0fb40705u, info));
        append(inner, make_chunk(0xd872e2a5u, data_pay));
        append(ucfb_body, make_chunk(0x5bb97f21u, make_chunk_str("emo_", inner)));
    }

    // snd_ chunk
    {
        std::vector<uint8_t> snd;
        // Base prop
        std::vector<uint8_t> base_scop;
        append(base_scop, make_data_u32("Name", {sound_name_hash("base_sfx")}));
        append(base_scop, make_data_f32("MinDistance", {15.0f}));
        append(base_scop, make_data_f32("MaxDistance", {120.0f}));
        append(base_scop, make_data_f32("Gain", {0.75f}));
        append(snd, make_data_u32("SoundProperties", {}));
        append(snd, make_chunk_str("SCOP", base_scop));

        // Derived prop
        std::vector<uint8_t> child_scop;
        append(child_scop, make_data_u32("Name", {sound_name_hash("child_sfx")}));
        append(child_scop, make_data_u32("Inherit", {sound_name_hash("base_sfx")}));
        append(child_scop, make_data_f32("Gain", {0.5f}));
        std::vector<uint8_t> sample_list;
        std::vector<uint8_t> samp_p;
        push_u32(samp_p, sound_name_hash("Sample"));
        samp_p.push_back(2);
        push_u32(samp_p, sound_name_hash("test_sfx_clip"));
        push_f32(samp_p, 1.0f);
        append(sample_list, make_chunk_str("DATA", samp_p));
        append(child_scop, make_data_u32("SampleList", {}));
        append(child_scop, make_chunk_str("SCOP", sample_list));
        append(snd, make_data_u32("SoundProperties", {}));
        append(snd, make_chunk_str("SCOP", child_scop));

        append(ucfb_body, make_chunk_str("snd_", snd));
    }

    const auto lvl_path = std::filesystem::path("swm_sound_test.lvl");
    {
        std::ofstream out(lvl_path, std::ios::binary);
        const auto full = make_chunk_str("ucfb", ucfb_body);
        out.write(reinterpret_cast<const char*>(full.data()), std::streamsize(full.size()));
    }

    SoundLibrary lib;
    lib.add_file(lvl_path);

    assert(lib.sample_count() == 1);
    assert(lib.segment_count() == 1);
    assert(lib.prop_count() == 2);

    const auto resolved = lib.resolve(sound_name_hash("child_sfx"));
    assert(resolved.has_value());
    assert(resolved->gain == 0.5f);
    assert(resolved->min_distance == 15.0f);
    assert(resolved->max_distance == 120.0f);
    assert(resolved->entries.size() == 1);

    const SoundClipRef* clip = lib.find_sample(resolved->entries[0].first);
    assert(clip != nullptr);
    const std::vector<int16_t> pcm = decode_clip(*clip);
    assert(pcm == (std::vector<int16_t>{100, 200, -100, -200}));

    const SoundClipRef* seg = lib.find_segment(sound_name_hash("test_vo_stream"), sound_name_hash("test_vo_seg"));
    assert(seg != nullptr);
    if (!seg || decode_clip(*seg).size() != 64) {
        throw std::runtime_error("unexpected decoded segment size");
    }
    std::filesystem::remove(lvl_path);
}

} // namespace

#include <iostream>

int main() {
    try {
        test_hashes();
        test_adpcm_and_wav();
        test_library_bank_stream_and_inherit();
    } catch (const std::exception& e) {
        std::cerr << "sound_test exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
