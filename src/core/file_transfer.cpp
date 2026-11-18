#include "dfs/storage/file_transfer.hpp"

#include "dfs/common/checksum.hpp"

#include <atomic>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <openssl/evp.h>

namespace dfs {
namespace {
std::atomic_uint64_t g_temp_sequence{0};

bool valid_file_id(std::string_view id) {
    if (id.empty() || id.size() > 100U) return false;
    for (const unsigned char ch : id) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!allowed) return false;
    }
    return true;
}

bool valid_digest(std::string_view digest) {
    if (digest.size() != 64U) return false;
    for (const char ch : digest) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return true;
}

std::string make_chunk_id(const std::string& file_id, std::uint64_t sequence) {
    return file_id + "-" + std::to_string(sequence);
}

struct EvpDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

class Sha256Stream {
public:
    Sha256Stream() : context_(EVP_MD_CTX_new()) {
        if (!context_) throw std::runtime_error("failed to allocate SHA-256 context");
        if (EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("failed to initialize SHA-256 context");
        }
    }

    void update(const std::byte* data, std::size_t size) {
        if (size != 0U && EVP_DigestUpdate(context_.get(), data, size) != 1) {
            throw std::runtime_error("failed to update SHA-256 context");
        }
    }

    std::string finish() {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int length = 0;
        if (EVP_DigestFinal_ex(context_.get(), digest.data(), &length) != 1) {
            throw std::runtime_error("failed to finalize SHA-256 context");
        }
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (unsigned int i = 0; i < length; ++i) {
            output << std::setw(2) << static_cast<unsigned int>(digest[i]);
        }
        return output.str();
    }

private:
    std::unique_ptr<EVP_MD_CTX, EvpDeleter> context_;
};

Result<void> validate_manifest(const FileManifest& manifest) {
    if (!valid_file_id(manifest.file_id))
        return Result<void>::failure("manifest contains an invalid file id");
    if (!valid_digest(manifest.sha256))
        return Result<void>::failure("manifest contains an invalid file checksum");
    if (manifest.total_size_bytes == 0U && !manifest.chunks.empty())
        return Result<void>::failure("empty file manifest must not contain chunks");
    if (manifest.total_size_bytes != 0U && manifest.chunks.empty())
        return Result<void>::failure("non-empty file manifest has no chunks");

    std::uint64_t total = 0;
    for (std::size_t index = 0; index < manifest.chunks.size(); ++index) {
        const auto& chunk = manifest.chunks[index];
        if (chunk.sequence != static_cast<std::uint64_t>(index))
            return Result<void>::failure("manifest chunk sequence is not contiguous");
        if (chunk.chunk_id != make_chunk_id(manifest.file_id, chunk.sequence))
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
} // namespace

Result<FileManifest> FileTransfer::upload(
    const std::filesystem::path& source, const std::string& file_id, const ChunkStore& store) {
    if (!valid_file_id(file_id))
        return Result<FileManifest>::failure("file id must contain 1 to 100 safe filename characters");

    try {
        std::ifstream input(source, std::ios::binary);
        if (!input) return Result<FileManifest>::failure("source file not found or unreadable");

        FileManifest manifest;
        manifest.file_id = file_id;
        Sha256Stream file_digest;
        std::vector<std::byte> buffer(kMaxChunkSizeBytes);
        std::uint64_t sequence = 0;

        while (true) {
            input.read(reinterpret_cast<char*>(buffer.data()),
                       static_cast<std::streamsize>(buffer.size()));
            const std::streamsize bytes_read = input.gcount();
            if (bytes_read < 0)
                return Result<FileManifest>::failure("source stream returned a negative byte count");
            const auto count = static_cast<std::size_t>(bytes_read);

            if (count != 0U) {
                const auto count64 = static_cast<std::uint64_t>(count);
                if (manifest.total_size_bytes > std::numeric_limits<std::uint64_t>::max() - count64)
                    return Result<FileManifest>::failure("source file is too large for the manifest format");

                file_digest.update(buffer.data(), count);
                std::vector<std::byte> chunk(
                    buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
                const std::string chunk_id = make_chunk_id(file_id, sequence);
                const auto saved = store.put(chunk_id, chunk);
                if (!saved) {
                    return Result<FileManifest>::failure(
                        "failed to store chunk " + std::to_string(sequence) + ": " + saved.error());
                }

                manifest.chunks.push_back(ChunkReference{
                    sequence, chunk_id, count64, saved.value().sha256});
                manifest.total_size_bytes += count64;
                if (sequence == std::numeric_limits<std::uint64_t>::max())
                    return Result<FileManifest>::failure("source contains too many chunks");
                ++sequence;
            }

            if (input.bad() || (input.fail() && !input.eof()))
                return Result<FileManifest>::failure("I/O error while reading source file");
            if (input.eof()) break;
        }

        manifest.sha256 = file_digest.finish();
        return Result<FileManifest>::success(std::move(manifest));
    } catch (const std::exception& error) {
        return Result<FileManifest>::failure(error.what());
    }
}

Result<void> FileTransfer::download(
    const FileManifest& manifest, const ChunkStore& store,
    const std::filesystem::path& destination) {
    const auto validation = validate_manifest(manifest);
    if (!validation) return validation;
    if (destination.empty() || destination.filename().empty())
        return Result<void>::failure("destination must include a filename");

    const auto suffix = g_temp_sequence.fetch_add(1, std::memory_order_relaxed);
    const std::filesystem::path temporary = destination.string() + ".tmp." + std::to_string(suffix);

    try {
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path());

        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return Result<void>::failure("unable to open temporary destination file");
        Sha256Stream file_digest;

        for (const auto& reference : manifest.chunks) {
            const auto loaded = store.get(reference.chunk_id);
            if (!loaded) {
                output.close();
                std::filesystem::remove(temporary);
                return Result<void>::failure(
                    "failed to load chunk " + reference.chunk_id + ": " + loaded.error());
            }
            const auto& bytes = loaded.value();
            if (bytes.size() != reference.size_bytes) {
                output.close();
                std::filesystem::remove(temporary);
                return Result<void>::failure("chunk size does not match manifest");
            }
            if (Checksum::sha256(bytes) != reference.sha256) {
                output.close();
                std::filesystem::remove(temporary);
                return Result<void>::failure("chunk checksum does not match manifest");
            }

            file_digest.update(bytes.data(), bytes.size());
            if (!bytes.empty()) {
                output.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            }
            if (!output) {
                output.close();
                std::filesystem::remove(temporary);
                return Result<void>::failure("failed while writing reconstructed file");
            }
        }

        if (file_digest.finish() != manifest.sha256) {
            output.close();
            std::filesystem::remove(temporary);
            return Result<void>::failure("reconstructed file checksum does not match manifest");
        }
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary);
            return Result<void>::failure("failed to flush reconstructed file");
        }
        output.close();
        if (!output) {
            std::filesystem::remove(temporary);
            return Result<void>::failure("failed to close reconstructed file");
        }

        std::error_code error;
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            std::filesystem::remove(temporary);
            return Result<void>::failure("unable to publish reconstructed file: " + error.message());
        }
        return Result<void>::success();
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(error.what());
    }
}
} // namespace dfs
