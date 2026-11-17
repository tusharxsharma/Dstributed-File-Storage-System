#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace dfs {

class Checksum {
public:
    [[nodiscard]] static std::string sha256(std::span<const std::byte> data);
};

} // namespace dfs
