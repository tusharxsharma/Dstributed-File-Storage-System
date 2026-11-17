#include "dfs/common/checksum.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace {
std::vector<std::byte> to_bytes(std::string_view text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char ch : text) result.push_back(static_cast<std::byte>(ch));
    return result;
}
}

TEST(ChecksumTest, ComputesKnownSha256Digest) {
    const auto input = to_bytes("abc");
    EXPECT_EQ(dfs::Checksum::sha256(input),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(ChecksumTest, HandlesEmptyInput) {
    const std::vector<std::byte> input;
    EXPECT_EQ(dfs::Checksum::sha256(input),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
