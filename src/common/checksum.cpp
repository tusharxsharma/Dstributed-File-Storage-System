#include "dfs/common/checksum.hpp"

#include <array>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>

namespace dfs {

std::string Checksum::sha256(std::span<const std::byte> data) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_length = 0;

    EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
    if (raw_context == nullptr) {
        throw std::runtime_error("failed to allocate OpenSSL digest context");
    }

    const auto cleanup = [](EVP_MD_CTX* context) { EVP_MD_CTX_free(context); };
    const std::unique_ptr<EVP_MD_CTX, decltype(cleanup)> context(raw_context, cleanup);

    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context.get(), data.data(), data.size()) != 1 ||
        EVP_DigestFinal_ex(context.get(), digest.data(), &digest_length) != 1) {
        throw std::runtime_error("SHA-256 calculation failed");
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_length; ++i) {
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

} // namespace dfs
