// Builds small synthetic TRE archives (no game data) and reads them back.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <zlib.h>

#include "swg/tre_archive.h"

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

using Bytes = std::vector<uint8_t>;

Bytes bytes_of(const std::string& s) {
    return Bytes(s.begin(), s.end());
}

Bytes zlib_compress(const Bytes& data) {
    uLongf size = compressBound(static_cast<uLong>(data.size()));
    Bytes out(size);
    compress(out.data(), &size, data.data(), static_cast<uLong>(data.size()));
    out.resize(size);
    return out;
}

void put_u32(Bytes& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

struct TestFile {
    std::string name;
    Bytes contents;
    bool compress;
};

// Writes a TRE with the given version tag ("5000" or "6000").
void write_tre(const std::filesystem::path& path, const char* version, const std::vector<TestFile>& files,
               bool compress_tables) {
    Bytes data;
    Bytes toc;
    Bytes names;
    uint32_t offset = 36;
    for (const TestFile& file : files) {
        const Bytes stored = file.compress ? zlib_compress(file.contents) : file.contents;
        put_u32(toc, 0);                                          // crc (unused by the reader)
        put_u32(toc, static_cast<uint32_t>(file.contents.size())); // size
        put_u32(toc, offset);                                      // offset
        put_u32(toc, file.compress ? tre_compressor_zlib : tre_compressor_none);
        put_u32(toc, file.compress ? static_cast<uint32_t>(stored.size()) : 0);
        put_u32(toc, static_cast<uint32_t>(names.size())); // name offset
        names.insert(names.end(), file.name.begin(), file.name.end());
        names.push_back(0);
        data.insert(data.end(), stored.begin(), stored.end());
        offset += static_cast<uint32_t>(stored.size());
    }

    const Bytes toc_stored = compress_tables ? zlib_compress(toc) : toc;
    const Bytes names_stored = compress_tables ? zlib_compress(names) : names;
    const uint32_t table_compressor = compress_tables ? tre_compressor_zlib : tre_compressor_none;

    Bytes out = bytes_of("EERT");
    out.insert(out.end(), version, version + 4);
    put_u32(out, static_cast<uint32_t>(files.size()));
    put_u32(out, offset); // the TOC follows the file data
    put_u32(out, table_compressor);
    put_u32(out, static_cast<uint32_t>(toc_stored.size()));
    put_u32(out, table_compressor);
    put_u32(out, static_cast<uint32_t>(names_stored.size()));
    put_u32(out, static_cast<uint32_t>(names.size()));
    out.insert(out.end(), data.begin(), data.end());
    out.insert(out.end(), toc_stored.begin(), toc_stored.end());
    out.insert(out.end(), names_stored.begin(), names_stored.end());

    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(out.data()),
                                                static_cast<std::streamsize>(out.size()));
}

void test_round_trip(const char* version, bool compress_tables) {
    const std::vector<TestFile> files = {
        {"appearance/test_stored.msh", bytes_of("stored contents"), false},
        {"texture/test_zlib.dds", bytes_of(std::string(5000, 'x') + "end"), true},
        {"misc/empty.iff", {}, false},
    };
    const auto path = std::filesystem::temp_directory_path() / "swm_tre_archive_test.tre";
    write_tre(path, version, files, compress_tables);

    {
        TreArchive tre(path);
        CHECK(tre.version() == uint32_t(version[0] - '0'));
        CHECK(tre.entries().size() == files.size());
        for (size_t i = 0; i < files.size() && i < tre.entries().size(); ++i) {
            CHECK(tre.entries()[i].path == files[i].name);
            CHECK(tre.read(tre.entries()[i]) == files[i].contents);
        }
    }
    std::filesystem::remove(path);
}

void test_rejects_non_tre() {
    const auto path = std::filesystem::temp_directory_path() / "swm_tre_archive_test_bad.tre";
    std::ofstream(path, std::ios::binary) << "this is not a tre archive at all, just text padding....";
    bool threw = false;
    try {
        TreArchive tre(path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    std::filesystem::remove(path);
}

} // namespace

int main() {
    test_round_trip("5000", true);
    test_round_trip("5000", false);
    test_round_trip("6000", true);
    test_rejects_non_tre();

    if (failures == 0) {
        std::printf("tre_archive_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
