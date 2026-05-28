#include "util/secret_utils.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#if !defined(_WIN32) && defined(CPPHTTPLIB_OPENSSL_SUPPORT)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

namespace
{
typedef std::array<std::uint8_t, 32> Sha256Digest;

std::string hex_encode_buffer(const std::uint8_t* bytes, std::size_t count)
{
    static const char* kHex = "0123456789abcdef";

    std::string encoded;
    encoded.reserve(count * 2U);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint8_t byte = bytes[i];
        encoded.push_back(kHex[(byte >> 4U) & 0x0F]);
        encoded.push_back(kHex[byte & 0x0F]);
    }
    return encoded;
}

std::string hex_encode(const Sha256Digest& digest)
{
    return hex_encode_buffer(digest.data(), digest.size());
}

std::string hex_encode_bytes(const std::vector<std::uint8_t>& bytes)
{
    return hex_encode_buffer(bytes.data(), bytes.size());
}

// Compute HMAC-SHA256(key, data). On Windows uses BCrypt; on non-Windows uses OpenSSL HMAC().
// The key is the raw salt bytes; the data is the secret string.
Sha256Digest hmac_sha256(const std::vector<std::uint8_t>& key, const std::string& data)
{
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hash_object_length = 0;
    DWORD hash_length = 0;
    DWORD bytes_written = 0;
    std::vector<UCHAR> hash_object;
    Sha256Digest digest = {};

    const NTSTATUS open_status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!NT_SUCCESS(open_status))
        throw std::runtime_error("BCryptOpenAlgorithmProvider(HMAC-SHA256) failed");

    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                      reinterpret_cast<PUCHAR>(&hash_object_length),
                      sizeof(hash_object_length), &bytes_written, 0);
    BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                      reinterpret_cast<PUCHAR>(&hash_length),
                      sizeof(hash_length), &bytes_written, 0);

    if (hash_length != static_cast<DWORD>(digest.size()))
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCrypt HMAC-SHA256 unexpected digest length");
    }

    hash_object.assign(hash_object_length, 0U);
    const PUCHAR key_ptr =
        key.empty() ? nullptr : const_cast<PUCHAR>(key.data());
    const NTSTATUS create_status =
        BCryptCreateHash(algorithm, &hash, hash_object.data(),
                         static_cast<ULONG>(hash_object.size()),
                         key_ptr, static_cast<ULONG>(key.size()), 0);
    if (!NT_SUCCESS(create_status))
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptCreateHash(HMAC-SHA256) failed");
    }

    const PUCHAR data_ptr =
        data.empty() ? nullptr : reinterpret_cast<PUCHAR>(const_cast<char*>(data.data()));
    const NTSTATUS hash_status =
        BCryptHashData(hash, data_ptr, static_cast<ULONG>(data.size()), 0);
    if (!NT_SUCCESS(hash_status))
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptHashData(HMAC-SHA256) failed");
    }

    const NTSTATUS finish_status =
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!NT_SUCCESS(finish_status))
        throw std::runtime_error("BCryptFinishHash(HMAC-SHA256) failed");

    return digest;
#elif defined(CPPHTTPLIB_OPENSSL_SUPPORT)
    Sha256Digest digest = {};
    unsigned int digest_length = 0;
    const unsigned char* key_ptr = key.empty() ? nullptr : key.data();
    const unsigned char* data_ptr =
        data.empty() ? nullptr : reinterpret_cast<const unsigned char*>(data.data());
    const unsigned char* result = HMAC(EVP_sha256(), key_ptr, static_cast<int>(key.size()),
                                       data_ptr, data.size(), digest.data(), &digest_length);
    if (result == nullptr)
        throw std::runtime_error("HMAC(EVP_sha256) failed");
    if (digest_length != static_cast<unsigned int>(digest.size()))
        throw std::runtime_error("HMAC-SHA256 returned unexpected digest length");
    return digest;
#else
#error "Non-Windows HMAC-SHA256 requires CPPHTTPLIB_OPENSSL_SUPPORT"
#endif
}

#ifndef _WIN32
void throw_system_error(int error, const char* message)
{
    throw std::system_error(error, std::generic_category(), message);
}

class ScopedFileDescriptor
{
public:
    explicit ScopedFileDescriptor(int fd) : fd_(fd) {}

    ~ScopedFileDescriptor()
    {
        if (fd_ >= 0)
            close(fd_);
    }

    int get() const
    {
        return fd_;
    }

private:
    int fd_;
};

bool try_fill_random_bytes_with_getrandom(std::uint8_t* bytes, std::size_t count)
{
#if defined(__linux__)
    std::size_t offset = 0;
    while (offset < count)
    {
        const ssize_t read_result = getrandom(bytes + offset, count - offset, 0);
        if (read_result < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == ENOSYS || errno == EINVAL)
                return false;
            throw_system_error(errno, "getrandom failed");
        }
        if (read_result == 0)
            throw std::runtime_error("getrandom returned no data");
        offset += static_cast<std::size_t>(read_result);
    }
    return true;
#else
    (void)bytes;
    (void)count;
    return false;
#endif
}

ScopedFileDescriptor open_urandom()
{
    for (;;)
    {
        const int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0)
            return ScopedFileDescriptor(fd);
        if (errno != EINTR)
            throw_system_error(errno, "open(/dev/urandom) failed");
    }
}

