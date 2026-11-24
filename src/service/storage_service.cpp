#include "dfs/service/storage_service.hpp"

#include "dfs/storage/file_transfer.hpp"

#include <utility>

namespace dfs {

StorageService::StorageService(std::filesystem::path root)
    : root_(std::move(root)), chunks_(root_ / "chunks"), catalog_(root_ / "metadata") {}

Result<FileManifest> StorageService::upload(
    const std::filesystem::path& source, const std::string& file_id) const {
    const auto manifest = FileTransfer::upload(source, file_id, chunks_);
    if (!manifest) return Result<FileManifest>::failure(manifest.error());
    const auto saved = catalog_.save(manifest.value());
    if (!saved) {
        return Result<FileManifest>::failure(
            "chunks were stored but metadata publication failed: " + saved.error());
    }
    return manifest;
}

Result<void> StorageService::download(
    const std::string& file_id, const std::filesystem::path& destination) const {
    const auto manifest = catalog_.load(file_id);
    if (!manifest) return Result<void>::failure(manifest.error());
    return FileTransfer::download(manifest.value(), chunks_, destination);
}

Result<FileManifest> StorageService::metadata(const std::string& file_id) const {
    return catalog_.load(file_id);
}

Result<std::vector<std::string>> StorageService::list_files() const {
    return catalog_.list();
}

} // namespace dfs
