#include "dfs/common/checksum.hpp"
#include "dfs/storage/file_transfer.hpp"
#include "dfs/storage/manifest_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif
#include <vector>
#include <memory>

namespace {
class ManifestStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("dfs-manifest-test-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    std::filesystem::path root_;

    static std::vector<std::byte> bytes(std::string_view value) {
        std::vector<std::byte> result;
        result.reserve(value.size());
        for (const char ch : value) result.push_back(static_cast<std::byte>(ch));
        return result;
    }

    static dfs::FileManifest sample_manifest() {
        const auto payload = bytes("manifest payload");
        const std::string digest = dfs::Checksum::sha256(payload);
        return dfs::FileManifest{
            "sample-file",
            static_cast<std::uint64_t>(payload.size()),
            digest,
            {{0U, "sample-file-0", static_cast<std::uint64_t>(payload.size()), digest}}
        };
    }

    static std::vector<char> read_all(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
    }

    static void write_all(const std::filesystem::path& path, const std::vector<char>& data) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
};
} // namespace

TEST_F(ManifestStoreTest, SavesAndReloadsManifestDeterministically) {
    const auto manifest = sample_manifest();
    const auto path = root_ / "metadata" / "sample.manifest";
    const dfs::ManifestStore store(path);

    const auto saved = store.save(manifest);
    ASSERT_TRUE(saved) << saved.error();
    const auto first_encoding = read_all(path);

    const auto loaded = store.load();
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_EQ(loaded.value().file_id, manifest.file_id);
    EXPECT_EQ(loaded.value().total_size_bytes, manifest.total_size_bytes);
    EXPECT_EQ(loaded.value().sha256, manifest.sha256);
    ASSERT_EQ(loaded.value().chunks.size(), manifest.chunks.size());
    EXPECT_EQ(loaded.value().chunks[0].sequence, manifest.chunks[0].sequence);
    EXPECT_EQ(loaded.value().chunks[0].chunk_id, manifest.chunks[0].chunk_id);
    EXPECT_EQ(loaded.value().chunks[0].size_bytes, manifest.chunks[0].size_bytes);
    EXPECT_EQ(loaded.value().chunks[0].sha256, manifest.chunks[0].sha256);

    ASSERT_TRUE(store.save(manifest));
    EXPECT_EQ(read_all(path), first_encoding);
}

TEST_F(ManifestStoreTest, RoundTripsEmptyFileManifest) {
    const std::vector<std::byte> empty;
    const dfs::FileManifest manifest{"empty-file", 0U, dfs::Checksum::sha256(empty), {}};
    const dfs::ManifestStore store(root_ / "empty.manifest");

    ASSERT_TRUE(store.save(manifest));
    const auto loaded = store.load();
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_EQ(loaded.value().file_id, "empty-file");
    EXPECT_EQ(loaded.value().total_size_bytes, 0U);
    EXPECT_TRUE(loaded.value().chunks.empty());
}

TEST_F(ManifestStoreTest, PersistsUploadedManifestForLaterReconstruction) {
    const auto source = root_ / "source.bin";
    {
        std::ofstream output(source, std::ios::binary);
        ASSERT_TRUE(output);
        output << "a manifest must survive the transfer object's lifetime";
    }

    dfs::ChunkStore chunks(root_ / "chunks");
    const auto uploaded = dfs::FileTransfer::upload(source, "persisted-file", chunks);
    ASSERT_TRUE(uploaded) << uploaded.error();

    const auto manifest_path = root_ / "metadata" / "persisted.manifest";
    ASSERT_TRUE(dfs::ManifestStore(manifest_path).save(uploaded.value()));

    // A new store instance models a later process opening the same metadata file.
    const dfs::ManifestStore reopened_store(manifest_path);
    const auto reloaded = reopened_store.load();
    ASSERT_TRUE(reloaded) << reloaded.error();

    const auto destination = root_ / "restored.bin";
    const auto restored = dfs::FileTransfer::download(reloaded.value(), chunks, destination);
    ASSERT_TRUE(restored) << restored.error();
    EXPECT_EQ(read_all(destination), read_all(source));
}

