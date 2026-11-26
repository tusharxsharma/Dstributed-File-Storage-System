#include "dfs/service/storage_node_rpc.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace dfs {
namespace {
constexpr std::size_t kMaxFrameBytes = kMaxChunkSizeBytes + 256U;
constexpr std::uint8_t kPut = 1U;
constexpr std::uint8_t kGet = 2U;
constexpr std::uint8_t kStatus = 3U;
constexpr std::uint8_t kSuccess = 1U;
constexpr std::uint8_t kFailure = 0U;

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::byte>(value & 0xffU));
}
void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((value >> static_cast<unsigned>(shift)) & 0xffU));
}
void append_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((value >> static_cast<unsigned>(shift)) & 0xffU));
}
bool read_u16(const std::vector<std::byte>& data, std::size_t& offset, std::uint16_t& value) {
    if (offset + 2U > data.size()) return false;
    value = (static_cast<std::uint16_t>(std::to_integer<unsigned char>(data[offset])) << 8U) |
            static_cast<std::uint16_t>(std::to_integer<unsigned char>(data[offset + 1U]));
    offset += 2U;
    return true;
}
bool read_u32(const std::vector<std::byte>& data, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4U > data.size()) return false;
    value = 0U;
    for (unsigned i = 0; i < 4U; ++i)
        value = (value << 8U) | std::to_integer<unsigned char>(data[offset + i]);
    offset += 4U;
    return true;
}
bool read_u64(const std::vector<std::byte>& data, std::size_t& offset, std::uint64_t& value) {
    if (offset + 8U > data.size()) return false;
    value = 0U;
    for (unsigned i = 0; i < 8U; ++i)
        value = (value << 8U) | std::to_integer<unsigned char>(data[offset + i]);
    offset += 8U;
    return true;
}

#if !defined(_WIN32)
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
bool send_all(int fd, const std::byte* data, std::size_t size) {
    std::size_t sent = 0U;
    while (sent < size) {
        const auto count = ::send(fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}
bool recv_all(int fd, std::byte* data, std::size_t size) {
    std::size_t received = 0U;
    while (received < size) {
        const auto count = ::recv(fd, data + received, size - received, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        received += static_cast<std::size_t>(count);
    }
    return true;
}
bool send_frame(int fd, const std::vector<std::byte>& payload) {
    if (payload.size() > kMaxFrameBytes || payload.size() > UINT32_MAX) return false;
    std::vector<std::byte> header;
    append_u32(header, static_cast<std::uint32_t>(payload.size()));
    return send_all(fd, header.data(), header.size()) &&
           (payload.empty() || send_all(fd, payload.data(), payload.size()));
}
Result<std::vector<std::byte>> recv_frame(int fd) {
    std::array<std::byte, 4> header{};
    if (!recv_all(fd, header.data(), header.size()))
        return Result<std::vector<std::byte>>::failure("receive frame header failed");
    std::vector<std::byte> header_vec(header.begin(), header.end());
    std::size_t offset = 0U;
    std::uint32_t length = 0U;
    if (!read_u32(header_vec, offset, length) || length > kMaxFrameBytes)
        return Result<std::vector<std::byte>>::failure("invalid or oversized RPC frame");
    std::vector<std::byte> payload(length);
    if (!payload.empty() && !recv_all(fd, payload.data(), payload.size()))
        return Result<std::vector<std::byte>>::failure("receive frame payload failed");
    return Result<std::vector<std::byte>>::success(std::move(payload));
}
void send_error(int fd, const std::string& message) {
    std::vector<std::byte> response;
    response.push_back(static_cast<std::byte>(kFailure));
    const auto size = std::min<std::size_t>(message.size(), 1024U);
    append_u32(response, static_cast<std::uint32_t>(size));
    for (std::size_t i = 0; i < size; ++i)
        response.push_back(static_cast<std::byte>(static_cast<unsigned char>(message[i])));
    (void)send_frame(fd, response);
}
Result<int> connect_to(const StorageNodeEndpoint& endpoint) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(endpoint.port);
    const int lookup = getaddrinfo(endpoint.host.c_str(), service.c_str(), &hints, &addresses);
    if (lookup != 0) return Result<int>::failure("resolve RPC endpoint: " + std::string(gai_strerror(lookup)));
    int fd = -1;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        timeval timeout{2, 0};
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) return Result<int>::failure("connect to storage node failed");
    return Result<int>::success(fd);
}
#endif
} // namespace

