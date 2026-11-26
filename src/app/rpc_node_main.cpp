#include "dfs/service/storage_node.hpp"
#include "dfs/service/storage_node_rpc.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
constexpr std::uint64_t kDefaultCapacity = 1024ULL * 1024ULL * 1024ULL;
void usage() {
    std::cerr << "Usage: dfs_rpc_node NODE_ID ROOT HOST PORT [CAPACITY_BYTES]\n";
}
bool parse_unsigned(const std::string& text, unsigned long long& value) {
    if (text.empty() || text.front() == '-') return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
}
int main(int argc, char* argv[]) {
    if (argc < 5 || argc > 6) { usage(); return 2; }
    unsigned long long port_value = 0U;
    if (!parse_unsigned(argv[4], port_value) || port_value > 65535U) { usage(); return 2; }
    std::uint64_t capacity = kDefaultCapacity;
    if (argc == 6) {
        unsigned long long parsed_capacity = 0U;
        if (!parse_unsigned(argv[5], parsed_capacity) || parsed_capacity == 0U) { usage(); return 2; }
        capacity = static_cast<std::uint64_t>(parsed_capacity);
    }
    dfs::StorageNode node({argv[1], argv[2], capacity});
    const auto initialized = node.initialize();
    if (!initialized) { std::cerr << initialized.error() << '\n'; return 1; }
    dfs::StorageNodeRpcServer server(node, argv[3], static_cast<std::uint16_t>(port_value));
    const auto started = server.start();
    if (!started) { std::cerr << started.error() << '\n'; return 1; }
    std::cout << "node=" << argv[1] << " listening on " << argv[3] << ':' << server.port()
              << " capacity_bytes=" << capacity << "\n"
              << "Type quit to stop.\n";
    std::string line;
    while (std::getline(std::cin, line)) if (line == "quit" || line == "exit") break;
    server.stop();
    return 0;
}
