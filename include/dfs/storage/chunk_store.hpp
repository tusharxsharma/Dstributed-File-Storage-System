#pragma once

#include "dfs/common/result.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace dfs {

inline constexpr std::size_t kMaxChunkSizeBytes = 4U * 1024U * 1024U;

struct ChunkInfo {
    std::string id;
    std::filesystem::path path;
    std::size_t size_bytes{};
    std::string sha256;
};

class ChunkStore {
public:
    explicit ChunkStore(std::filesystem::path root);

    // Persists one bounded-size chunk through a temporary file and rename.
    [[nodiscard]] Result<ChunkInfo> put(const std::string& chunk_id,
                                        const std::vector<std::byte>& data) const;

    // Reads a chunk and verifies its SHA-256 digest before returning it.
    [[nodiscard]] Result<std::vector<std::byte>> get(const std::string& chunk_id) const;

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    [[nodiscard]] Result<std::filesystem::path> path_for(const std::string& chunk_id) const;
    std::filesystem::path root_;
};

} // namespace dfs
