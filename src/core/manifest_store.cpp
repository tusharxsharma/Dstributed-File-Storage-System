#include "dfs/storage/manifest_store.hpp"

#include "dfs/common/checksum.hpp"
#include "dfs/storage/chunk_store.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dfs {
namespace {
constexpr std::array<std::byte, 8> kMagic{
    std::byte{'D'}, std::byte{'F'}, std::byte{'S'}, std::byte{'M'},
    std::byte{'A'}, std::byte{'N'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::uint16_t kFormatVersion = 1U;
constexpr std::size_t kDigestHexSize = 64U;
constexpr std::size_t kTrailerSize = kDigestHexSize;
constexpr std::size_t kMinimumEncodedSize =
    kMagic.size() + 2U + 2U + 8U + 4U + 2U + 1U + kDigestHexSize + kTrailerSize;
std::atomic_uint64_t g_temp_sequence{0};

bool valid_id(std::string_view id, std::size_t maximum) {
    if (id.empty() || id.size() > maximum) return false;
    for (const unsigned char ch : id) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!allowed) return false;
    }
    return true;
}

bool valid_digest(std::string_view digest) {
    if (digest.size() != kDigestHexSize) return false;
    for (const char ch : digest) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return true;
}

std::string expected_chunk_id(std::string_view file_id, std::uint64_t sequence) {
    return std::string(file_id) + "-" + std::to_string(sequence);
}

Result<void> validate_manifest(const FileManifest& manifest) {
    if (!valid_id(manifest.file_id, 100U))
        return Result<void>::failure("manifest contains an invalid file id");
    if (!valid_digest(manifest.sha256))
        return Result<void>::failure("manifest contains an invalid file checksum");
    if (manifest.chunks.size() > kMaxManifestChunkCount)
        return Result<void>::failure("manifest exceeds the maximum chunk count");
    if (manifest.total_size_bytes == 0U && !manifest.chunks.empty())
        return Result<void>::failure("empty file manifest must not contain chunks");
    if (manifest.total_size_bytes != 0U && manifest.chunks.empty())
        return Result<void>::failure("non-empty file manifest has no chunks");

    std::uint64_t total = 0;
    for (std::size_t index = 0; index < manifest.chunks.size(); ++index) {
        const auto& chunk = manifest.chunks[index];
        const auto sequence = static_cast<std::uint64_t>(index);
        if (chunk.sequence != sequence)
            return Result<void>::failure("manifest chunk sequence is not contiguous");
        if (!valid_id(chunk.chunk_id, 128U) ||
            chunk.chunk_id != expected_chunk_id(manifest.file_id, sequence))
            return Result<void>::failure("manifest chunk id does not match its file and sequence");
        if (chunk.size_bytes == 0U || chunk.size_bytes > kMaxChunkSizeBytes)
            return Result<void>::failure("manifest contains an invalid chunk size");
        if (!valid_digest(chunk.sha256))
            return Result<void>::failure("manifest contains an invalid chunk checksum");
        if (total > std::numeric_limits<std::uint64_t>::max() - chunk.size_bytes)
            return Result<void>::failure("manifest file size overflows uint64");
        total += chunk.size_bytes;
    }
    if (total != manifest.total_size_bytes)
        return Result<void>::failure("manifest total size does not match its chunks");
    return Result<void>::success();
}

void append_integer(std::vector<std::byte>& out, std::uint64_t value, unsigned int bytes) {
    for (unsigned int shift = 0; shift < bytes * 8U; shift += 8U)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

void append_string(std::vector<std::byte>& out, std::string_view value) {
    for (const unsigned char ch : value) out.push_back(static_cast<std::byte>(ch));
}

Result<std::vector<std::byte>> encode_manifest(const FileManifest& manifest) {
    const auto validation = validate_manifest(manifest);
    if (!validation) return Result<std::vector<std::byte>>::failure(validation.error());

    std::size_t encoded_size = kMinimumEncodedSize + manifest.file_id.size() - 1U;
    for (const auto& chunk : manifest.chunks) {
        const std::size_t record_size = 8U + 2U + chunk.chunk_id.size() + 8U + kDigestHexSize;
        if (record_size > kMaxManifestFileSizeBytes ||
            encoded_size > kMaxManifestFileSizeBytes - record_size)
            return Result<std::vector<std::byte>>::failure(
                "encoded manifest exceeds the 64 MiB size limit");
        encoded_size += record_size;
    }

    std::vector<std::byte> encoded;
    encoded.reserve(encoded_size);
    encoded.insert(encoded.end(), kMagic.begin(), kMagic.end());
    append_integer(encoded, kFormatVersion, 2U);
    append_integer(encoded, 0U, 2U);
    append_integer(encoded, manifest.total_size_bytes, 8U);
    append_integer(encoded, manifest.chunks.size(), 4U);
    append_integer(encoded, manifest.file_id.size(), 2U);
    append_string(encoded, manifest.file_id);
    append_string(encoded, manifest.sha256);

    for (const auto& chunk : manifest.chunks) {
        append_integer(encoded, chunk.sequence, 8U);
        append_integer(encoded, chunk.chunk_id.size(), 2U);
        append_string(encoded, chunk.chunk_id);
        append_integer(encoded, chunk.size_bytes, 8U);
        append_string(encoded, chunk.sha256);
    }

    append_string(encoded, Checksum::sha256(
        std::span<const std::byte>(encoded.data(), encoded.size())));
    return Result<std::vector<std::byte>>::success(std::move(encoded));
}

class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> input) : input_(input) {}

