#include "dfs/storage/file_transfer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
class FileTransferTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("dfs-transfer-test-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
        store_root_ = root_ / "chunks";
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    std::filesystem::path root_;
    std::filesystem::path store_root_;

    std::filesystem::path write_source(const std::string& name, std::size_t size) {
        const auto path = root_ / name;
        std::ofstream output(path, std::ios::binary);
        for (std::size_t i = 0; i < size; ++i)
            output.put(static_cast<char>((i * 31U) % 251U));
        output.close();
        return path;
    }

    static std::vector<char> read_all(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
    }
};
}

TEST_F(FileTransferTest, StreamsMultipleChunksAndReconstructsOriginalFile) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("large.bin", dfs::kMaxChunkSizeBytes + 12345U);
    const auto manifest = dfs::FileTransfer::upload(source, "file-large", store);
    ASSERT_TRUE(manifest) << manifest.error();
    ASSERT_EQ(manifest.value().chunks.size(), 2U);
    EXPECT_EQ(manifest.value().chunks[0].size_bytes, dfs::kMaxChunkSizeBytes);
    EXPECT_EQ(manifest.value().chunks[1].size_bytes, 12345U);

    const auto destination = root_ / "restored" / "large.bin";
    const auto restored = dfs::FileTransfer::download(manifest.value(), store, destination);
    ASSERT_TRUE(restored) << restored.error();
    EXPECT_EQ(read_all(destination), read_all(source));
}

TEST_F(FileTransferTest, SupportsEmptyFiles) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("empty.bin", 0U);
    const auto manifest = dfs::FileTransfer::upload(source, "empty-file", store);
    ASSERT_TRUE(manifest) << manifest.error();
    EXPECT_EQ(manifest.value().total_size_bytes, 0U);
    EXPECT_TRUE(manifest.value().chunks.empty());

    const auto destination = root_ / "empty-restored.bin";
    const auto restored = dfs::FileTransfer::download(manifest.value(), store, destination);
    ASSERT_TRUE(restored) << restored.error();
    EXPECT_TRUE(std::filesystem::exists(destination));
    EXPECT_EQ(std::filesystem::file_size(destination), 0U);
}

TEST_F(FileTransferTest, HandlesExactChunkBoundaryWithoutExtraEmptyChunk) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("boundary.bin", dfs::kMaxChunkSizeBytes);
    const auto manifest = dfs::FileTransfer::upload(source, "boundary-file", store);
    ASSERT_TRUE(manifest) << manifest.error();
    ASSERT_EQ(manifest.value().chunks.size(), 1U);
    EXPECT_EQ(manifest.value().chunks.front().size_bytes, dfs::kMaxChunkSizeBytes);
}

TEST_F(FileTransferTest, HandlesPartialFinalChunk) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("partial.bin", dfs::kMaxChunkSizeBytes + 1U);
    const auto manifest = dfs::FileTransfer::upload(source, "partial-file", store);
    ASSERT_TRUE(manifest) << manifest.error();
    ASSERT_EQ(manifest.value().chunks.size(), 2U);
    EXPECT_EQ(manifest.value().chunks.back().size_bytes, 1U);
}

TEST_F(FileTransferTest, RejectsUnsafeFileIds) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("small.bin", 10U);
    EXPECT_FALSE(dfs::FileTransfer::upload(source, "../escape", store));
}

TEST_F(FileTransferTest, ReportsMissingSource) {
    dfs::ChunkStore store(store_root_);
    EXPECT_FALSE(dfs::FileTransfer::upload(root_ / "absent.bin", "missing-file", store));
}

TEST_F(FileTransferTest, RejectsMissingStoredChunkAndDoesNotPublishOutput) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("missing-chunk.bin", 20U);
    const auto manifest = dfs::FileTransfer::upload(source, "missing-chunk-file", store);
    ASSERT_TRUE(manifest) << manifest.error();

    std::filesystem::remove(store_root_ / (manifest.value().chunks.front().chunk_id + ".chunk"));
    const auto destination = root_ / "must-not-exist.bin";
    EXPECT_FALSE(dfs::FileTransfer::download(manifest.value(), store, destination));
    EXPECT_FALSE(std::filesystem::exists(destination));
}

TEST_F(FileTransferTest, RejectsManifestChecksumMismatchAndPreservesExistingDestination) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("tampered.bin", 100U);
    auto manifest = dfs::FileTransfer::upload(source, "tampered-file", store);
    ASSERT_TRUE(manifest) << manifest.error();
    manifest.value().sha256[0] = manifest.value().sha256[0] == '0' ? '1' : '0';

    const auto destination = root_ / "existing.bin";
    {
        std::ofstream output(destination, std::ios::binary);
        output << "keep-old-content";
    }
    EXPECT_FALSE(dfs::FileTransfer::download(manifest.value(), store, destination));
    EXPECT_EQ(read_all(destination), std::vector<char>(
        {'k','e','e','p','-','o','l','d','-','c','o','n','t','e','n','t'}));
}

TEST_F(FileTransferTest, RejectsNonContiguousManifestSequence) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("sequence.bin", dfs::kMaxChunkSizeBytes + 1U);
    auto manifest = dfs::FileTransfer::upload(source, "sequence-file", store);
    ASSERT_TRUE(manifest) << manifest.error();
    manifest.value().chunks[1].sequence = 9U;
    EXPECT_FALSE(dfs::FileTransfer::download(manifest.value(), store, root_ / "sequence-out.bin"));
}

TEST_F(FileTransferTest, RejectsChunkChecksumMismatchInManifest) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("chunk-hash.bin", 50U);
    auto manifest = dfs::FileTransfer::upload(source, "chunk-hash-file", store);
    ASSERT_TRUE(manifest) << manifest.error();
    manifest.value().chunks.front().sha256[0] =
        manifest.value().chunks.front().sha256[0] == '0' ? '1' : '0';
    EXPECT_FALSE(dfs::FileTransfer::download(manifest.value(), store, root_ / "chunk-hash-out.bin"));
}

TEST_F(FileTransferTest, RejectsManifestSizeMismatch) {
    dfs::ChunkStore store(store_root_);
    const auto source = write_source("size.bin", 40U);
    auto manifest = dfs::FileTransfer::upload(source, "size-file", store);
    ASSERT_TRUE(manifest) << manifest.error();
    ++manifest.value().total_size_bytes;
    EXPECT_FALSE(dfs::FileTransfer::download(manifest.value(), store, root_ / "size-out.bin"));
}
