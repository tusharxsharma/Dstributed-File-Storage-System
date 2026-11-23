#include "dfs/storage/chunk_store.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
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

TEST_F(ChunkStoreTest, ConcurrentSameIdWritesAndReadsRemainCompleteAcrossInstances) {
    dfs::ChunkStore first(root_);
    dfs::ChunkStore second(root_);
    const auto initial = bytes("initial-payload");
    ASSERT_TRUE(first.put("shared", initial));

    constexpr std::size_t writer_count = 8U;
    constexpr std::size_t iterations = 20U;
    std::vector<std::vector<std::byte>> payloads;
    payloads.reserve(writer_count);
    for (std::size_t index = 0; index < writer_count; ++index) {
        payloads.push_back(bytes("writer-" + std::to_string(index) + "-payload"));
    }

    const auto is_complete_payload = [&](const std::vector<std::byte>& candidate) {
        return candidate == initial ||
               std::find(payloads.begin(), payloads.end(), candidate) != payloads.end();
    };

    std::atomic<bool> reader_ok{true};
    std::atomic<bool> writers_ok{true};
    std::atomic<bool> done{false};
    std::atomic<std::size_t> successful_writes{0U};
    std::barrier start_gate(static_cast<std::ptrdiff_t>(writer_count + 1U));
    std::thread reader([&] {
        start_gate.arrive_and_wait();
        while (!done.load(std::memory_order_acquire)) {
            const auto loaded = first.get("shared");
            if (!loaded || !is_complete_payload(loaded.value())) {
                reader_ok.store(false, std::memory_order_release);
                break;
            }
        }
        const auto final_read = second.get("shared");
        if (!final_read || !is_complete_payload(final_read.value())) {
            reader_ok.store(false, std::memory_order_release);
        }
    });

    std::vector<std::thread> writers;
    writers.reserve(writer_count);
    for (std::size_t index = 0; index < writer_count; ++index) {
        writers.emplace_back([&, index] {
            start_gate.arrive_and_wait();
            const dfs::ChunkStore& store = (index % 2U == 0U) ? first : second;
            for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
                const auto saved = store.put("shared", payloads[index]);
                if (!saved) {
                    writers_ok.store(false, std::memory_order_release);
                    return;
                }
                successful_writes.fetch_add(1U, std::memory_order_relaxed);
            }
        });
    }

    for (auto& writer : writers) writer.join();
    done.store(true, std::memory_order_release);
    reader.join();

    EXPECT_TRUE(writers_ok.load());
    EXPECT_TRUE(reader_ok.load());
    EXPECT_EQ(successful_writes.load(), writer_count * iterations);

    const auto final_chunk = first.get("shared");
    ASSERT_TRUE(final_chunk) << final_chunk.error();
    EXPECT_TRUE(is_complete_payload(final_chunk.value()));

    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        EXPECT_EQ(entry.path().filename().string().find(".tmp."), std::string::npos)
            << "temporary file left behind: " << entry.path();
    }
}
