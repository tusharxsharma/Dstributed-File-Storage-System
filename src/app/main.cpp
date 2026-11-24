#include "dfs/service/storage_service.hpp"

#include <iostream>
#include <string>

namespace {

void usage() {
    std::cerr
        << "Usage:\n"
        << "  dfs_cli ROOT list\n"
        << "  dfs_cli ROOT upload FILE_ID SOURCE\n"
        << "  dfs_cli ROOT download FILE_ID DESTINATION\n"
        << "  dfs_cli ROOT inspect FILE_ID\n";
}

int fail(const std::string& message) {
    std::cerr << "error: " << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage();
        return 2;
    }

    const std::filesystem::path root(argv[1]);
    const std::string command(argv[2]);
    const dfs::StorageService service(root);

    if (command == "list" && argc == 3) {
        const auto files = service.list_files();
        if (!files) return fail(files.error());
        if (files.value().empty()) {
            std::cout << "No files stored.\n";
            return 0;
        }
        for (const auto& id : files.value()) std::cout << id << '\n';
        return 0;
    }

    if (command == "upload" && argc == 5) {
        const auto uploaded = service.upload(argv[4], argv[3]);
        if (!uploaded) return fail(uploaded.error());
        const auto& manifest = uploaded.value();
        std::cout << "uploaded " << manifest.file_id
                  << " bytes=" << manifest.total_size_bytes
                  << " chunks=" << manifest.chunks.size()
                  << " sha256=" << manifest.sha256 << '\n';
        return 0;
    }

    if (command == "download" && argc == 5) {
        const auto downloaded = service.download(argv[3], argv[4]);
        if (!downloaded) return fail(downloaded.error());
        std::cout << "downloaded " << argv[3] << " to " << argv[4] << '\n';
        return 0;
    }

    if (command == "inspect" && argc == 4) {
        const auto manifest = service.metadata(argv[3]);
        if (!manifest) return fail(manifest.error());
        const auto& value = manifest.value();
        std::cout << "file_id: " << value.file_id << '\n'
                  << "size_bytes: " << value.total_size_bytes << '\n'
                  << "chunks: " << value.chunks.size() << '\n'
                  << "sha256: " << value.sha256 << '\n';
        for (const auto& chunk : value.chunks) {
            std::cout << "  " << chunk.sequence << " " << chunk.chunk_id
                      << " " << chunk.size_bytes << " " << chunk.sha256 << '\n';
        }
        return 0;
    }

    usage();
    return 2;
}