StorageNodeRpcServer::StorageNodeRpcServer(StorageNode& node, std::string bind_host,
                                           std::uint16_t port)
    : node_(node), bind_host_(std::move(bind_host)), requested_port_(port) {}
StorageNodeRpcServer::~StorageNodeRpcServer() { stop(); }

Result<void> StorageNodeRpcServer::start() {
#if defined(_WIN32)
    return Result<void>::failure("RPC transport is currently POSIX-only");
#else
    if (running_.load()) return Result<void>::failure("RPC server is already running");
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(requested_port_);
    const int lookup = getaddrinfo(bind_host_.empty() ? nullptr : bind_host_.c_str(),
                                  service.c_str(), &hints, &addresses);
    if (lookup != 0) return Result<void>::failure("resolve RPC bind address: " + std::string(gai_strerror(lookup)));
    int listener = -1;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        listener = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (listener < 0) continue;
        int reuse = 1;
        (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (::bind(listener, address->ai_addr, address->ai_addrlen) == 0 &&
            ::listen(listener, 16) == 0) break;
        ::close(listener);
        listener = -1;
    }
    freeaddrinfo(addresses);
    if (listener < 0) return Result<void>::failure("bind or listen RPC socket failed");
    sockaddr_in bound{};
    socklen_t bound_size = sizeof(bound);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_size) != 0) {
        ::close(listener);
        return Result<void>::failure("read bound RPC port failed");
    }
    listener_fd_ = listener;
    bound_port_ = ntohs(bound.sin_port);
    stopping_.store(false);
    running_.store(true);
    worker_ = std::thread(&StorageNodeRpcServer::run_loop, this, listener);
    return Result<void>::success();
#endif
}

void StorageNodeRpcServer::stop() noexcept {
#if !defined(_WIN32)
    stopping_.store(true);
    const int listener = listener_fd_;
    listener_fd_ = -1;
    if (listener >= 0) {
        (void)::shutdown(listener, SHUT_RDWR);
        (void)::close(listener);
    }
    if (worker_.joinable()) worker_.join();
    running_.store(false);
#endif
}

void StorageNodeRpcServer::run_loop(int listener) noexcept {
#if !defined(_WIN32)
    while (!stopping_.load()) {
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        handle_connection(client);
        (void)::close(client);
    }
    running_.store(false);
#else
    (void)listener;
#endif
}

void StorageNodeRpcServer::handle_connection(int client) noexcept {
#if !defined(_WIN32)
    try {
        const auto frame = recv_frame(client);
        if (!frame) { send_error(client, frame.error()); return; }
        const auto& request = frame.value();
        if (request.size() < 7U) { send_error(client, "truncated RPC request"); return; }
        const auto operation = std::to_integer<std::uint8_t>(request[0]);
        std::size_t offset = 1U;
        std::uint16_t id_length = 0U;
        std::uint32_t data_length = 0U;
        if (!read_u16(request, offset, id_length) || id_length > 128U ||
            offset + id_length > request.size()) {
            send_error(client, "invalid chunk ID length");
            return;
        }
        std::string id;
        id.reserve(id_length);
        for (std::size_t i = 0; i < id_length; ++i)
            id.push_back(static_cast<char>(std::to_integer<unsigned char>(request[offset + i])));
        offset += id_length;
        if (!read_u32(request, offset, data_length) || data_length > kMaxChunkSizeBytes ||
            offset + data_length != request.size()) {
            send_error(client, "invalid RPC payload length");
            return;
        }
        std::vector<std::byte> data(request.begin() + static_cast<std::ptrdiff_t>(offset), request.end());
        std::vector<std::byte> response_data;
        if (operation == kPut) {
            const auto stored = node_.put_chunk(id, data);
            if (!stored) { send_error(client, stored.error()); return; }
        } else if (operation == kGet) {
            if (data_length != 0U) { send_error(client, "GET payload must be empty"); return; }
            const auto retrieved = node_.get_chunk(id);
            if (!retrieved) { send_error(client, retrieved.error()); return; }
            response_data = retrieved.value();
        } else if (operation == kStatus) {
            if (id_length != 0U || data_length != 0U) { send_error(client, "STATUS request must not contain a chunk ID or payload"); return; }
            const auto status = node_.status();
            if (!status) { send_error(client, status.error()); return; }
            const auto& value = status.value();
            append_u16(response_data, static_cast<std::uint16_t>(value.node_id.size()));
            for (const char ch : value.node_id) response_data.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
            append_u64(response_data, value.capacity_bytes);
            append_u64(response_data, value.used_bytes);
            append_u64(response_data, value.available_bytes);
            append_u32(response_data, static_cast<std::uint32_t>(value.chunk_count));
        } else {
            send_error(client, "unknown RPC operation");
            return;
        }
        std::vector<std::byte> response;
        response.push_back(static_cast<std::byte>(kSuccess));
        append_u32(response, static_cast<std::uint32_t>(response_data.size()));
        response.insert(response.end(), response_data.begin(), response_data.end());
        (void)send_frame(client, response);
    } catch (const std::exception& error) {
        send_error(client, error.what());
    }
#else
    (void)client;
#endif
}