    bool read_integer(std::uint64_t& value, unsigned int bytes) {
        if (remaining() < bytes) return false;
        value = 0U;
        for (unsigned int index = 0; index < bytes; ++index)
            value |= static_cast<std::uint64_t>(
                std::to_integer<unsigned char>(input_[offset_++])) << (index * 8U);
        return true;
    }

    bool read_string(std::size_t length, std::string& value) {
        if (length > remaining()) return false;
        value.assign(reinterpret_cast<const char*>(input_.data() + offset_), length);
        offset_ += length;
        return true;
    }

    std::size_t remaining() const noexcept { return input_.size() - offset_; }

private:
    std::span<const std::byte> input_;
    std::size_t offset_{0};
};

Result<FileManifest> decode_manifest(std::span<const std::byte> encoded) {
    if (encoded.size() < kMinimumEncodedSize)
        return Result<FileManifest>::failure("manifest is truncated");
    if (encoded.size() > kMaxManifestFileSizeBytes)
        return Result<FileManifest>::failure("manifest exceeds the 64 MiB size limit");

    const auto payload = encoded.first(encoded.size() - kTrailerSize);
    ByteReader reader(payload);
    for (const auto expected : kMagic) {
        std::string actual;
        if (!reader.read_string(1U, actual) ||
            static_cast<unsigned char>(actual.front()) != std::to_integer<unsigned char>(expected))
            return Result<FileManifest>::failure("invalid manifest magic header");
    }

    std::uint64_t version = 0, flags = 0, total_size = 0, chunk_count = 0, file_id_size = 0;
    if (!reader.read_integer(version, 2U) || !reader.read_integer(flags, 2U) ||
        !reader.read_integer(total_size, 8U) || !reader.read_integer(chunk_count, 4U) ||
        !reader.read_integer(file_id_size, 2U))
        return Result<FileManifest>::failure("manifest header is truncated");
    if (version != kFormatVersion)
        return Result<FileManifest>::failure("unsupported manifest format version");
    if (flags != 0U)
        return Result<FileManifest>::failure("manifest contains unsupported flags");
    if (chunk_count > kMaxManifestChunkCount)
        return Result<FileManifest>::failure("manifest exceeds the maximum chunk count");
    if (file_id_size == 0U || file_id_size > 100U)
        return Result<FileManifest>::failure("manifest contains an invalid file id length");

    const std::string expected_checksum(
        reinterpret_cast<const char*>(encoded.data() + payload.size()), kTrailerSize);
    if (expected_checksum != Checksum::sha256(payload))
        return Result<FileManifest>::failure("manifest checksum mismatch");

    FileManifest manifest;
    manifest.total_size_bytes = total_size;
    if (!reader.read_string(static_cast<std::size_t>(file_id_size), manifest.file_id) ||
        !reader.read_string(kDigestHexSize, manifest.sha256))
        return Result<FileManifest>::failure("manifest file metadata is truncated");

    constexpr std::size_t kMinimumChunkRecordSize = 8U + 2U + 1U + 8U + kDigestHexSize;
    if (chunk_count > reader.remaining() / kMinimumChunkRecordSize)
        return Result<FileManifest>::failure(
            "manifest chunk count exceeds the remaining record data");

    manifest.chunks.reserve(static_cast<std::size_t>(chunk_count));
    for (std::uint64_t index = 0; index < chunk_count; ++index) {
        ChunkReference chunk;
        std::uint64_t chunk_id_size = 0;
        if (!reader.read_integer(chunk.sequence, 8U) ||
            !reader.read_integer(chunk_id_size, 2U))
            return Result<FileManifest>::failure("manifest chunk record is truncated");
        if (chunk_id_size == 0U || chunk_id_size > 128U)
            return Result<FileManifest>::failure("manifest contains an invalid chunk id length");
        if (!reader.read_string(static_cast<std::size_t>(chunk_id_size), chunk.chunk_id) ||
            !reader.read_integer(chunk.size_bytes, 8U) ||
            !reader.read_string(kDigestHexSize, chunk.sha256))
            return Result<FileManifest>::failure("manifest chunk record is truncated");
        manifest.chunks.push_back(std::move(chunk));
    }

    if (reader.remaining() != 0U)
        return Result<FileManifest>::failure("manifest contains unexpected trailing data");
    const auto validation = validate_manifest(manifest);
    if (!validation) return Result<FileManifest>::failure(validation.error());
    return Result<FileManifest>::success(std::move(manifest));
}
} // namespace

