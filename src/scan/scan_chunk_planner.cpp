#include "scan/scan_chunk_planner.hpp"

#include "scan/target_validation.hpp"
#include "util/string_utils.hpp"
#include <cstdint>
#include <sstream>

namespace
{
constexpr int kChunkPrefixLength = 27;
constexpr int kMinimumChunkablePrefixLength = 16;
constexpr std::size_t kMaxScanChunkCount = 256;

struct Ipv4Cidr
{
    std::uint32_t address = 0;
    int prefix_length = 32;
};

std::uint32_t ipv4_mask(int prefix_length)
{
    return prefix_length == 0 ? 0u : (0xffffffffu << (32 - prefix_length));
}

std::uint32_t cidr_address_count(int prefix_length)
{
    return 1u << (32 - prefix_length);
}

bool parse_ipv4_cidr_target(const std::string& raw_target, Ipv4Cidr& cidr)
{
    const std::string target = util::trim(raw_target);
    const std::string::size_type slash = target.find('/');
    if (slash == std::string::npos || slash == 0 || slash != target.rfind('/'))
        return false;

    const std::string base = target.substr(0, slash);
    const std::string prefix = target.substr(slash + 1);
    return parse_ipv4_literal(base, cidr.address) &&
           parse_canonical_uint_in_range(prefix, 0, 32, cidr.prefix_length);
}

bool should_chunk_request(const ScanRequest& request, Ipv4Cidr& cidr)
{
    if (request.host_discovery_only)
        return false;
    if (!parse_ipv4_cidr_target(request.target, cidr))
        return false;
    if (cidr.prefix_length < kMinimumChunkablePrefixLength)
        return false;
    return cidr.prefix_length < kChunkPrefixLength;
}

std::string format_ipv4(std::uint32_t address)
{
    std::ostringstream out;
    out << ((address >> 24) & 0xffu) << "."
        << ((address >> 16) & 0xffu) << "."
        << ((address >> 8) & 0xffu) << "."
        << (address & 0xffu);
    return out.str();
}

std::string format_chunk_target(std::uint32_t address)
{
    return format_ipv4(address) + "/" + std::to_string(kChunkPrefixLength);
}

ScanRequest make_chunk_request(const ScanRequest& request, std::uint32_t chunk_network)
{
    ScanRequest chunk = request;
    chunk.target = format_chunk_target(chunk_network);
    return chunk;
}

std::vector<ScanRequest> make_chunk_requests(const ScanRequest& request, const Ipv4Cidr& cidr)
{
    const std::uint32_t network = cidr.address & ipv4_mask(cidr.prefix_length);
    const std::uint32_t chunk_step = cidr_address_count(kChunkPrefixLength);
    const std::uint32_t range_size = cidr_address_count(cidr.prefix_length);

    std::vector<ScanRequest> chunks;
    chunks.reserve(range_size / chunk_step);
    for (std::uint32_t offset = 0; offset < range_size; offset += chunk_step)
        chunks.push_back(make_chunk_request(request, network + offset));
    return chunks;
}
} // namespace

std::size_t max_scan_chunk_count()
{
    return kMaxScanChunkCount;
}

std::size_t scan_chunk_count(const ScanRequest& request)
{
    Ipv4Cidr cidr;
    if (!should_chunk_request(request, cidr))
        return 1;

    const std::uint32_t chunk_step = cidr_address_count(kChunkPrefixLength);
    const std::uint32_t range_size = cidr_address_count(cidr.prefix_length);
    return static_cast<std::size_t>(range_size / chunk_step);
}

ScanChunkPlan plan_scan_chunks(const ScanRequest& request)
{
    Ipv4Cidr cidr;
    if (!should_chunk_request(request, cidr))
        return {false, std::vector<ScanRequest>{request}};

    return {true, make_chunk_requests(request, cidr)};
}
