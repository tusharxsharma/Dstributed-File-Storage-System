#include "dfs/service/storage_node_rpc.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void usage() {
    std::cerr << "Usage:\n"
              << "  dfs_cluster_cli put CHUNK_ID SOURCE REPLICATION_FACTOR NODE_ID HOST PORT [NODE_ID HOST PORT ...]\n"
              << "  dfs_cluster_cli get CHUNK_ID DEST REPLICATION_FACTOR NODE_ID HOST PORT [NODE_ID HOST PORT ...]\n";
}
dfs::Result<std::vector<dfs::StorageNodeEndpoint>> parse_nodes(int argc, char* argv[], int start) {
    if ((argc - start) < 3 || ((argc - start) % 3) != 0)
        return dfs::Result<std::vector<dfs::StorageNodeEndpoint>>::failure("node endpoints must be NODE_ID HOST PORT triples");
    std::vector<dfs::StorageNodeEndpoint> nodes;
    for (int i = start; i < argc; i += 3) {
        unsigned port = 0U;
        const std::string port_text(argv[i + 2]);
        const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size() ||
            port == 0U || port > 65535U)
            return dfs::Result<std::vector<dfs::StorageNodeEndpoint>>::failure("invalid endpoint port");
        nodes.push_back({argv[i], argv[i + 1], static_cast<std::uint16_t>(port)});
    }
    return dfs::Result<std::vector<dfs::StorageNodeEndpoint>>::success(std::move(nodes));
}
dfs::Result<std::vector<std::byte>> read_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return dfs::Result<std::vector<std::byte>>::failure("inspect source: " + error.message());
    if (size > dfs::kMaxChunkSizeBytes) return dfs::Result<std::vector<std::byte>>::failure("source exceeds the 4 MiB chunk limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) return dfs::Result<std::vector<std::byte>>::failure("open source failed");
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    if (!data.empty()) input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input && !data.empty()) return dfs::Result<std::vector<std::byte>>::failure("read source failed");
    return dfs::Result<std::vector<std::byte>>::success(std::move(data));
}
dfs::Result<void> write_file(const std::filesystem::path& path, const std::vector<std::byte>& data) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".dfs-tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return dfs::Result<void>::failure("open temporary output failed");
        if (!data.empty()) output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) { output.close(); std::filesystem::remove(temp); return dfs::Result<void>::failure("write output failed"); }
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) { std::filesystem::remove(temp); return dfs::Result<void>::failure("publish output: " + error.message()); }
    return dfs::Result<void>::success();
}
}
int main(int argc, char* argv[]) {
    if (argc < 8) { usage(); return 2; }
    const std::string command(argv[1]);
    const std::string chunk_id(argv[2]);
    const int source_or_dest = 3;
    const int factor_index = 4;
    unsigned factor = 0U;
    const std::string factor_text(argv[factor_index]);
    const auto parsed = std::from_chars(factor_text.data(), factor_text.data() + factor_text.size(), factor);
    if (parsed.ec != std::errc{} || parsed.ptr != factor_text.data() + factor_text.size() || factor == 0U) {
        usage(); return 2;
    }
    const auto nodes = parse_nodes(argc, argv, 5);
    if (!nodes) { std::cerr << nodes.error() << '\n'; usage(); return 2; }
    dfs::DistributedChunkService service(nodes.value(), factor);
    if (command == "put") {
        const auto data = read_file(argv[source_or_dest]);
        if (!data) { std::cerr << data.error() << '\n'; return 1; }
        const auto stored = service.put_chunk(chunk_id, data.value());
        if (!stored) { std::cerr << stored.error() << '\n'; return 1; }
        std::cout << "stored " << chunk_id << " on ";
        for (const auto& id : stored.value().replica_node_ids) std::cout << id << ' ';
        std::cout << '\n';
        return 0;
    }
    if (command == "get") {
        const auto data = service.get_chunk(chunk_id);
        if (!data) { std::cerr << data.error() << '\n'; return 1; }
        const auto written = write_file(argv[source_or_dest], data.value());
        if (!written) { std::cerr << written.error() << '\n'; return 1; }
        std::cout << "retrieved " << chunk_id << " bytes=" << data.value().size() << '\n';
        return 0;
    }
    usage(); return 2;
}