TEST_F(ManifestStoreTest, RejectsInvalidManifestBeforeCreatingFile) {
    auto manifest = sample_manifest();
    manifest.chunks.front().sequence = 4U;
    const auto path = root_ / "invalid.manifest";
    const dfs::ManifestStore store(path);

    const auto saved = store.save(manifest);
    EXPECT_FALSE(saved);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(ManifestStoreTest, RejectsTruncatedManifest) {
    const dfs::ManifestStore store(root_ / "truncated.manifest");
    ASSERT_TRUE(store.save(sample_manifest()));
    const auto size = std::filesystem::file_size(store.path());
    std::filesystem::resize_file(store.path(), size - 1U);

    const auto loaded = store.load();
    EXPECT_FALSE(loaded);
}

TEST_F(ManifestStoreTest, RejectsUnsupportedFormatVersion) {
    const dfs::ManifestStore store(root_ / "future-version.manifest");
    ASSERT_TRUE(store.save(sample_manifest()));
    auto encoded = read_all(store.path());
    ASSERT_GT(encoded.size(), 10U);
    encoded[8] = static_cast<char>(2);
    write_all(store.path(), encoded);

    const auto loaded = store.load();
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("unsupported manifest format version"), std::string::npos);
}

TEST_F(ManifestStoreTest, DetectsManifestChecksumCorruption) {
    const dfs::ManifestStore store(root_ / "corrupt.manifest");
    ASSERT_TRUE(store.save(sample_manifest()));
    auto encoded = read_all(store.path());
    ASSERT_FALSE(encoded.empty());
    encoded.back() = encoded.back() == '0' ? '1' : '0';
    write_all(store.path(), encoded);

    const auto loaded = store.load();
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("manifest checksum mismatch"), std::string::npos);
}

TEST_F(ManifestStoreTest, RejectsOversizedFileBeforeReadingIt) {
    const dfs::ManifestStore store(root_ / "oversized.manifest");
    {
        std::ofstream output(store.path(), std::ios::binary);
        ASSERT_TRUE(output);
    }
    std::filesystem::resize_file(store.path(), dfs::kMaxManifestFileSizeBytes + 1U);

    const auto loaded = store.load();
    ASSERT_FALSE(loaded);
    EXPECT_NE(loaded.error().find("64 MiB size limit"), std::string::npos);
}

TEST_F(ManifestStoreTest, ReplacesExistingManifestAndRemovesTemporaryFile) {
    const auto path = root_ / "metadata" / "replace.manifest";
    const dfs::ManifestStore store(path);
    ASSERT_TRUE(store.save(sample_manifest()));

    const std::vector<std::byte> empty;
    const dfs::FileManifest replacement{
        "sample-file", 0U, dfs::Checksum::sha256(empty), {}};
    const auto replaced = store.save(replacement);
    ASSERT_TRUE(replaced) << replaced.error();

    const auto loaded = store.load();
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_EQ(loaded.value().file_id, replacement.file_id);
    EXPECT_EQ(loaded.value().total_size_bytes, 0U);
    EXPECT_TRUE(loaded.value().chunks.empty());

    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
        EXPECT_EQ(entry.path().filename().string().find(".tmp."), std::string::npos);
}

TEST_F(ManifestStoreTest, RejectsInvalidReplacementAndKeepsPreviousManifest) {
    const auto path = root_ / "preserve.manifest";
    const dfs::ManifestStore store(path);
    const auto original = sample_manifest();
    ASSERT_TRUE(store.save(original));

    auto invalid = original;
    invalid.chunks.front().sequence = 5U;
    const auto failed = store.save(invalid);
    EXPECT_FALSE(failed);

    const auto loaded = store.load();
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_EQ(loaded.value().sha256, original.sha256);
    EXPECT_EQ(loaded.value().total_size_bytes, original.total_size_bytes);
    EXPECT_EQ(loaded.value().chunks.front().sequence, original.chunks.front().sequence);
}


