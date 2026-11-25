#include "dfs/service/storage_node.hpp"

#include <atomic>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
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
constexpr std::uint64_t kChunkRecordHeaderBytes = 7U + 64U;
constexpr char kNodeIdentityMagic[] = "DFS-NODE-1";
std::atomic_uint64_t g_node_temp_sequence{0};
std::mutex g_node_lock_registry_mutex;
std::unordered_map<std::string, std::weak_ptr<std::mutex>> g_node_lock_registry;

std::uint64_t process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(_getpid());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

bool valid_node_id(const std::string& id) {
    if (id.empty() || id.size() > 100U) return false;
    for (const unsigned char ch : id) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!allowed) return false;
    }
    return true;
}

bool valid_chunk_id(const std::string& id) {
    if (id.empty() || id.size() > 128U) return false;
    for (const unsigned char ch : id) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!allowed) return false;
    }
    return true;
}

std::shared_ptr<std::mutex> mutex_for_root(const std::filesystem::path& root) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(root, error);
    const std::string key = (error ? root : absolute).lexically_normal().string();
    std::lock_guard registry_guard(g_node_lock_registry_mutex);
    const auto found = g_node_lock_registry.find(key);
    if (found != g_node_lock_registry.end()) {
        if (auto existing = found->second.lock()) return existing;
    }
    auto created = std::make_shared<std::mutex>();
    g_node_lock_registry[key] = created;
    if (g_node_lock_registry.size() > 256U) {
        for (auto it = g_node_lock_registry.begin(); it != g_node_lock_registry.end();) {
            if (it->second.expired()) it = g_node_lock_registry.erase(it);
            else ++it;
        }
    }
    return created;
}
} // namespace

StorageNode::StorageNode(StorageNodeConfig config)
    : config_(std::move(config)), chunks_(config_.root / "chunks"),
      root_mutex_(mutex_for_root(config_.root)) {}

Result<void> StorageNode::initialize() {
    std::lock_guard root_guard(*root_mutex_);
    if (!valid_node_id(config_.node_id)) {
        return Result<void>::failure("node ID must contain 1 to 100 safe filename characters");
    }
    if (config_.capacity_bytes == 0U) {
        return Result<void>::failure("node capacity must be greater than zero");
    }

    try {
        std::filesystem::create_directories(config_.root);
        std::filesystem::create_directories(chunks_.root());
        const auto identity_path = config_.root / "node-id";
        std::error_code error;
        const bool identity_exists = std::filesystem::exists(identity_path, error);
        if (error) return Result<void>::failure("inspect node identity: " + error.message());

        if (identity_exists) {
            std::ifstream input(identity_path, std::ios::binary);
            if (!input) return Result<void>::failure("unable to read node identity");
            std::string magic;
            std::string stored_id;
            std::getline(input, magic);
            std::getline(input, stored_id);
            if (magic != kNodeIdentityMagic || stored_id != config_.node_id) {
                return Result<void>::failure(
                    "storage root is already registered to a different or invalid node ID");
            }
        } else {
            const auto suffix = g_node_temp_sequence.fetch_add(1, std::memory_order_relaxed);
            const auto temporary = identity_path.string() + ".tmp." +
                                   std::to_string(process_id()) + "." + std::to_string(suffix);
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) return Result<void>::failure("unable to create node identity temporary file");
                output << kNodeIdentityMagic << '\n' << config_.node_id << '\n';
                output.flush();
                if (!output) {
                    output.close();
                    std::filesystem::remove(temporary);
                    return Result<void>::failure("failed while writing node identity");
                }
            }
            std::filesystem::rename(temporary, identity_path, error);
            if (error) {
                std::filesystem::remove(temporary);
                return Result<void>::failure("publish node identity: " + error.message());
            }
        }
        initialized_ = true;
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(error.what());
    }
}

Result<void> StorageNode::ensure_initialized() const {
    if (!initialized_) return Result<void>::failure("storage node is not initialized");
    return Result<void>::success();
}

