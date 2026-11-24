#include "dfs/service/metadata_catalog.hpp"

#include "dfs/storage/manifest_store.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace dfs {
namespace {

bool valid_file_id(const std::string& id) {
    if (id.empty() || id.size() > 100U) return false;
    for (const unsigned char ch : id) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (!allowed) return false;
    }
    return true;
}

std::mutex registry_mutex;
std::unordered_map<std::string, std::weak_ptr<std::mutex>> lock_registry;

std::shared_ptr<std::mutex> mutex_for_path(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const std::string key = (error ? path : absolute).lexically_normal().string();
    std::lock_guard guard(registry_mutex);
    const auto found = lock_registry.find(key);
    if (found != lock_registry.end()) {
        if (auto existing = found->second.lock()) return existing;
    }
    auto created = std::make_shared<std::mutex>();
    lock_registry[key] = created;
    if (lock_registry.size() > 256U) {
        for (auto it = lock_registry.begin(); it != lock_registry.end();) {
            if (it->second.expired()) it = lock_registry.erase(it);
            else ++it;
        }
    }
    return created;
}

std::filesystem::path manifest_path(const std::filesystem::path& root,
                                   const std::string& id) {
    return root / (id + ".manifest");
}

} // namespace

MetadataCatalog::MetadataCatalog(std::filesystem::path root) : root_(std::move(root)) {}

Result<void> MetadataCatalog::save(const FileManifest& manifest) const {
    if (!valid_file_id(manifest.file_id))
        return Result<void>::failure("file id must contain 1 to 100 safe filename characters");
    try {
        std::filesystem::create_directories(root_);
        const auto path = manifest_path(root_, manifest.file_id);
        const auto path_mutex = mutex_for_path(path);
        std::lock_guard guard(*path_mutex);
        return ManifestStore(path).save(manifest);
    } catch (const std::exception& error) {
        return Result<void>::failure(error.what());
    }
}

Result<FileManifest> MetadataCatalog::load(const std::string& file_id) const {
    if (!valid_file_id(file_id))
        return Result<FileManifest>::failure("file id must contain 1 to 100 safe filename characters");
    try {
        const auto path = manifest_path(root_, file_id);
        const auto path_mutex = mutex_for_path(path);
        std::lock_guard guard(*path_mutex);
        const auto loaded = ManifestStore(path).load();
        if (!loaded) return Result<FileManifest>::failure(loaded.error());
        if (loaded.value().file_id != file_id)
            return Result<FileManifest>::failure("manifest file id does not match catalog key");
        return loaded;
    } catch (const std::exception& error) {
        return Result<FileManifest>::failure(error.what());
    }
}

Result<std::vector<std::string>> MetadataCatalog::list() const {
    try {
        std::error_code error;
        if (!std::filesystem::exists(root_, error)) {
            if (error) return Result<std::vector<std::string>>::failure(
                "inspect metadata directory: " + error.message());
            return Result<std::vector<std::string>>::success({});
        }
        std::vector<std::string> ids;
        std::filesystem::directory_iterator iterator(root_, error);
        if (error) return Result<std::vector<std::string>>::failure(
            "scan metadata directory: " + error.message());
        const std::filesystem::directory_iterator end;
        while (iterator != end) {
            const auto entry = *iterator;
            const auto status = entry.symlink_status(error);
            if (!error && std::filesystem::is_regular_file(status) &&
                entry.path().extension() == ".manifest") {
                const std::string id = entry.path().stem().string();
                if (valid_file_id(id)) ids.push_back(id);
            }
            error.clear();
            iterator.increment(error);
            if (error) return Result<std::vector<std::string>>::failure(
                "scan metadata directory: " + error.message());
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return Result<std::vector<std::string>>::success(std::move(ids));
    } catch (const std::exception& error) {
        return Result<std::vector<std::string>>::failure(error.what());
    }
}

} // namespace dfs