StorageNodeRpcClient::StorageNodeRpcClient(StorageNodeEndpoint endpoint)
    : endpoint_(std::move(endpoint)) {}

Result<std::vector<std::byte>> StorageNodeRpcClient::request(
    std::uint8_t operation, const std::string& chunk_id,
    const std::vector<std::byte>& data) const {
#if defined(_WIN32)
    (void)operation; (void)chunk_id; (void)data;
    return Result<std::vector<std::byte>>::failure("RPC transport is currently POSIX-only");
#else
    if (chunk_id.size() > 128U || data.size() > kMaxChunkSizeBytes)
        return Result<std::vector<std::byte>>::failure("RPC request exceeds configured bounds");
    const auto connection = connect_to(endpoint_);
    if (!connection) return Result<std::vector<std::byte>>::failure(connection.error());
    const int fd = connection.value();
    std::vector<std::byte> request_payload;
    request_payload.push_back(static_cast<std::byte>(operation));
    append_u16(request_payload, static_cast<std::uint16_t>(chunk_id.size()));
    for (const char ch : chunk_id) request_payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    append_u32(request_payload, static_cast<std::uint32_t>(data.size()));
    request_payload.insert(request_payload.end(), data.begin(), data.end());
    const bool sent = send_frame(fd, request_payload);
    const auto response = sent ? recv_frame(fd) : Result<std::vector<std::byte>>::failure("send RPC request failed");
    (void)::close(fd);
    if (!response) return Result<std::vector<std::byte>>::failure(response.error());
    const auto& payload = response.value();
    std::size_t offset = 0U;
    if (payload.size() < 5U) return Result<std::vector<std::byte>>::failure("truncated RPC response");
    const auto status = std::to_integer<std::uint8_t>(payload[offset++]);
    std::uint32_t length = 0U;
    if (!read_u32(payload, offset, length) || offset + length != payload.size())
        return Result<std::vector<std::byte>>::failure("invalid RPC response length");
    if (status == kFailure) {
        std::string message;
        message.reserve(length);
        for (std::size_t i = 0; i < length; ++i)
            message.push_back(static_cast<char>(std::to_integer<unsigned char>(payload[offset + i])));
        return Result<std::vector<std::byte>>::failure(message);
    }
    if (status != kSuccess) return Result<std::vector<std::byte>>::failure("unknown RPC response status");
    return Result<std::vector<std::byte>>::success(
        std::vector<std::byte>(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end()));
#endif
}