Result<StorageNodeStatus> StorageNode::scan_status() const {
    StorageNodeStatus result{config_.node_id, config_.root, config_.capacity_bytes,
                             0U, 0U, 0U};
    std::error_code error;
    if (!std::filesystem::exists(chunks_.root(), error)) {
        if (error) return Result<StorageNodeStatus>::failure(
            "inspect chunk directory: " + error.message());
        result.available_bytes = config_.capacity_bytes;
        return Result<StorageNodeStatus>::success(std::move(result));
    }

    std::filesystem::directory_iterator iterator(chunks_.root(), error);
    if (error) return Result<StorageNodeStatus>::failure("scan chunk directory: " + error.message());
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const auto entry = *iterator;
        const auto status = entry.symlink_status(error);
        if (error) return Result<StorageNodeStatus>::failure(
            "inspect chunk entry: " + error.message());
        if (std::filesystem::is_regular_file(status) && entry.path().extension() == ".chunk") {
            const auto file_size = entry.file_size(error);
            if (error) return Result<StorageNodeStatus>::failure(
                "read chunk size: " + error.message());
            if (file_size < kChunkRecordHeaderBytes ||
                file_size - kChunkRecordHeaderBytes > kMaxChunkSizeBytes) {
                return Result<StorageNodeStatus>::failure(
                    "invalid chunk record size encountered during capacity scan");
            }
            const auto payload_size = static_cast<std::uint64_t>(
                file_size - kChunkRecordHeaderBytes);
            if (payload_size > std::numeric_limits<std::uint64_t>::max() - result.used_bytes) {
                return Result<StorageNodeStatus>::failure("capacity scan overflow");
            }
            result.used_bytes += payload_size;
            ++result.chunk_count;
        }
        iterator.increment(error);
        if (error) return Result<StorageNodeStatus>::failure(
            "scan chunk directory: " + error.message());
    }
    result.available_bytes = result.used_bytes >= result.capacity_bytes
        ? 0U : result.capacity_bytes - result.used_bytes;
    return Result<StorageNodeStatus>::success(std::move(result));
}

Result<StorageNodeStatus> StorageNode::status() const {
    std::lock_guard root_guard(*root_mutex_);
    const auto initialized = ensure_initialized();
    if (!initialized) return Result<StorageNodeStatus>::failure(initialized.error());
    return scan_status();
}

Result<ChunkInfo> StorageNode::put_chunk(
    const std::string& chunk_id, const std::vector<std::byte>& data) {
    std::lock_guard root_guard(*root_mutex_);
    const auto initialized = ensure_initialized();
    if (!initialized) return Result<ChunkInfo>::failure(initialized.error());
    if (!valid_chunk_id(chunk_id)) {
        return Result<ChunkInfo>::failure(
            "chunk ID must contain 1 to 128 safe filename characters");
    }
    if (data.size() > kMaxChunkSizeBytes) {
        return Result<ChunkInfo>::failure("chunk exceeds the 4 MiB size limit");
    }

    const auto current = scan_status();
    if (!current) return Result<ChunkInfo>::failure(current.error());
    std::uint64_t existing_bytes = 0U;
    const auto existing_path = chunks_.root() / (chunk_id + ".chunk");
    std::error_code error;
    const auto existing_status = std::filesystem::symlink_status(existing_path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        return Result<ChunkInfo>::failure("inspect existing chunk: " + error.message());
    }
    if (!error && std::filesystem::exists(existing_status)) {
        if (!std::filesystem::is_regular_file(existing_status)) {
            return Result<ChunkInfo>::failure("existing chunk path is not a regular file");
        }
        const auto verified = chunks_.get(chunk_id);
        if (!verified) return Result<ChunkInfo>::failure(
            "refusing to replace an unreadable or corrupt existing chunk: " + verified.error());
        existing_bytes = static_cast<std::uint64_t>(verified.value().size());
    }

    if (existing_bytes > current.value().used_bytes) {
        return Result<ChunkInfo>::failure("capacity accounting inconsistency");
    }
    const auto base_used = current.value().used_bytes - existing_bytes;
    const auto new_size = static_cast<std::uint64_t>(data.size());
    if (new_size > config_.capacity_bytes ||
        base_used > config_.capacity_bytes - new_size) {
        return Result<ChunkInfo>::failure("node capacity exceeded");
    }
    return chunks_.put(chunk_id, data);
}

Result<std::vector<std::byte>> StorageNode::get_chunk(const std::string& chunk_id) const {
    std::lock_guard root_guard(*root_mutex_);
    const auto initialized = ensure_initialized();
    if (!initialized) return Result<std::vector<std::byte>>::failure(initialized.error());
    if (!valid_chunk_id(chunk_id)) {
        return Result<std::vector<std::byte>>::failure(
            "chunk ID must contain 1 to 128 safe filename characters");
    }
    return chunks_.get(chunk_id);
}

} // namespace dfs
