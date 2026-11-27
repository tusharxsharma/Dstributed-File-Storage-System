#pragma once

#include "dfs/common/result.hpp"
#include "dfs/storage/chunk_store.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dfs {

struct StorageNodeConfig {
    std::string node_id;
    std::filesystem::path root;
    std::uint64_t capacity_bytes{1024ULL * 1024ULL * 1024ULL};
};

struct StorageNodeStatus {
    std::string node_id;
    std::filesystem::path root;
    std::uint64_t capacity_bytes{};
    std::uint64_t used_bytes{};
    std::uint64_t available_bytes{};
    std::size_t chunk_count{};
};

class StorageNode {
public:
    explicit StorageNode(StorageNodeConfig config);

    // Creates the node directories and records a stable node identity.
    // Reusing a root with a different node ID is rejected.
    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<ChunkInfo> put_chunk(
        const std::string& chunk_id, const std::vector<std::byte>& data);
    // Repair publication may replace a corrupt/divergent record, while still enforcing quota.
    [[nodiscard]] Result<ChunkInfo> repair_chunk(
        const std::string& chunk_id, const std::vector<std::byte>& data);
    [[nodiscard]] Result<std::vector<std::byte>> get_chunk(
        const std::string& chunk_id) const;
    [[nodiscard]] Result<StorageNodeStatus> status() const;

    [[nodiscard]] const StorageNodeConfig& config() const noexcept { return config_; }

private:
    [[nodiscard]] Result<void> ensure_initialized() const;
    [[nodiscard]] Result<StorageNodeStatus> scan_status() const;

    StorageNodeConfig config_;
    ChunkStore chunks_;
    std::shared_ptr<std::mutex> root_mutex_;
    bool initialized_{false};
};

} // namespace dfs
