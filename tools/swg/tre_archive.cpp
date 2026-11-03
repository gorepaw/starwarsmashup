#include "swg/tre_archive.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

namespace swm::swg {
namespace {

static_assert(std::endian::native == std::endian::little, "TRE parsing assumes a little-endian host");

constexpr size_t header_size = 36;
constexpr size_t toc_entry_size = 24;

uint32_t read_u32(const uint8_t* p) {
    uint32_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

} // namespace

TreArchive::TreArchive(const std::filesystem::path& file) : path_(file), file_(file, std::ios::binary) {
    if (!file_) {
        throw std::runtime_error(path_.string() + ": cannot open");
    }

    uint8_t header[header_size];
    if (!file_.read(reinterpret_cast<char*>(header), header_size)) {
        throw std::runtime_error(path_.string() + ": truncated header");
    }
    if (std::memcmp(header, "EERT", 4) != 0) {
        throw std::runtime_error(path_.string() + ": not a TRE archive");
    }
    // The version is stored as reversed ASCII digits: "5000" is version 0005.
    if (std::memcmp(header + 4, "5000", 4) == 0) {
        version_ = 5;
    } else if (std::memcmp(header + 4, "6000", 4) == 0) {
        version_ = 6;
    } else {
        throw std::runtime_error(path_.string() + ": unsupported TRE version '" +
                                 std::string(reinterpret_cast<const char*>(header + 4), 4) + "'");
    }

    const uint32_t file_count = read_u32(header + 8);
    const uint32_t toc_offset = read_u32(header + 12);
    const uint32_t toc_compressor = read_u32(header + 16);
    const uint32_t toc_stored_size = read_u32(header + 20);
    const uint32_t names_compressor = read_u32(header + 24);
    const uint32_t names_stored_size = read_u32(header + 28);
    const uint32_t names_size = read_u32(header + 32);

    std::vector<uint8_t> toc;
    try {
        toc = read_block(toc_offset, toc_compressor, toc_stored_size, uint64_t(file_count) * toc_entry_size);
    } catch (const std::runtime_error&) {
        // SWG Restoration's clients use 0006 archives with 32-byte entries and
        // encrypted file data. We don't read those.
        constexpr size_t encrypted_entry_size = 32;
        if (version_ == 6) {
            // Throws its own error if this isn't that layout either.
            read_block(toc_offset, toc_compressor, toc_stored_size, uint64_t(file_count) * encrypted_entry_size);
            throw std::runtime_error(path_.string() +
                                     ": 0006 archive with 32-byte entries (encrypted file data); not supported");
        }
        throw;
    }
    const std::vector<uint8_t> names =
        read_block(uint64_t(toc_offset) + toc_stored_size, names_compressor, names_stored_size, names_size);

    entries_.reserve(file_count);
    for (uint32_t i = 0; i < file_count; ++i) {
        const uint8_t* record = toc.data() + size_t(i) * toc_entry_size;
        TreEntry entry;
        entry.crc = read_u32(record);
        entry.size = read_u32(record + 4);
        entry.offset = read_u32(record + 8);
        entry.compressor = read_u32(record + 12);
        entry.compressed_size = read_u32(record + 16);

        const uint32_t name_offset = read_u32(record + 20);
        if (name_offset >= names.size()) {
            throw std::runtime_error(path_.string() + ": file name offset out of range");
        }
        const auto name_begin = names.begin() + name_offset;
        const auto name_end = std::find(name_begin, names.end(), uint8_t(0));
        if (name_end == names.end()) {
            throw std::runtime_error(path_.string() + ": unterminated file name");
        }
        entry.path.assign(name_begin, name_end);
        entries_.push_back(std::move(entry));
    }
}

std::vector<uint8_t> TreArchive::read(const TreEntry& entry) {
    return read_block(entry.offset, entry.compressor, entry.compressed_size, entry.size);
}

std::vector<uint8_t> TreArchive::read_block(uint64_t offset, uint32_t compressor, uint32_t stored_size,
                                            uint64_t size) {
    if (size == 0) {
        return {};
    }
    if (compressor != tre_compressor_none && compressor != tre_compressor_zlib) {
        throw std::runtime_error(path_.string() + ": unsupported compressor " + std::to_string(compressor));
    }

    std::vector<uint8_t> stored(compressor == tre_compressor_none ? size : stored_size);
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    if (!file_.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()))) {
        throw std::runtime_error(path_.string() + ": truncated data at offset " + std::to_string(offset));
    }
    if (compressor == tre_compressor_none) {
        return stored;
    }

    std::vector<uint8_t> out(size);
    uLongf out_size = static_cast<uLongf>(size);
    const int result = uncompress(out.data(), &out_size, stored.data(), static_cast<uLong>(stored.size()));
    if (result != Z_OK || out_size != size) {
        throw std::runtime_error(path_.string() + ": corrupt zlib data at offset " + std::to_string(offset));
    }
    return out;
}

} // namespace swm::swg