TEST_F(ManifestStoreTest, RemovesOldTempsOwnedByDeadProcessesAndIsIdempotent) {
    const auto manifest_path = root_ / "recovery.manifest";
    const dfs::ManifestStore store(manifest_path);
    const auto stale = root_ / "recovery.manifest.tmp.2147483647.7";
    write_all(stale, std::vector<char>{'s','t','a','l','e'});
    std::error_code error;
    std::filesystem::last_write_time(
        stale, std::filesystem::file_time_type::clock::now() - std::chrono::hours(48), error);
    ASSERT_FALSE(error);

    const auto cleaned = store.cleanup_stale_temporary_files(std::chrono::hours(24));
    ASSERT_TRUE(cleaned) << cleaned.error();
    EXPECT_EQ(cleaned.value(), 1U);
    EXPECT_FALSE(std::filesystem::exists(stale));

    const auto repeated = store.cleanup_stale_temporary_files(std::chrono::hours(24));
    ASSERT_TRUE(repeated) << repeated.error();
    EXPECT_EQ(repeated.value(), 0U);
}

TEST_F(ManifestStoreTest, PreservesRecentTempsEvenWhenOwnerIsDead) {
    const dfs::ManifestStore store(root_ / "recent.manifest");
    const auto recent = root_ / "recent.manifest.tmp.2147483647.8";
    write_all(recent, std::vector<char>{'r'});
    const auto cleaned = store.cleanup_stale_temporary_files(std::chrono::hours(24));
    ASSERT_TRUE(cleaned) << cleaned.error();
    EXPECT_EQ(cleaned.value(), 0U);
    EXPECT_TRUE(std::filesystem::exists(recent));
}

TEST_F(ManifestStoreTest, PreservesOldTempsOwnedByAnActiveProcess) {
#if defined(__unix__) || defined(__APPLE__)
    const dfs::ManifestStore store(root_ / "active.manifest");
    const auto active = root_ / ("active.manifest.tmp." + std::to_string(::getpid()) + ".9");
    write_all(active, std::vector<char>{'a'});
    std::error_code error;
    std::filesystem::last_write_time(
        active, std::filesystem::file_time_type::clock::now() - std::chrono::hours(48), error);
    ASSERT_FALSE(error);

    const auto cleaned = store.cleanup_stale_temporary_files(std::chrono::hours(24));
    ASSERT_TRUE(cleaned) << cleaned.error();
    EXPECT_EQ(cleaned.value(), 0U);
    EXPECT_TRUE(std::filesystem::exists(active));
#endif
}

TEST_F(ManifestStoreTest, IgnoresMalformedNamesOtherTargetsAndSymlinks) {
#if defined(__unix__) || defined(__APPLE__)
    const dfs::ManifestStore store(root_ / "target.manifest");
    const auto malformed = root_ / "target.manifest.tmp.not-a-pid.2";
    const auto other = root_ / "other.manifest.tmp.2147483647.3";
    const auto link_target = root_ / "link-target";
    const auto symlink = root_ / "target.manifest.tmp.2147483647.4";
    write_all(malformed, std::vector<char>{'m'});
    write_all(other, std::vector<char>{'o'});
    write_all(link_target, std::vector<char>{'l'});
    std::filesystem::create_symlink(link_target, symlink);
    std::error_code error;
    for (const auto& candidate : {malformed, other, link_target, symlink})
        std::filesystem::last_write_time(
            candidate, std::filesystem::file_time_type::clock::now() - std::chrono::hours(48), error);
    ASSERT_FALSE(error);

    const auto cleaned = store.cleanup_stale_temporary_files(std::chrono::hours(24));
    ASSERT_TRUE(cleaned) << cleaned.error();
    EXPECT_EQ(cleaned.value(), 0U);
    EXPECT_TRUE(std::filesystem::exists(malformed));
    EXPECT_TRUE(std::filesystem::exists(other));
    EXPECT_TRUE(std::filesystem::is_symlink(symlink));
#endif
}

