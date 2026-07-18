#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <vector>

#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#include <openssl/hmac.h>
#include <openssl/sha.h>
#else
#include <algorithm>
#include <cctype>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#endif

#include "authentication.hpp"
#include <format>

namespace datasuite
{
    // raw_buffer implementation
    raw_buffer::raw_buffer(const unsigned char* data, size_t size)
        : m_data(data), m_size(size)
    {
    }

    const unsigned char* raw_buffer::data() const
    {
        return m_data;
    }

    size_t raw_buffer::size() const
    {
        return m_size;
    }

    // Specialization of authentication using OpenSSL.
    class openssl_authentication : public authentication
    {
    public:

        openssl_authentication(const std::string& scheme,
            const std::string& key);
        virtual ~openssl_authentication();

    private:

        std::string sign_impl(const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const override;

        bool verify_impl(const raw_buffer& signature,
            const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const override;

        std::string compute_hex_signature(const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const;

        std::string sign_impl(const raw_buffer& content) const override;
        bool verify_impl(const raw_buffer& signature, const raw_buffer& content) const override;
        std::string compute_hex_signature(const raw_buffer& content) const;

        void init_hex_signature() const;
        std::string finalize_hex_signature() const;

        std::string m_key;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        const EVP_MD* m_evp;
        HMAC_CTX* m_hmac;
#else
        std::string m_hash_name;
        OSSL_PARAM m_ossl_params[2];
        EVP_MAC* m_evp_mac;
        EVP_MAC_CTX* m_evp_mac_ctx;
#endif
        mutable std::mutex m_mac_mutex;
    };

    // Specialization of authentication without any signature checking.
    class no_authentication : public authentication
    {
    public:

        no_authentication() = default;
        virtual ~no_authentication() = default;

    private:

        std::string sign_impl(const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const override;

        bool verify_impl(const raw_buffer& signature,
            const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const override;

        std::string sign_impl(const raw_buffer& content) const override;
        bool verify_impl(const raw_buffer& signature, const raw_buffer& content) const override;
    };

    std::string authentication::sign(const raw_buffer& header,
        const raw_buffer& parent_header,
        const raw_buffer& meta_data,
        const raw_buffer& content) const
    {
        return sign_impl(header, parent_header, meta_data, content);
    }

    bool authentication::verify(const raw_buffer& signature,
        const raw_buffer& header,
        const raw_buffer& parent_header,
        const raw_buffer& meta_data,
        const raw_buffer& content) const
    {
        return verify_impl(signature, header, parent_header, meta_data, content);
    }

    std::string authentication::sign(const raw_buffer& content) const
    {
        return sign_impl(content);
    }

    bool authentication::verify(const raw_buffer& signature, const raw_buffer& content) const
    {
        return verify_impl(signature, content);
    }

    std::unique_ptr<authentication> make_authentication(const std::string& scheme,
        const std::string& key)
    {
        if (scheme == "none")
        {
            return std::make_unique<no_authentication>();
        }
        else
        {
            return std::make_unique<openssl_authentication>(scheme, key);
        }
    }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
    inline const EVP_MD* asevp(const std::string& scheme)
    {
        static const std::map<std::string, const EVP_MD* (*)()> schemes = {
            {"hmac-md5", EVP_md5},
            {"hmac-sha1", EVP_sha1},
            // MDC2 is disabled by default unless enable-mdc2 is specified
            // {"hmac-mdc2", EVP_mdc2},
            {"hmac-ripemd160", EVP_ripemd160},
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
            {"hmac-blake2b512", EVP_blake2b512},
            {"hmac-blake2s256", EVP_blake2s256},
#endif
            {"hmac-sha224", EVP_sha224},
            {"hmac-sha256", EVP_sha256},
            {"hmac-sha384", EVP_sha384},
            {"hmac-sha512", EVP_sha512}
        };
        return schemes.at(scheme)();
    }
#endif

    openssl_authentication::openssl_authentication(const std::string& scheme, const std::string& key)
        : m_key(key)
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        , m_evp(asevp(scheme))
#else
        , m_evp_mac(nullptr)
        , m_evp_mac_ctx(nullptr)
#endif

    {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        // OpenSSL 1.0.x
        m_hmac = new HMAC_CTX();
        HMAC_CTX_init(m_hmac);
#elif OPENSSL_VERSION_NUMBER < 0x30000000L
        m_hmac = HMAC_CTX_new();
#else
        m_hash_name = scheme.substr(5);
        std::transform(m_hash_name.begin(), m_hash_name.end(), m_hash_name.begin(),
            [](unsigned char c) { return std::toupper(c); });
        m_ossl_params[0] = OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>(m_hash_name.c_str()), std::size_t(0));
        m_ossl_params[1] = OSSL_PARAM_construct_end();
        m_evp_mac = EVP_MAC_fetch(nullptr, "hmac", nullptr);
        if (!m_evp_mac)
        {
            throw std::runtime_error("Could not fetch evp_mac");
        }
        m_evp_mac_ctx = EVP_MAC_CTX_new(m_evp_mac);
        if (!m_evp_mac_ctx)
        {
            throw std::runtime_error("Could not allocate evp_mac_ctx");
        }
#endif
    }

    openssl_authentication::~openssl_authentication()
    {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        // OpenSSL 1.0.x
        HMAC_CTX_cleanup(m_hmac);
#elif OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_CTX_free(m_hmac);
#else
        EVP_MAC_CTX_free(m_evp_mac_ctx);
        EVP_MAC_free(m_evp_mac);
#endif
    }

    std::string openssl_authentication::sign_impl(const raw_buffer& header,
        const raw_buffer& parent_header,
        const raw_buffer& meta_data,
        const raw_buffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_mac_mutex);
        std::string hex_sig = compute_hex_signature(header, parent_header, meta_data, content);
        return hex_sig;
    }

    bool openssl_authentication::verify_impl(const raw_buffer& signature,
        const raw_buffer& header,
        const raw_buffer& parent_header,
        const raw_buffer& meta_data,
        const raw_buffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_mac_mutex);
        std::string hex_sig = compute_hex_signature(header, parent_header, meta_data, content);
        auto cmp = CRYPTO_memcmp(reinterpret_cast<const void*>(hex_sig.c_str()), signature.data(), hex_sig.size());
        return cmp == 0;
    }

    std::string openssl_authentication::compute_hex_signature(const raw_buffer& header,
        const raw_buffer& parent_header,
        const raw_buffer& meta_data,
        const raw_buffer& content) const
    {
        init_hex_signature();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_Update(m_hmac, header.data(), header.size());
        HMAC_Update(m_hmac, parent_header.data(), parent_header.size());
        HMAC_Update(m_hmac, meta_data.data(), meta_data.size());
        HMAC_Update(m_hmac, content.data(), content.size());
#else
        EVP_MAC_update(m_evp_mac_ctx, header.data(), header.size());
        EVP_MAC_update(m_evp_mac_ctx, parent_header.data(), parent_header.size());
        EVP_MAC_update(m_evp_mac_ctx, meta_data.data(), meta_data.size());
        EVP_MAC_update(m_evp_mac_ctx, content.data(), content.size());
#endif
        return finalize_hex_signature();
    }

    std::string openssl_authentication::sign_impl(const raw_buffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_mac_mutex);
        std::string hex_sig = compute_hex_signature(content);
        return hex_sig;
    }

