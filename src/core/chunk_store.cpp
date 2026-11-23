#include "dfs/storage/chunk_store.hpp"

#include "dfs/common/checksum.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace dfs {
namespace {
constexpr char kMagic[] = "DFSCHN1";
constexpr std::size_t kHeaderSize = (sizeof(kMagic) - 1U) + 64U;
std::atomic_uint64_t g_temp_sequence{0};

std::mutex g_chunk_lock_registry_mutex;
std::unordered_map<std::string, std::weak_ptr<std::mutex>> g_chunk_lock_registry;

std::uint64_t process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(_getpid());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::shared_ptr<std::mutex> mutex_for_chunk(
    const std::filesystem::path& root, const std::string& chunk_id) {
    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(root, error);
    const std::string normalized_root =
        (error ? root : absolute_root).lexically_normal().string();
    std::string key = normalized_root;
    key.push_back('\0');
    key.append(chunk_id);

    std::lock_guard registry_guard(g_chunk_lock_registry_mutex);
    const auto found = g_chunk_lock_registry.find(key);
    if (found != g_chunk_lock_registry.end()) {
        if (auto existing = found->second.lock()) return existing;
    }

    auto created = std::make_shared<std::mutex>();
    g_chunk_lock_registry[key] = created;
    if (g_chunk_lock_registry.size() > 256U) {
        for (auto it = g_chunk_lock_registry.begin(); it != g_chunk_lock_registry.end();) {
            if (it->second.expired()) it = g_chunk_lock_registry.erase(it);
            else ++it;
        }
    }
    return created;
}
}

ChunkStore::ChunkStore(std::filesystem::path root) : root_(std::move(root)) {}

Result<std::filesystem::path> ChunkStore::path_for(const std::string& chunk_id) const {
    if (chunk_id.empty() || chunk_id.size() > 128U) {
        return Result<std::filesystem::path>::failure("chunk id must contain 1 to 128 characters");
    }
    for (const unsigned char ch : chunk_id) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!allowed) {
            return Result<std::filesystem::path>::failure("chunk id contains invalid characters");
        }
    }
    return Result<std::filesystem::path>::success(root_ / (chunk_id + ".chunk"));
}

Result<ChunkInfo> ChunkStore::put(const std::string& chunk_id,
                                  const std::vector<std::byte>& data) const {
    const auto destination = path_for(chunk_id);
    if (!destination) return Result<ChunkInfo>::failure(destination.error());
    if (data.size() > kMaxChunkSizeBytes) {
        return Result<ChunkInfo>::failure("chunk exceeds the 4 MiB size limit");
    }

    try {
        const auto chunk_mutex = mutex_for_chunk(root_, chunk_id);
        std::lock_guard chunk_guard(*chunk_mutex);
        std::filesystem::create_directories(root_);
        const std::string digest = Checksum::sha256(data);
        const auto suffix = g_temp_sequence.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path temporary =
            destination.value().string() + ".tmp." + std::to_string(process_id()) +
            "." + std::to_string(suffix);

        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return Result<ChunkInfo>::failure("unable to open temporary chunk file");

            output.write(kMagic, static_cast<std::streamsize>(sizeof(kMagic) - 1U));
            output.write(digest.data(), static_cast<std::streamsize>(digest.size()));
            if (!data.empty()) {
                output.write(reinterpret_cast<const char*>(data.data()),
                             static_cast<std::streamsize>(data.size()));
            }
            output.flush();
            if (!output) {
                output.close();
                std::filesystem::remove(temporary);
                return Result<ChunkInfo>::failure("failed while writing chunk data");
            }
        }

        std::error_code error;
        std::filesystem::rename(temporary, destination.value(), error);
        if (error) {
            std::filesystem::remove(temporary);
            return Result<ChunkInfo>::failure("unable to commit chunk: " + error.message());
        }

        return Result<ChunkInfo>::success(
            ChunkInfo{chunk_id, destination.value(), data.size(), digest});
    } catch (const std::exception& error) {
        return Result<ChunkInfo>::failure(error.what());
    }
}

Result<std::vector<std::byte>> ChunkStore::get(const std::string& chunk_id) const {
    const auto source = path_for(chunk_id);
    if (!source) return Result<std::vector<std::byte>>::failure(source.error());

    try {
        const auto chunk_mutex = mutex_for_chunk(root_, chunk_id);
        std::lock_guard chunk_guard(*chunk_mutex);
        std::ifstream input(source.value(), std::ios::binary);
        if (!input) return Result<std::vector<std::byte>>::failure("chunk not found or unreadable");

        char magic[sizeof(kMagic) - 1U]{};
        char stored_digest[64]{};
        input.read(magic, static_cast<std::streamsize>(sizeof(magic)));
        input.read(stored_digest, static_cast<std::streamsize>(sizeof(stored_digest)));
        if (!input || std::string(magic, sizeof(magic)) != std::string(kMagic, sizeof(magic))) {
            return Result<std::vector<std::byte>>::failure("invalid chunk header");
        }

        input.seekg(0, std::ios::end);
        const std::streamoff end = input.tellg();
        if (end < static_cast<std::streamoff>(kHeaderSize)) {
            return Result<std::vector<std::byte>>::failure("truncated chunk file");
        }
        const auto payload_size = static_cast<std::uintmax_t>(
            end - static_cast<std::streamoff>(kHeaderSize));
        if (payload_size > kMaxChunkSizeBytes) {
            return Result<std::vector<std::byte>>::failure("stored chunk exceeds the 4 MiB size limit");
        }

        std::vector<std::byte> data(static_cast<std::size_t>(payload_size));
        input.seekg(static_cast<std::streamoff>(kHeaderSize), std::ios::beg);
        if (!data.empty()) {
            input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        if (!input && !data.empty()) {
            return Result<std::vector<std::byte>>::failure("failed while reading chunk payload");
        }

        if (Checksum::sha256(data) != std::string(stored_digest, sizeof(stored_digest))) {
            return Result<std::vector<std::byte>>::failure("chunk checksum mismatch");
        }
        return Result<std::vector<std::byte>>::success(std::move(data));
    } catch (const std::exception& error) {
        return Result<std::vector<std::byte>>::failure(error.what());
    }
}

} // namespace dfs