Result<void> StorageNodeRpcClient::put_chunk(
    const std::string& chunk_id, const std::vector<std::byte>& data) const {
    const auto response = request(kPut, chunk_id, data);
    if (!response) return Result<void>::failure(response.error());
    return Result<void>::success();
}
Result<std::vector<std::byte>> StorageNodeRpcClient::get_chunk(
    const std::string& chunk_id) const {
    return request(kGet, chunk_id, {});
}
Result<StorageNodeStatus> StorageNodeRpcClient::status() const {
    const auto response = request(kStatus, {}, {});
    if (!response) return Result<StorageNodeStatus>::failure(response.error());
    const auto& data = response.value();
    std::size_t offset = 0U;
    std::uint16_t id_length = 0U;
    std::uint64_t capacity = 0U, used = 0U, available = 0U;
    std::uint32_t chunks = 0U;
    if (!read_u16(data, offset, id_length) || offset + id_length > data.size())
        return Result<StorageNodeStatus>::failure("invalid node status response");
    std::string node_id;
    for (std::size_t i = 0; i < id_length; ++i)
        node_id.push_back(static_cast<char>(std::to_integer<unsigned char>(data[offset + i])));
    offset += id_length;
    if (!read_u64(data, offset, capacity) || !read_u64(data, offset, used) ||
        !read_u64(data, offset, available) || !read_u32(data, offset, chunks) ||
        offset != data.size())
        return Result<StorageNodeStatus>::failure("invalid node status response fields");
    return Result<StorageNodeStatus>::success(
        StorageNodeStatus{node_id, {}, capacity, used, available, chunks});
}

DistributedChunkService::DistributedChunkService(
    std::vector<StorageNodeEndpoint> nodes, std::size_t replication_factor)
    : nodes_(std::move(nodes)), replication_factor_(replication_factor) {}

Result<std::vector<StorageNodeEndpoint>> DistributedChunkService::placement(
    const std::string& chunk_id) const {
    if (chunk_id.empty() || chunk_id.size() > 128U)
        return Result<std::vector<StorageNodeEndpoint>>::failure("invalid chunk ID length");
    if (nodes_.empty() || replication_factor_ == 0U || replication_factor_ > nodes_.size())
        return Result<std::vector<StorageNodeEndpoint>>::failure("invalid node set or replication factor");
    for (const auto& node : nodes_) {
        if (node.node_id.empty() || node.host.empty() || node.port == 0U)
            return Result<std::vector<StorageNodeEndpoint>>::failure("invalid storage-node endpoint");
    }
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        for (std::size_t j = i + 1U; j < nodes_.size(); ++j)
            if (nodes_[i].node_id == nodes_[j].node_id)
                return Result<std::vector<StorageNodeEndpoint>>::failure("duplicate storage-node ID");
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : chunk_id) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    const auto start = static_cast<std::size_t>(hash % nodes_.size());
    std::vector<StorageNodeEndpoint> result;
    result.reserve(replication_factor_);
    for (std::size_t i = 0; i < replication_factor_; ++i)
        result.push_back(nodes_[(start + i) % nodes_.size()]);
    return Result<std::vector<StorageNodeEndpoint>>::success(std::move(result));
}

Result<ReplicationResult> DistributedChunkService::put_chunk(
    const std::string& chunk_id, const std::vector<std::byte>& data) const {
    if (data.size() > kMaxChunkSizeBytes)
        return Result<ReplicationResult>::failure("chunk exceeds the 4 MiB size limit");
    const auto targets = placement(chunk_id);
    if (!targets) return Result<ReplicationResult>::failure(targets.error());
    ReplicationResult result{chunk_id, {}};
    std::vector<std::string> failures;
    for (const auto& target : targets.value()) {
        const auto stored = StorageNodeRpcClient(target).put_chunk(chunk_id, data);
        if (stored) result.replica_node_ids.push_back(target.node_id);
        else failures.push_back(target.node_id + ": " + stored.error());
    }
    if (result.replica_node_ids.size() != replication_factor_) {
        std::string message = "replication incomplete; stored on " +
            std::to_string(result.replica_node_ids.size()) + " of " +
            std::to_string(replication_factor_) + " targets";
        for (const auto& failure : failures) message += "; " + failure;
        return Result<ReplicationResult>::failure(std::move(message));
    }
    return Result<ReplicationResult>::success(std::move(result));
}

Result<std::vector<std::byte>> DistributedChunkService::get_chunk(
    const std::string& chunk_id) const {
    const auto targets = placement(chunk_id);
    if (!targets) return Result<std::vector<std::byte>>::failure(targets.error());
    std::string failures;
    for (const auto& target : targets.value()) {
        const auto retrieved = StorageNodeRpcClient(target).get_chunk(chunk_id);
        if (retrieved) return retrieved;
        if (!failures.empty()) failures += "; ";
        failures += target.node_id + ": " + retrieved.error();
    }
    return Result<std::vector<std::byte>>::failure("all selected replicas failed: " + failures);
}

} // namespace dfs