TEST_F(ManifestStoreTest, RejectsNegativeCleanupAge) {
    const dfs::ManifestStore store(root_ / "negative.manifest");
    const auto cleaned = store.cleanup_stale_temporary_files(std::chrono::seconds(-1));
    EXPECT_FALSE(cleaned);
}


#if defined(__unix__) || defined(__APPLE__)
TEST_F(ManifestStoreTest, InjectedWriteFailurePreservesOldManifestAndCleansTemp) {
    const auto path = root_ / "fault.manifest";
    const dfs::ManifestStore original_store(path);
    const auto original = sample_manifest();
    ASSERT_TRUE(original_store.save(original));
    const auto original_bytes = read_all(path);
    const std::vector<std::byte> empty;
    const dfs::FileManifest replacement{"sample-file", 0U, dfs::Checksum::sha256(empty), {}};

    auto syscalls = dfs::make_default_manifest_store_syscalls();
    auto real_write = syscalls->write;
    syscalls->write = [real_write](int fd, const void* data, std::size_t size) {
        if (size >= 3U) {
            (void)real_write(fd, data, 3U);
            errno = EIO;
            return static_cast<std::ptrdiff_t>(-1);
        }
        return real_write(fd, data, size);
    };
    const dfs::ManifestStore faulty(path, syscalls);
    const auto result = faulty.save(replacement);
    EXPECT_FALSE(result);
    EXPECT_EQ(read_all(path), original_bytes);
    for (const auto& entry : std::filesystem::directory_iterator(root_))
        EXPECT_FALSE(entry.path().filename().string().starts_with("fault.manifest.tmp."));
}

TEST_F(ManifestStoreTest, InjectedFileSyncFailurePreservesOldManifest) {
    const auto path = root_ / "fault.manifest";
    ASSERT_TRUE(dfs::ManifestStore(path).save(sample_manifest()));
    const auto original_bytes = read_all(path);
    const std::vector<std::byte> empty;
    const dfs::FileManifest replacement{"sample-file", 0U, dfs::Checksum::sha256(empty), {}};

    auto syscalls = dfs::make_default_manifest_store_syscalls();
    syscalls->fsync = [](int) { errno = EIO; return -1; };
    const dfs::ManifestStore faulty(path, syscalls);
    EXPECT_FALSE(faulty.save(replacement));
    EXPECT_EQ(read_all(path), original_bytes);
    for (const auto& entry : std::filesystem::directory_iterator(root_))
        EXPECT_FALSE(entry.path().filename().string().starts_with("fault.manifest.tmp."));
}

TEST_F(ManifestStoreTest, InjectedCloseFailurePreservesOldManifest) {
    const auto path = root_ / "fault.manifest";
    ASSERT_TRUE(dfs::ManifestStore(path).save(sample_manifest()));
    const auto original_bytes = read_all(path);
    const std::vector<std::byte> empty;
    const dfs::FileManifest replacement{"sample-file", 0U, dfs::Checksum::sha256(empty), {}};

    auto syscalls = dfs::make_default_manifest_store_syscalls();
    auto real_close = syscalls->close;
    syscalls->close = [real_close](int fd) {
        const int result = real_close(fd);
        if (result == 0) { errno = EIO; return -1; }
        return result;
    };
    const dfs::ManifestStore faulty(path, syscalls);
    EXPECT_FALSE(faulty.save(replacement));
    EXPECT_EQ(read_all(path), original_bytes);
    for (const auto& entry : std::filesystem::directory_iterator(root_))
        EXPECT_FALSE(entry.path().filename().string().starts_with("fault.manifest.tmp."));
}

