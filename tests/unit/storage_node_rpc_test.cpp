#include "dfs/service/storage_node_rpc.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {
class StorageNodeRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("dfs-rpc-test-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }
    void TearDown() override {
        for (auto* server : servers_) if (server) server->stop();
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    std::filesystem::path root_;
    std::vector<dfs::StorageNodeRpcServer*> servers_;
    std::vector<std::unique_ptr<dfs::StorageNode>> nodes_;
    std::vector<std::unique_ptr<dfs::StorageNodeRpcServer>> server_objects_;

    static std::vector<std::byte> bytes(const std::string& value) {
        std::vector<std::byte> data(value.size());
        for (std::size_t i = 0; i < value.size(); ++i)
            data[i] = static_cast<std::byte>(static_cast<unsigned char>(value[i]));
        return data;
    }
    dfs::Result<dfs::StorageNodeEndpoint> add_node(const std::string& id) {
        nodes_.push_back(std::make_unique<dfs::StorageNode>(
            dfs::StorageNodeConfig{id, root_ / id, 1024U}));
        const auto initialized = nodes_.back()->initialize();
        if (!initialized) return dfs::Result<dfs::StorageNodeEndpoint>::failure(initialized.error());
        server_objects_.push_back(std::make_unique<dfs::StorageNodeRpcServer>(*nodes_.back()));
        const auto started = server_objects_.back()->start();
        if (!started) return dfs::Result<dfs::StorageNodeEndpoint>::failure(started.error());
        servers_.push_back(server_objects_.back().get());
        return dfs::Result<dfs::StorageNodeEndpoint>::success(
            {id, "127.0.0.1", server_objects_.back()->port()});
    }
};

TEST_F(StorageNodeRpcTest, PutGetAndStatusAcrossTcp) {
    const auto result = add_node("node-a");
    ASSERT_TRUE(result) << result.error();
    const auto endpoint = result.value();
    dfs::StorageNodeRpcClient client(endpoint);
    ASSERT_TRUE(client.put_chunk("rpc-chunk", bytes("hello")));
    const auto fetched = client.get_chunk("rpc-chunk");
    ASSERT_TRUE(fetched) << fetched.error();
    EXPECT_EQ(fetched.value(), bytes("hello"));
    const auto status = client.status();
    ASSERT_TRUE(status) << status.error();
    EXPECT_EQ(status.value().node_id, "node-a");
    EXPECT_EQ(status.value().used_bytes, 5U);
    EXPECT_EQ(status.value().chunk_count, 1U);
}

TEST_F(StorageNodeRpcTest, ReplicationStoresOnDistinctSelectedNodes) {
    const auto ar = add_node("node-a");
    const auto br = add_node("node-b");
    const auto cr = add_node("node-c");
    ASSERT_TRUE(ar) << ar.error();
    ASSERT_TRUE(br) << br.error();
    ASSERT_TRUE(cr) << cr.error();
    const auto a = ar.value(), b = br.value(), c = cr.value();
    dfs::DistributedChunkService cluster({a, b, c}, 2U);
    const auto stored = cluster.put_chunk("replicated", bytes("payload"));
    ASSERT_TRUE(stored) << stored.error();
    ASSERT_EQ(stored.value().replica_node_ids.size(), 2U);
    EXPECT_NE(stored.value().replica_node_ids[0], stored.value().replica_node_ids[1]);
    for (const auto& id : stored.value().replica_node_ids) {
        const auto found = std::find_if(nodes_.begin(), nodes_.end(), [&](const auto& node) {
            return node->config().node_id == id;
        });
        ASSERT_NE(found, nodes_.end());
        const auto fetched = (*found)->get_chunk("replicated");
        ASSERT_TRUE(fetched) << fetched.error();
        EXPECT_EQ(fetched.value(), bytes("payload"));
    }
}

TEST_F(StorageNodeRpcTest, RetrievalFallsBackToSecondReplicaWhenFirstIsStopped) {
    const auto ar = add_node("node-a");
    const auto br = add_node("node-b");
    const auto cr = add_node("node-c");
    ASSERT_TRUE(ar) << ar.error();
    ASSERT_TRUE(br) << br.error();
    ASSERT_TRUE(cr) << cr.error();
    const auto a = ar.value(), b = br.value(), c = cr.value();
    dfs::DistributedChunkService cluster({a, b, c}, 2U);
    const auto targets = cluster.placement("fallback");
    ASSERT_TRUE(targets) << targets.error();
    ASSERT_EQ(targets.value().size(), 2U);
    ASSERT_TRUE(cluster.put_chunk("fallback", bytes("recoverable")));
    const auto first = targets.value().front().node_id;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i]->config().node_id == first) {
            server_objects_[i]->stop();
            break;
        }
    }
    const auto fetched = cluster.get_chunk("fallback");
    ASSERT_TRUE(fetched) << fetched.error();
    EXPECT_EQ(fetched.value(), bytes("recoverable"));
}

TEST_F(StorageNodeRpcTest, RejectsInvalidReplicationFactorAndOversizedChunks) {
    const auto result = add_node("node-a");
    ASSERT_TRUE(result) << result.error();
    const auto a = result.value();
    dfs::DistributedChunkService invalid({a}, 2U);
    EXPECT_FALSE(invalid.placement("id"));
    dfs::DistributedChunkService valid({a}, 1U);
    std::vector<std::byte> oversized(dfs::kMaxChunkSizeBytes + 1U);
    EXPECT_FALSE(valid.put_chunk("id", oversized));
}

TEST_F(StorageNodeRpcTest, InvalidIdsAndMissingChunksArePropagated) {
    const auto result = add_node("node-a");
    ASSERT_TRUE(result) << result.error();
    const auto a = result.value();
    dfs::StorageNodeRpcClient client(a);
    EXPECT_FALSE(client.put_chunk("../bad", bytes("x")));
    EXPECT_FALSE(client.get_chunk("missing"));
}
} // namespace
