#pragma once

#include "dfs/common/result.hpp"
#include "dfs/service/metadata_catalog.hpp"
#include "dfs/storage/chunk_store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dfs {

class StorageService {
public:
    explicit StorageService(std::filesystem::path root);

    [[nodiscard]] Result<FileManifest> upload(
        const std::filesystem::path& source, const std::string& file_id) const;
    [[nodiscard]] Result<void> download(
        const std::string& file_id, const std::filesystem::path& destination) const;
    [[nodiscard]] Result<FileManifest> metadata(const std::string& file_id) const;
    [[nodiscard]] Result<std::vector<std::string>> list_files() const;

private:
    std::filesystem::path root_;
    ChunkStore chunks_;
    MetadataCatalog catalog_;
};

} // namespace dfs
