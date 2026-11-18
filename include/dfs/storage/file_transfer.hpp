#pragma once

#include "dfs/common/result.hpp"
#include "dfs/storage/chunk_store.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dfs {

struct ChunkReference {
    std::uint64_t sequence{};
    std::string chunk_id;
    std::uint64_t size_bytes{};
    std::string sha256;
};

struct FileManifest {
    std::string file_id;
    std::uint64_t total_size_bytes{};
    std::string sha256;
    std::vector<ChunkReference> chunks;
};

class FileTransfer {
public:
    [[nodiscard]] static Result<FileManifest> upload(
        const std::filesystem::path& source,
        const std::string& file_id,
        const ChunkStore& store);

    [[nodiscard]] static Result<void> download(
        const FileManifest& manifest,
        const ChunkStore& store,
        const std::filesystem::path& destination);
};

} // namespace dfs