    bool openssl_authentication::verify_impl(const raw_buffer& signature, const raw_buffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_mac_mutex);
        std::string hex_sig = compute_hex_signature(content);
        auto cmp = CRYPTO_memcmp(reinterpret_cast<const void*>(hex_sig.c_str()), signature.data(), hex_sig.size());
        return cmp == 0;
    }

    std::string openssl_authentication::compute_hex_signature(const raw_buffer& content) const
    {
        init_hex_signature();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_Update(m_hmac, content.data(), content.size());
#else
        EVP_MAC_update(m_evp_mac_ctx, content.data(), content.size());
#endif
        return finalize_hex_signature();
    }

    void openssl_authentication::init_hex_signature() const
    {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_Init_ex(m_hmac, m_key.c_str(), m_key.size(), m_evp, nullptr);
#else
        EVP_MAC_init(m_evp_mac_ctx, reinterpret_cast<const unsigned char*>(m_key.c_str()), m_key.size(), m_ossl_params);
#endif
    }

    std::string openssl_authentication::finalize_hex_signature() const
    {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        auto sig = std::vector<unsigned char>(EVP_MD_size(m_evp));
        HMAC_Final(m_hmac, sig.data(), nullptr);
#else
        size_t final_size(0);
        // Computes the final size
        EVP_MAC_final(m_evp_mac_ctx, nullptr, &final_size, size_t(0));
        auto sig = std::vector<unsigned char>(final_size);
        EVP_MAC_final(m_evp_mac_ctx, sig.data(), &final_size, sig.size());
#endif
        std::string hex_result;
        hex_result.reserve(sig.size() * 2); // Pre-allocate memory for exact size (2 hex chars per byte)

        for (unsigned char byte : sig) {
            // {:02x} formats the byte as exactly 2 lowercase hexadecimal characters
            std::format_to(std::back_inserter(hex_result), "{:02x}", byte);
        }

        return hex_result;
    }

    std::string no_authentication::sign_impl(const raw_buffer& /*header*/,
        const raw_buffer& /*parent_header*/,
        const raw_buffer& /*meta_data*/,
        const raw_buffer& /*content*/) const
    {
        return {};
    }

    bool no_authentication::verify_impl(const raw_buffer& /*signature*/,
        const raw_buffer& /*header*/,
        const raw_buffer& /*parent_header*/,
        const raw_buffer& /*meta_data*/,
        const raw_buffer& /*content*/) const
    {
        return true;
    }

    std::string no_authentication::sign_impl(const raw_buffer&) const
    {
        return {};
    }

    bool no_authentication::verify_impl(const raw_buffer&, const raw_buffer&) const
    {
        return true;
    }
}
