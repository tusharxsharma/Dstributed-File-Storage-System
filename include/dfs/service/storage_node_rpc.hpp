#pragma once

#include "dfs/common/result.hpp"
#include "dfs/service/storage_node.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace dfs {

struct StorageNodeEndpoint {
    std::string node_id;
    std::string host;
    std::uint16_t port{};
};

class StorageNodeRpcServer {
public:
    explicit StorageNodeRpcServer(StorageNode& node, std::string bind_host = "127.0.0.1",
                                  std::uint16_t port = 0U);
    ~StorageNodeRpcServer();
    StorageNodeRpcServer(const StorageNodeRpcServer&) = delete;
    StorageNodeRpcServer& operator=(const StorageNodeRpcServer&) = delete;

    [[nodiscard]] Result<void> start();
    void stop() noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept { return bound_port_; }
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

private:
    void run_loop(int listener) noexcept;
    void handle_connection(int client) noexcept;

    StorageNode& node_;
    std::string bind_host_;
    std::uint16_t requested_port_{};
    std::uint16_t bound_port_{};
    std::atomic_bool stopping_{false};
    std::atomic_bool running_{false};
    int listener_fd_{-1};
    std::thread worker_;
};

class StorageNodeRpcClient {
public:
    explicit StorageNodeRpcClient(StorageNodeEndpoint endpoint);
    [[nodiscard]] Result<void> put_chunk(
        const std::string& chunk_id, const std::vector<std::byte>& data) const;
    [[nodiscard]] Result<std::vector<std::byte>> get_chunk(
        const std::string& chunk_id) const;
    [[nodiscard]] Result<StorageNodeStatus> status() const;
    [[nodiscard]] const StorageNodeEndpoint& endpoint() const noexcept { return endpoint_; }

private:
    [[nodiscard]] Result<std::vector<std::byte>> request(
        std::uint8_t operation, const std::string& chunk_id,
        const std::vector<std::byte>& data) const;
    StorageNodeEndpoint endpoint_;
};

struct ReplicationResult {
    std::string chunk_id;
    std::vector<std::string> replica_node_ids;
};

class DistributedChunkService {
public:
    DistributedChunkService(std::vector<StorageNodeEndpoint> nodes,
                           std::size_t replication_factor);

    [[nodiscard]] Result<std::vector<StorageNodeEndpoint>> placement(
        const std::string& chunk_id) const;
    [[nodiscard]] Result<ReplicationResult> put_chunk(
        const std::string& chunk_id, const std::vector<std::byte>& data) const;
    [[nodiscard]] Result<std::vector<std::byte>> get_chunk(
        const std::string& chunk_id) const;

private:
    std::vector<StorageNodeEndpoint> nodes_;
    std::size_t replication_factor_{};
};

} // namespace dfs
