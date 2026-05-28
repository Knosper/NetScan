#include "util/secret_utils.hpp"
#include "test_output.hpp"

#include <iostream>
#include <string>

namespace
{
bool expect_true(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool salted_hashes_differ_for_same_input()
{
    const std::string secret = "mysecretkey";
    const std::string hash1 = hash_secret_key(secret);
    const std::string hash2 = hash_secret_key(secret);
    return expect_true(hash1 != hash2,
                       "two hashes of the same secret should differ (random salt)");
}

bool new_hash_uses_salted_prefix()
{
    const std::string hash = hash_secret_key("test");
    return expect_true(hash.compare(0, 12, "hmac-sha256$") == 0,
                       "new hash should start with 'hmac-sha256$'");
}

bool generated_secret_key_is_non_empty_and_not_all_zero_entropy()
{
    const std::string key = generate_secret_key();
    bool saw_non_a = false;
    for (std::size_t i = 0; i < key.size(); ++i)
    {
        if (key[i] != 'A')
        {
            saw_non_a = true;
            break;
        }
    }

    return expect_true(!key.empty() && saw_non_a,
                       "generate_secret_key should not return an empty or all-zero-derived key");
}

bool verify_accepts_own_hash()
{
    const std::string secret = "myapikey";
    const std::string hash = hash_secret_key(secret);
    return expect_true(verify_secret_key(secret, hash),
                       "verify_secret_key should accept the hash it produced");
}

bool verify_rejects_wrong_secret()
{
    const std::string hash = hash_secret_key("correctkey");
    return expect_true(!verify_secret_key("wrongkey", hash),
                       "verify_secret_key should reject a wrong secret");
}

bool verify_rejects_tampered_hash()
{
    std::string hash = hash_secret_key("mykey");
    hash[hash.size() - 1U] ^= 0xFFU;
    return expect_true(!verify_secret_key("mykey", hash),
                       "verify_secret_key should reject a tampered hash");
}

bool verify_rejects_legacy_format()
{
    // The old unsalted sha256:<hex> format must be rejected unconditionally.
    const std::string legacy_hash =
        "sha256:18efc535e34afb21823f6df95aa03d7798a28ebcb529052519116c7eb6d3ba26";

    const bool rejects_correct_key = !verify_secret_key("legacykey", legacy_hash);
    const bool rejects_wrong_key = !verify_secret_key("wrongkey", legacy_hash);
    return expect_true(rejects_correct_key && rejects_wrong_key,
                       "legacy sha256: format must be rejected regardless of provided key");
}

bool verify_rejects_empty_hash()
{
    return expect_true(!verify_secret_key("key", ""),
                       "verify_secret_key should reject empty stored hash");
}

bool verify_rejects_unknown_format()
{
    return expect_true(!verify_secret_key("key", "md5:abc123"),
                       "verify_secret_key should reject unknown hash format prefix");
}

bool verify_rejects_malformed_salted_hash()
{
    return expect_true(!verify_secret_key("key", "hmac-sha256$nosecond"),
                       "verify_secret_key should reject salted hash missing second '$'");
}

bool verify_accepts_known_compatible_hash()
{
    // Precomputed: HMAC-SHA256(key=unhex("0123456789abcdeffedcba9876543210"), data="compatibility-key")
    const std::string stored_hash =
        "hmac-sha256$0123456789abcdeffedcba9876543210$"
        "fb75905c28e2a54bcd42e6191c0be3ab5ea755a00997431ab35c9d03177df44f";

    return expect_true(verify_secret_key("compatibility-key", stored_hash),
                       "verify_secret_key should accept a precomputed hmac-sha256$salt$hex hash");
}

bool constant_time_equals_same()
{
    return expect_true(constant_time_equals("abc", "abc"),
                       "constant_time_equals should return true for equal strings");
}

bool constant_time_equals_different()
{
    return expect_true(!constant_time_equals("abc", "xyz"),
                       "constant_time_equals should return false for different strings");
}

bool constant_time_equals_different_lengths()
{
    return expect_true(!constant_time_equals("ab", "abc"),
                       "constant_time_equals should return false for different-length strings");
}
} // namespace

int main()
{
    bool all_ok = true;

    all_ok = salted_hashes_differ_for_same_input() && all_ok;
    all_ok = new_hash_uses_salted_prefix() && all_ok;
    all_ok = generated_secret_key_is_non_empty_and_not_all_zero_entropy() && all_ok;
    all_ok = verify_accepts_own_hash() && all_ok;
    all_ok = verify_rejects_wrong_secret() && all_ok;
    all_ok = verify_rejects_tampered_hash() && all_ok;
    all_ok = verify_rejects_legacy_format() && all_ok;
    all_ok = verify_rejects_empty_hash() && all_ok;
    all_ok = verify_rejects_unknown_format() && all_ok;
    all_ok = verify_rejects_malformed_salted_hash() && all_ok;
    all_ok = verify_accepts_known_compatible_hash() && all_ok;
    all_ok = constant_time_equals_same() && all_ok;
    all_ok = constant_time_equals_different() && all_ok;
    all_ok = constant_time_equals_different_lengths() && all_ok;

    return finish_test("secret_utils_test", all_ok);
}
