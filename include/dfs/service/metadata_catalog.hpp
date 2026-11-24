#pragma once

#include "dfs/common/result.hpp"
#include "dfs/storage/file_transfer.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dfs {

class MetadataCatalog {
public:
    explicit MetadataCatalog(std::filesystem::path root);

    [[nodiscard]] Result<void> save(const FileManifest& manifest) const;
    [[nodiscard]] Result<FileManifest> load(const std::string& file_id) const;
    [[nodiscard]] Result<std::vector<std::string>> list() const;

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
};

} // namespace dfs
