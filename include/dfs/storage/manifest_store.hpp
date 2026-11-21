#pragma once

#include "dfs/common/result.hpp"
#include "dfs/storage/file_transfer.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>

namespace dfs {

// A manifest is deliberately bounded before it is read into memory.
inline constexpr std::size_t kMaxManifestFileSizeBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaxManifestChunkCount = 300'000U;

class ManifestStore {
public:
    explicit ManifestStore(std::filesystem::path manifest_path);

    // Writes a versioned, checksummed manifest through a unique temporary sibling.
    // On POSIX, success follows file fsync, rename, and parent-directory fsync.
    // If directory fsync fails after rename, the file may already be visible and
    // durability is uncertain. Filesystem/hardware guarantees still vary.
    [[nodiscard]] Result<void> save(const FileManifest& manifest) const;

    // Removes old temporary siblings belonging to this manifest only. Files are
    // eligible only after the age threshold and when their encoded owner PID is
    // no longer alive. This is explicit (not run by save/load) and POSIX-only.
    // The conservative default grace period is 24 hours.
    [[nodiscard]] Result<std::size_t> cleanup_stale_temporary_files(
        std::chrono::seconds minimum_age = std::chrono::hours(24)) const;

    // Rejects unsupported versions, malformed lengths, oversized files,
    // truncated records, checksum mismatches and inconsistent metadata.
    [[nodiscard]] Result<FileManifest> load() const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace dfs
