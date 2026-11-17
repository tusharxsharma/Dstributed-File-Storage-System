#include "dfs/storage/chunk_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
class ChunkStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("dfs-test-" + std::to_string(stamp));
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    std::filesystem::path root_;
};

std::vector<std::byte> bytes(std::string_view value) {
    std::vector<std::byte> result;
    for (const char ch : value) result.push_back(static_cast<std::byte>(ch));
    return result;
}
}

TEST_F(ChunkStoreTest, StoresAndRetrievesChunk) {
    dfs::ChunkStore store(root_);
    const auto original = bytes("distributed storage");
    const auto saved = store.put("chunk-001", original);
    ASSERT_TRUE(saved) << saved.error();
    EXPECT_EQ(saved.value().size_bytes, original.size());

    const auto loaded = store.get("chunk-001");
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_EQ(loaded.value(), original);
}

TEST_F(ChunkStoreTest, RejectsPathTraversalIds) {
    dfs::ChunkStore store(root_);
    const auto saved = store.put("../outside", bytes("nope"));
    EXPECT_FALSE(saved);
}

TEST_F(ChunkStoreTest, ReportsMissingChunk) {
    dfs::ChunkStore store(root_);
    const auto loaded = store.get("missing");
    EXPECT_FALSE(loaded);
}

TEST_F(ChunkStoreTest, DetectsCorruptedPayload) {
    dfs::ChunkStore store(root_);
    ASSERT_TRUE(store.put("chunk-corrupt", bytes("original")));

    const auto file = root_ / "chunk-corrupt.chunk";
    {
        std::fstream output(file, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(output);
        output.seekp(-1, std::ios::end);
        output.put('X');
    }

    const auto loaded = store.get("chunk-corrupt");
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("checksum mismatch"), std::string::npos);
}

TEST_F(ChunkStoreTest, RejectsOversizedChunk) {
    dfs::ChunkStore store(root_);
    std::vector<std::byte> oversized(dfs::kMaxChunkSizeBytes + 1U);
    const auto saved = store.put("too-large", oversized);
    EXPECT_FALSE(saved);
}