void fill_random_bytes_from_urandom(std::uint8_t* bytes, std::size_t count)
{
    ScopedFileDescriptor urandom_fd = open_urandom();
    std::size_t offset = 0;
    while (offset < count)
    {
        const ssize_t read_result = read(urandom_fd.get(), bytes + offset, count - offset);
        if (read_result < 0)
        {
            if (errno == EINTR)
                continue;
            throw_system_error(errno, "read(/dev/urandom) failed");
        }
        if (read_result == 0)
            throw std::runtime_error("/dev/urandom returned EOF");
        offset += static_cast<std::size_t>(read_result);
    }
}
#endif

std::string base64url_encode(const std::vector<std::uint8_t>& bytes)
{
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2U) / 3U) * 4U);

    std::size_t i = 0;
    while (i + 3U <= bytes.size())
    {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[i]) << 16U) |
            (static_cast<std::uint32_t>(bytes[i + 1U]) << 8U) |
            static_cast<std::uint32_t>(bytes[i + 2U]);
        encoded.push_back(kAlphabet[(chunk >> 18U) & 0x3FU]);
        encoded.push_back(kAlphabet[(chunk >> 12U) & 0x3FU]);
        encoded.push_back(kAlphabet[(chunk >> 6U) & 0x3FU]);
        encoded.push_back(kAlphabet[chunk & 0x3FU]);
        i += 3U;
    }

    const std::size_t remaining = bytes.size() - i;
    if (remaining == 1U)
    {
        const std::uint32_t chunk = static_cast<std::uint32_t>(bytes[i]) << 16U;
        encoded.push_back(kAlphabet[(chunk >> 18U) & 0x3FU]);
        encoded.push_back(kAlphabet[(chunk >> 12U) & 0x3FU]);
    }
    else if (remaining == 2U)
    {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[i]) << 16U) |
            (static_cast<std::uint32_t>(bytes[i + 1U]) << 8U);
        encoded.push_back(kAlphabet[(chunk >> 18U) & 0x3FU]);
        encoded.push_back(kAlphabet[(chunk >> 12U) & 0x3FU]);
        encoded.push_back(kAlphabet[(chunk >> 6U) & 0x3FU]);
    }

    return encoded;
}

#ifdef _WIN32
std::vector<std::uint8_t> generate_random_bytes(std::size_t count)
{
    std::vector<std::uint8_t> bytes(count, 0U);
    const NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(),
                                            static_cast<ULONG>(count),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!NT_SUCCESS(status))
        bytes.assign(count, 0U);
    return bytes;
}
#else
// getrandom()/urandom is kept instead of OpenSSL RAND_bytes because it is a direct OS
// kernel interface that does not depend on the OpenSSL PRNG being seeded or initialized.
// This is the recommended approach for key material on Linux (man 2 getrandom).
std::vector<std::uint8_t> generate_random_bytes(std::size_t count)
{
    std::vector<std::uint8_t> bytes(count, 0U);
    if (!try_fill_random_bytes_with_getrandom(bytes.data(), bytes.size()))
        fill_random_bytes_from_urandom(bytes.data(), bytes.size());
    return bytes;
}
#endif

// Decode a hex string into raw bytes. Used to reconstruct the salt key for HMAC.
std::vector<std::uint8_t> hex_decode(const std::string& hex)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2U);
    for (std::size_t i = 0; i + 1U < hex.size(); i += 2U)
    {
        const auto nibble = [](char c) -> std::uint8_t {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
            return 0U;
        };
        bytes.push_back(static_cast<std::uint8_t>((nibble(hex[i]) << 4U) | nibble(hex[i + 1U])));
    }
    return bytes;
}

// Stores HMAC-SHA256 of the secret keyed by the raw salt bytes.
// Format: hmac-sha256$<salt_hex>$<hmac_hex>
std::string hash_with_salt(const std::vector<std::uint8_t>& salt_bytes, const std::string& secret)
{
    const std::string salt_hex = hex_encode_bytes(salt_bytes);
    return "hmac-sha256$" + salt_hex + "$" + hex_encode(hmac_sha256(salt_bytes, secret));
}

bool verify_salted_hash(const std::string& provided, const std::string& stored_hash)
{
    const std::size_t first_dollar = stored_hash.find('$');
    if (first_dollar == std::string::npos)
        return false;
    const std::size_t second_dollar = stored_hash.find('$', first_dollar + 1U);
    if (second_dollar == std::string::npos)
        return false;

    const std::string salt_hex =
        stored_hash.substr(first_dollar + 1U, second_dollar - first_dollar - 1U);
    const std::string expected = hash_with_salt(hex_decode(salt_hex), provided);
    return constant_time_equals(expected, stored_hash);
}
} // namespace

std::string generate_secret_key()
{
    return base64url_encode(generate_random_bytes(32U));
}

std::string hash_secret_key(const std::string& secret)
{
    return hash_with_salt(generate_random_bytes(16U), secret);
}

bool verify_secret_key(const std::string& provided, const std::string& stored_hash)
{
    if (stored_hash.compare(0, 12, "hmac-sha256$") == 0)
        return verify_salted_hash(provided, stored_hash);
    return false;
}

bool constant_time_equals(const std::string& left, const std::string& right)
{
    if (left.size() != right.size())
        return false;
#if !defined(_WIN32) && defined(CPPHTTPLIB_OPENSSL_SUPPORT)
    // CRYPTO_memcmp is an audited constant-time comparison from OpenSSL.
    // It avoids timing side-channels present in manual byte loops.
    return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
#else
    // On Windows the manual loop is retained; BCrypt does not expose a
    // dedicated constant-time memcmp and the existing loop is correct.
    unsigned char diff = 0U;
    for (std::size_t i = 0; i < left.size(); ++i)
        diff |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
    return diff == 0U;
#endif
}
