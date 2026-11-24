#include "dfs/service/storage_service.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

class StorageServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dfs-service-test-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    std::filesystem::path root_;
    std::filesystem::path write_source(const std::string& name, const std::string& data) {
        const auto path = root_ / name;
        std::ofstream output(path, std::ios::binary);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        return path;
    }
    static std::vector<char> read_all(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
    }
};

} // namespace

TEST_F(StorageServiceTest, UploadMetadataListAndDownloadRoundTrip) {
    const std::string payload = "metadata is persisted before the client receives success";
    const auto source = write_source("source.bin", payload);
    const dfs::StorageService service(root_ / "store");

    const auto uploaded = service.upload(source, "round-trip");
    ASSERT_TRUE(uploaded) << uploaded.error();
    EXPECT_EQ(uploaded.value().total_size_bytes, payload.size());

    const auto metadata = service.metadata("round-trip");
    ASSERT_TRUE(metadata) << metadata.error();
    EXPECT_EQ(metadata.value().sha256, uploaded.value().sha256);

    const auto listed = service.list_files();
    ASSERT_TRUE(listed) << listed.error();
    EXPECT_EQ(listed.value(), (std::vector<std::string>{"round-trip"}));

    const auto destination = root_ / "nested" / "restored.bin";
    const auto downloaded = dfs::StorageService(root_ / "store").download("round-trip", destination);
    ASSERT_TRUE(downloaded) << downloaded.error();
    EXPECT_EQ(read_all(destination), read_all(source));
}

TEST_F(StorageServiceTest, HandlesEmptyFile) {
    const auto source = write_source("empty.bin", "");
    const dfs::StorageService service(root_ / "store");
    ASSERT_TRUE(service.upload(source, "empty-file"));
    const auto destination = root_ / "empty-restored.bin";
    ASSERT_TRUE(service.download("empty-file", destination));
    EXPECT_EQ(std::filesystem::file_size(destination), 0U);
}

TEST_F(StorageServiceTest, PropagatesMissingSourceAndMetadataErrors) {
    const dfs::StorageService service(root_ / "store");
    EXPECT_FALSE(service.upload(root_ / "absent.bin", "missing-source"));
    EXPECT_FALSE(service.download("absent-id", root_ / "out.bin"));
    EXPECT_FALSE(service.metadata("../unsafe"));
}
