#include "dfs/service/storage_node.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
class StorageNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dfs-node-test-" + std::to_string(stamp));
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    std::filesystem::path root_;

    static std::vector<std::byte> payload(const std::string& value) {
        std::vector<std::byte> bytes(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(value[i]));
        }
        return bytes;
    }
};
} // namespace

TEST_F(StorageNodeTest, InitializesPersistsIdentityAndRejectsDifferentNodeId) {
    dfs::StorageNode first({"node-a", root_, 64U});
    ASSERT_TRUE(first.initialize());
    const auto status = first.status();
    ASSERT_TRUE(status) << status.error();
    EXPECT_EQ(status.value().node_id, "node-a");
    EXPECT_EQ(status.value().capacity_bytes, 64U);
    EXPECT_EQ(status.value().used_bytes, 0U);
    EXPECT_EQ(status.value().available_bytes, 64U);
    EXPECT_EQ(status.value().chunk_count, 0U);

    dfs::StorageNode same({"node-a", root_, 128U});
    EXPECT_TRUE(same.initialize());
    dfs::StorageNode different({"node-b", root_, 128U});
    EXPECT_FALSE(different.initialize());
}

TEST_F(StorageNodeTest, PutGetAndCapacityAccountingIncludingReplacement) {
    dfs::StorageNode node({"node-a", root_, 5U});
    ASSERT_TRUE(node.initialize());
    ASSERT_TRUE(node.put_chunk("chunk-1", payload("abc")));
    auto status = node.status();
    ASSERT_TRUE(status) << status.error();
    EXPECT_EQ(status.value().used_bytes, 3U);
    EXPECT_EQ(status.value().chunk_count, 1U);

    ASSERT_TRUE(node.put_chunk("chunk-1", payload("abcd")));
    status = node.status();
    ASSERT_TRUE(status) << status.error();
    EXPECT_EQ(status.value().used_bytes, 4U);
    EXPECT_EQ(status.value().available_bytes, 1U);

    EXPECT_FALSE(node.put_chunk("chunk-2", payload("xy")));
    const auto retrieved = node.get_chunk("chunk-1");
    ASSERT_TRUE(retrieved) << retrieved.error();
    EXPECT_EQ(retrieved.value(), payload("abcd"));
}

TEST_F(StorageNodeTest, RejectsUnsafeIdsAndOversizedPayloads) {
    dfs::StorageNode node({"node-a", root_, 8U});
    ASSERT_TRUE(node.initialize());
    EXPECT_FALSE(node.put_chunk("../escape", payload("x")));
    std::vector<std::byte> oversized(dfs::kMaxChunkSizeBytes + 1U);
    EXPECT_FALSE(node.put_chunk("too-large", oversized));
    EXPECT_FALSE(node.get_chunk("../escape"));
}

TEST_F(StorageNodeTest, RefusesToReplaceCorruptExistingChunk) {
    dfs::StorageNode node({"node-a", root_, 64U});
    ASSERT_TRUE(node.initialize());
    ASSERT_TRUE(node.put_chunk("corrupt-me", payload("original")));
    const auto path = root_ / "chunks" / "corrupt-me.chunk";
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file);
        file.seekp(71);
        file.put('X');
    }
    EXPECT_FALSE(node.put_chunk("corrupt-me", payload("replacement")));
    EXPECT_FALSE(node.get_chunk("corrupt-me"));
}

TEST_F(StorageNodeTest, SharedRootQuotaIsSerializedAcrossInstances) {
    dfs::StorageNode first({"node-a", root_, 8U});
    dfs::StorageNode second({"node-a", root_, 8U});
    ASSERT_TRUE(first.initialize());
    ASSERT_TRUE(second.initialize());
    const auto five = payload("12345");
    std::atomic_int successes{0};
    std::thread one([&] { if (first.put_chunk("first", five)) ++successes; });
    std::thread two([&] { if (second.put_chunk("second", five)) ++successes; });
    one.join();
    two.join();
    EXPECT_EQ(successes.load(), 1);
    const auto status = first.status();
    ASSERT_TRUE(status) << status.error();
    EXPECT_EQ(status.value().used_bytes, 5U);
    EXPECT_EQ(status.value().chunk_count, 1U);
}

TEST_F(StorageNodeTest, MissingChunkAndUninitializedNodeReturnErrors) {
    dfs::StorageNode node({"node-a", root_, 64U});
    EXPECT_FALSE(node.status());
    EXPECT_FALSE(node.get_chunk("absent"));
    ASSERT_TRUE(node.initialize());
    EXPECT_FALSE(node.get_chunk("absent"));
}