ManifestStore::ManifestStore(std::filesystem::path manifest_path)
    : path_(std::move(manifest_path)) {}

Result<void> ManifestStore::save(const FileManifest& manifest) const {
    if (path_.empty() || path_.filename().empty())
        return Result<void>::failure("manifest path must include a filename");
    const auto encoded = encode_manifest(manifest);
    if (!encoded) return Result<void>::failure(encoded.error());

    const auto suffix = g_temp_sequence.fetch_add(1, std::memory_order_relaxed);
    const std::filesystem::path temporary = path_.string() + ".tmp." + std::to_string(suffix);
    try {
        if (!path_.parent_path().empty())
            std::filesystem::create_directories(path_.parent_path());
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return Result<void>::failure("unable to open temporary manifest file");
        output.write(reinterpret_cast<const char*>(encoded.value().data()),
                     static_cast<std::streamsize>(encoded.value().size()));
        output.flush();
        if (!output) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return Result<void>::failure("failed while writing manifest data");
        }
        output.close();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return Result<void>::failure("failed to close temporary manifest file");
        }

        std::error_code error;
        std::filesystem::rename(temporary, path_, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return Result<void>::failure("unable to publish manifest: " + error.message());
        }
        return Result<void>::success();
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(error.what());
    }
}

Result<FileManifest> ManifestStore::load() const {
    if (path_.empty() || path_.filename().empty())
        return Result<FileManifest>::failure("manifest path must include a filename");
    try {
        const std::uintmax_t file_size = std::filesystem::file_size(path_);
        if (file_size < kMinimumEncodedSize)
            return Result<FileManifest>::failure("manifest is truncated");
        if (file_size > kMaxManifestFileSizeBytes)
            return Result<FileManifest>::failure("manifest exceeds the 64 MiB size limit");

        std::ifstream input(path_, std::ios::binary);
        if (!input) return Result<FileManifest>::failure("manifest not found or unreadable");
        std::vector<std::byte> encoded(static_cast<std::size_t>(file_size));
        input.read(reinterpret_cast<char*>(encoded.data()),
                   static_cast<std::streamsize>(encoded.size()));
        if (input.gcount() != static_cast<std::streamsize>(encoded.size()) || input.bad())
            return Result<FileManifest>::failure("failed while reading manifest file");
        if (input.peek() != std::char_traits<char>::eof())
            return Result<FileManifest>::failure("manifest changed while being read");
        return decode_manifest(encoded);
    } catch (const std::exception& error) {
        return Result<FileManifest>::failure(error.what());
    }
}

} // namespace dfs