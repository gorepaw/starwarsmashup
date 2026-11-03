#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace swm::swg {

enum TreCompressor : uint32_t {
    tre_compressor_none = 0,
    tre_compressor_zlib = 2,
};

struct TreEntry {
    std::string path;              // as stored in the archive
    uint32_t crc = 0;
    uint32_t size = 0;             // uncompressed size
    uint32_t offset = 0;
    uint32_t compressor = 0;       // TreCompressor
    uint32_t compressed_size = 0;
};

// Reader for SWG TRE archives, versions 0005 (retail discs) and 0006 (later clients).
//
// Layout: a 36-byte header, file data, then a table of contents (24 bytes per
// entry) followed by a block of null-terminated file names. The table and the
// name block may each be zlib-compressed.
//
// Not thread-safe: use one TreArchive per thread.
class TreArchive {
public:
    // Throws std::runtime_error if the file can't be opened or is malformed.
    explicit TreArchive(const std::filesystem::path& file);

    uint32_t version() const { return version_; }
    const std::vector<TreEntry>& entries() const { return entries_; }

    // Returns the uncompressed contents of an entry. Throws std::runtime_error.
    std::vector<uint8_t> read(const TreEntry& entry);

private:
    std::vector<uint8_t> read_block(uint64_t offset, uint32_t compressor, uint32_t stored_size, uint64_t size);

    std::filesystem::path path_;
    std::ifstream file_;
    uint32_t version_ = 0;
    std::vector<TreEntry> entries_;
};

} // namespace swm::swg
