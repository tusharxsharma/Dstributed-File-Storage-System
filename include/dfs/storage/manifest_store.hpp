#pragma once

#include "dfs/common/result.hpp"
#include "dfs/storage/file_transfer.hpp"

#include <cstddef>
#include <filesystem>

namespace dfs {

// A manifest is deliberately bounded before it is read into memory.
inline constexpr std::size_t kMaxManifestFileSizeBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaxManifestChunkCount = 300'000U;

class ManifestStore {
public:
    explicit ManifestStore(std::filesystem::path manifest_path);

    // Writes a versioned, checksummed manifest using a temporary sibling file.
    // This provides atomic publication where rename replacement is atomic; it
    // does not promise power-loss durability (file/directory fsync is not done).
    [[nodiscard]] Result<void> save(const FileManifest& manifest) const;

    // Rejects unsupported versions, malformed lengths, oversized files,
    // truncated records, checksum mismatches and inconsistent metadata.
    [[nodiscard]] Result<FileManifest> load() const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace dfs