TEST_F(ManifestStoreTest, InjectedRenameFailurePreservesOldManifest) {
    const auto path = root_ / "fault.manifest";
    ASSERT_TRUE(dfs::ManifestStore(path).save(sample_manifest()));
    const auto original_bytes = read_all(path);
    const std::vector<std::byte> empty;
    const dfs::FileManifest replacement{"sample-file", 0U, dfs::Checksum::sha256(empty), {}};

    auto syscalls = dfs::make_default_manifest_store_syscalls();
    syscalls->rename = [](const auto&, const auto&) { errno = EIO; return -1; };
    const dfs::ManifestStore faulty(path, syscalls);
    EXPECT_FALSE(faulty.save(replacement));
    EXPECT_EQ(read_all(path), original_bytes);
    for (const auto& entry : std::filesystem::directory_iterator(root_))
        EXPECT_FALSE(entry.path().filename().string().starts_with("fault.manifest.tmp."));
}

TEST_F(ManifestStoreTest, InjectedParentDirectoryOpenFailureReportsPublishedButUncertain) {
    const auto path = root_ / "fault.manifest";
    ASSERT_TRUE(dfs::ManifestStore(path).save(sample_manifest()));
    const std::vector<std::byte> empty;
    const dfs::FileManifest replacement{"sample-file", 0U, dfs::Checksum::sha256(empty), {}};

    auto syscalls = dfs::make_default_manifest_store_syscalls();
    syscalls->open_directory = [](const auto&, int) { errno = EIO; return -1; };
    const dfs::ManifestStore faulty(path, syscalls);
    const auto result = faulty.save(replacement);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("manifest was published"), std::string::npos);
    const auto visible = dfs::ManifestStore(path).load();
    ASSERT_TRUE(visible) << visible.error();
    EXPECT_EQ(visible.value().total_size_bytes, 0U);
    EXPECT_TRUE(visible.value().chunks.empty());
}

TEST_F(ManifestStoreTest, InjectedParentDirectorySyncFailureReportsVisibleManifest) {
    const auto path = root_ / "fault.manifest";
    ASSERT_TRUE(dfs::ManifestStore(path).save(sample_manifest()));
    const std::vector<std::byte> empty;
    const dfs::FileManifest replacement{"sample-file", 0U, dfs::Checksum::sha256(empty), {}};

    auto syscalls = dfs::make_default_manifest_store_syscalls();
    auto real_fsync = syscalls->fsync;
    int call_count = 0;
    syscalls->fsync = [real_fsync, &call_count](int fd) {
        ++call_count;
        if (call_count == 2) { errno = EIO; return -1; }
        return real_fsync(fd);
    };
    const dfs::ManifestStore faulty(path, syscalls);
    const auto result = faulty.save(replacement);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("durability is uncertain"), std::string::npos);
    const auto visible = dfs::ManifestStore(path).load();
    ASSERT_TRUE(visible) << visible.error();
    EXPECT_EQ(visible.value().total_size_bytes, 0U);
    EXPECT_TRUE(visible.value().chunks.empty());
}

TEST_F(ManifestStoreTest, InjectedRecoveryRemoveFailureLeavesCandidateAndReportsError) {
    const dfs::ManifestStore store(root_ / "recovery.manifest");
    const auto candidate = root_ / "recovery.manifest.tmp.2147483647.11";
    write_all(candidate, std::vector<char>{'x'});
    std::error_code error;
    std::filesystem::last_write_time(
        candidate, std::filesystem::file_time_type::clock::now() - std::chrono::hours(48), error);
    ASSERT_FALSE(error);

    auto syscalls = dfs::make_default_manifest_store_syscalls();
    syscalls->remove_path = [](const auto&, std::error_code& error) {
        error = std::make_error_code(std::errc::permission_denied);
        return false;
    };
    const dfs::ManifestStore faulty(root_ / "recovery.manifest", syscalls);
    const auto result = faulty.cleanup_stale_temporary_files(std::chrono::hours(24));
    EXPECT_FALSE(result);
    EXPECT_TRUE(std::filesystem::exists(candidate));
}
#endif
