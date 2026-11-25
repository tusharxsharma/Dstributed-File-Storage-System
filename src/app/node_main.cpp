#include "dfs/service/storage_node.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr std::uint64_t kDefaultCapacityBytes = 1024ULL * 1024ULL * 1024ULL;

template <typename T>
dfs::Result<T> fail(const std::string& message) {
    return dfs::Result<T>::failure(message);
}

void usage() {
    std::cerr << "Usage: dfs_node NODE_ID ROOT [CAPACITY_BYTES]\n"
              << "Capacity is payload bytes; default is 1 GiB.\n";
}

dfs::Result<std::vector<std::byte>> read_chunk_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return fail<std::vector<std::byte>>("inspect source: " + error.message());
    if (size > dfs::kMaxChunkSizeBytes) {
        return fail<std::vector<std::byte>>("source exceeds the 4 MiB chunk limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return fail<std::vector<std::byte>>("unable to open source file");
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            return fail<std::vector<std::byte>>("short read from source file");
        }
    }
    return dfs::Result<std::vector<std::byte>>::success(std::move(data));
}

dfs::Result<void> publish_file(const std::filesystem::path& destination,
                               const std::vector<std::byte>& data) {
    try {
        if (!destination.parent_path().empty()) {
            std::filesystem::create_directories(destination.parent_path());
        }
        const auto temporary = destination.string() + ".dfs-node-tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return dfs::Result<void>::failure("unable to open temporary destination");
            if (!data.empty()) {
                output.write(reinterpret_cast<const char*>(data.data()),
                             static_cast<std::streamsize>(data.size()));
            }
            output.flush();
            if (!output) {
                output.close();
                std::filesystem::remove(temporary);
                return dfs::Result<void>::failure("failed while writing destination");
            }
        }
        std::error_code error;
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            std::filesystem::remove(temporary);
            return dfs::Result<void>::failure("publish destination: " + error.message());
        }
        return dfs::Result<void>::success();
    } catch (const std::exception& error) {
        return dfs::Result<void>::failure(error.what());
    }
}

void print_status(const dfs::StorageNodeStatus& status) {
    std::cout << "node_id: " << status.node_id << '\n'
              << "root: " << status.root.string() << '\n'
              << "capacity_bytes: " << status.capacity_bytes << '\n'
              << "used_bytes: " << status.used_bytes << '\n'
              << "available_bytes: " << status.available_bytes << '\n'
              << "chunks: " << status.chunk_count << '\n';
}

void print_help() {
    std::cout << "Commands:\n"
              << "  status\n"
              << "  put CHUNK_ID SOURCE_FILE\n"
              << "  get CHUNK_ID DESTINATION_FILE\n"
              << "  help\n"
              << "  quit\n";
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        usage();
        return 2;
    }

    std::uint64_t capacity = kDefaultCapacityBytes;
    if (argc == 4) {
        const std::string input(argv[3]);
        if (input.empty() || input.front() == '-') {
            usage();
            return 2;
        }
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), capacity);
        if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
            capacity == 0U) {
            usage();
            return 2;
        }
    }

    dfs::StorageNode node(dfs::StorageNodeConfig{argv[1], argv[2], capacity});
    const auto initialized = node.initialize();
    if (!initialized) {
        std::cerr << "error: " << initialized.error() << '\n';
        return 1;
    }
    const auto initial_status = node.status();
    if (!initial_status) {
        std::cerr << "error: " << initial_status.error() << '\n';
        return 1;
    }
    std::cout << "Storage node running. Type help for commands.\n";
    print_status(initial_status.value());

    std::string line;
    while (std::cout << "dfs-node> " && std::getline(std::cin, line)) {
        std::istringstream command(line);
        std::string verb;
        command >> verb;
        if (verb.empty()) continue;
        if (verb == "quit" || verb == "exit") break;
        if (verb == "help") {
            print_help();
            continue;
        }
        if (verb == "status") {
            const auto status = node.status();
            if (!status) std::cerr << "error: " << status.error() << '\n';
            else print_status(status.value());
            continue;
        }
        if (verb == "put") {
            std::string chunk_id;
            std::string source;
            std::string extra;
            if (!(command >> chunk_id >> source) || (command >> extra)) {
                std::cerr << "usage: put CHUNK_ID SOURCE_FILE\n";
                continue;
            }
            const auto data = read_chunk_file(source);
            if (!data) {
                std::cerr << "error: " << data.error() << '\n';
                continue;
            }
            const auto stored = node.put_chunk(chunk_id, data.value());
            if (!stored) std::cerr << "error: " << stored.error() << '\n';
            else std::cout << "stored " << stored.value().id << " bytes="
                           << stored.value().size_bytes << " sha256="
                           << stored.value().sha256 << '\n';
            continue;
        }
        if (verb == "get") {
            std::string chunk_id;
            std::string destination;
            std::string extra;
            if (!(command >> chunk_id >> destination) || (command >> extra)) {
                std::cerr << "usage: get CHUNK_ID DESTINATION_FILE\n";
                continue;
            }
            const auto data = node.get_chunk(chunk_id);
            if (!data) {
                std::cerr << "error: " << data.error() << '\n';
                continue;
            }
            const auto published = publish_file(destination, data.value());
            if (!published) std::cerr << "error: " << published.error() << '\n';
            else std::cout << "retrieved " << chunk_id << " bytes="
                           << data.value().size() << '\n';
            continue;
        }
        std::cerr << "unknown command; type help\n";
    }
    std::cout << "Storage node stopped.\n";
    return 0;
}
