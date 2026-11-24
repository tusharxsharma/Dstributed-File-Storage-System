#include "dfs/common/checksum.hpp"
#include "dfs/service/metadata_catalog.hpp"
#include "dfs/storage/manifest_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class MetadataCatalogTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dfs-catalog-test-" + std::to_string(stamp));
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    std::filesystem::path root_;

    static dfs::FileManifest empty_manifest(const std::string& id) {
        const std::vector<std::byte> empty;
        return dfs::FileManifest{id, 0U, dfs::Checksum::sha256(empty), {}};
    }
};

} // namespace

TEST_F(MetadataCatalogTest, SavesAndReloadsAcrossInstancesAndListsSortedIds) {
    dfs::MetadataCatalog catalog(root_);
    ASSERT_TRUE(catalog.save(empty_manifest("zeta"))) ;
    ASSERT_TRUE(catalog.save(empty_manifest("alpha")));

    const auto reloaded = dfs::MetadataCatalog(root_).load("alpha");
    ASSERT_TRUE(reloaded) << reloaded.error();
    EXPECT_EQ(reloaded.value().file_id, "alpha");
    EXPECT_EQ(reloaded.value().total_size_bytes, 0U);

    const auto listed = catalog.list();
    ASSERT_TRUE(listed) << listed.error();
    EXPECT_EQ(listed.value(), (std::vector<std::string>{"alpha", "zeta"}));
}

TEST_F(MetadataCatalogTest, RejectsUnsafeIds) {
    dfs::MetadataCatalog catalog(root_);
    EXPECT_FALSE(catalog.load("../outside"));
    EXPECT_FALSE(catalog.save(empty_manifest("../outside")));
}

TEST_F(MetadataCatalogTest, MissingRootListsAsEmpty) {
    const auto listed = dfs::MetadataCatalog(root_).list();
    ASSERT_TRUE(listed) << listed.error();
    EXPECT_TRUE(listed.value().empty());
}

TEST_F(MetadataCatalogTest, RejectsManifestWhoseFileIdDoesNotMatchCatalogKey) {
    std::filesystem::create_directories(root_);
    const auto mismatched = empty_manifest("stored-id");
    ASSERT_TRUE(dfs::ManifestStore(root_ / "requested-id.manifest").save(mismatched));

    const auto loaded = dfs::MetadataCatalog(root_).load("requested-id");
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("does not match catalog key"), std::string::npos);
}
