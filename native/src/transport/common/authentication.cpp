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

namespace adrastea
{
    // RawBuffer implementation
    RawBuffer::RawBuffer(const unsigned char* data, size_t size)
        : m_data(data), m_size(size)
    {
    }

    const unsigned char* RawBuffer::data() const
    {
        return m_data;
    }

    size_t RawBuffer::size() const
    {
        return m_size;
    }

    // Specialization of Authentication using OpenSSL.
    class OpensslAuthentication : public Authentication
    {
    public:

        OpensslAuthentication(const std::string& scheme,
            const std::string& key);
        virtual ~OpensslAuthentication();

    private:

        std::string signImpl(const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const override;

        bool verifyImpl(const RawBuffer& signature,
            const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const override;

        std::string computeHexSignature(const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const;

        std::string signImpl(const RawBuffer& content) const override;
        bool verifyImpl(const RawBuffer& signature, const RawBuffer& content) const override;
        std::string computeHexSignature(const RawBuffer& content) const;

        void initHexSignature() const;
        std::string finalizeHexSignature() const;

        std::string m_key;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        const EVP_MD* m_evp;
        HMAC_CTX* m_hmac;
#else
        std::string m_hashName;
        OSSL_PARAM m_osslParams[2];
        EVP_MAC* m_evpMac;
        EVP_MAC_CTX* m_evpMacCtx;
#endif
        mutable std::mutex m_macMutex;
    };

    // Specialization of Authentication without any signature checking.
    class NoAuthentication : public Authentication
    {
    public:

        NoAuthentication() = default;
        virtual ~NoAuthentication() = default;

    private:

        std::string signImpl(const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const override;

        bool verifyImpl(const RawBuffer& signature,
            const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const override;

        std::string signImpl(const RawBuffer& content) const override;
        bool verifyImpl(const RawBuffer& signature, const RawBuffer& content) const override;
    };

    std::string Authentication::sign(const RawBuffer& header,
        const RawBuffer& parent_header,
        const RawBuffer& meta_data,
        const RawBuffer& content) const
    {
        return signImpl(header, parent_header, meta_data, content);
    }

    bool Authentication::verify(const RawBuffer& signature,
        const RawBuffer& header,
        const RawBuffer& parent_header,
        const RawBuffer& meta_data,
        const RawBuffer& content) const
    {
        return verifyImpl(signature, header, parent_header, meta_data, content);
    }

    std::string Authentication::sign(const RawBuffer& content) const
    {
        return signImpl(content);
    }

    bool Authentication::verify(const RawBuffer& signature, const RawBuffer& content) const
    {
        return verifyImpl(signature, content);
    }

    std::unique_ptr<Authentication> makeAuthentication(const std::string& scheme,
        const std::string& key)
    {
        if (scheme == "none")
        {
            return std::make_unique<NoAuthentication>();
        }
        else
        {
            return std::make_unique<OpensslAuthentication>(scheme, key);
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

    OpensslAuthentication::OpensslAuthentication(const std::string& scheme, const std::string& key)
        : m_key(key)
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        , m_evp(asevp(scheme))
#else
        , m_evpMac(nullptr)
        , m_evpMacCtx(nullptr)
#endif

    {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        // OpenSSL 1.0.x
        m_hmac = new HMAC_CTX();
        HMAC_CTX_init(m_hmac);
#elif OPENSSL_VERSION_NUMBER < 0x30000000L
        m_hmac = HMAC_CTX_new();
#else
        m_hashName = scheme.substr(5);
        std::transform(m_hashName.begin(), m_hashName.end(), m_hashName.begin(),
            [](unsigned char c) { return std::toupper(c); });
        m_osslParams[0] = OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>(m_hashName.c_str()), std::size_t(0));
        m_osslParams[1] = OSSL_PARAM_construct_end();
        m_evpMac = EVP_MAC_fetch(nullptr, "hmac", nullptr);
        if (!m_evpMac)
        {
            throw std::runtime_error("Could not fetch evp_mac");
        }
        m_evpMacCtx = EVP_MAC_CTX_new(m_evpMac);
        if (!m_evpMacCtx)
        {
            throw std::runtime_error("Could not allocate evp_mac_ctx");
        }
#endif
    }

    OpensslAuthentication::~OpensslAuthentication()
    {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        // OpenSSL 1.0.x
        HMAC_CTX_cleanup(m_hmac);
#elif OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_CTX_free(m_hmac);
#else
        EVP_MAC_CTX_free(m_evpMacCtx);
        EVP_MAC_free(m_evpMac);
#endif
    }

    std::string OpensslAuthentication::signImpl(const RawBuffer& header,
        const RawBuffer& parent_header,
        const RawBuffer& meta_data,
        const RawBuffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_macMutex);
        std::string hex_sig = computeHexSignature(header, parent_header, meta_data, content);
        return hex_sig;
    }

    bool OpensslAuthentication::verifyImpl(const RawBuffer& signature,
        const RawBuffer& header,
        const RawBuffer& parent_header,
        const RawBuffer& meta_data,
        const RawBuffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_macMutex);
        std::string hex_sig = computeHexSignature(header, parent_header, meta_data, content);
        auto cmp = CRYPTO_memcmp(reinterpret_cast<const void*>(hex_sig.c_str()), signature.data(), hex_sig.size());
        return cmp == 0;
    }

    std::string OpensslAuthentication::computeHexSignature(const RawBuffer& header,
        const RawBuffer& parent_header,
        const RawBuffer& meta_data,
        const RawBuffer& content) const
    {
        initHexSignature();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_Update(m_hmac, header.data(), header.size());
        HMAC_Update(m_hmac, parent_header.data(), parent_header.size());
        HMAC_Update(m_hmac, meta_data.data(), meta_data.size());
        HMAC_Update(m_hmac, content.data(), content.size());
#else
        EVP_MAC_update(m_evpMacCtx, header.data(), header.size());
        EVP_MAC_update(m_evpMacCtx, parent_header.data(), parent_header.size());
        EVP_MAC_update(m_evpMacCtx, meta_data.data(), meta_data.size());
        EVP_MAC_update(m_evpMacCtx, content.data(), content.size());
#endif
        return finalizeHexSignature();
    }

    std::string OpensslAuthentication::signImpl(const RawBuffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_macMutex);
        std::string hex_sig = computeHexSignature(content);
        return hex_sig;
    }

    bool OpensslAuthentication::verifyImpl(const RawBuffer& signature, const RawBuffer& content) const
    {
        std::lock_guard<std::mutex> lock(m_macMutex);
        std::string hex_sig = computeHexSignature(content);
        auto cmp = CRYPTO_memcmp(reinterpret_cast<const void*>(hex_sig.c_str()), signature.data(), hex_sig.size());
        return cmp == 0;
    }

    std::string OpensslAuthentication::computeHexSignature(const RawBuffer& content) const
    {
        initHexSignature();
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_Update(m_hmac, content.data(), content.size());
#else
        EVP_MAC_update(m_evpMacCtx, content.data(), content.size());
#endif
        return finalizeHexSignature();
    }

    void OpensslAuthentication::initHexSignature() const
    {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        HMAC_Init_ex(m_hmac, m_key.c_str(), m_key.size(), m_evp, nullptr);
#else
        EVP_MAC_init(m_evpMacCtx, reinterpret_cast<const unsigned char*>(m_key.c_str()), m_key.size(), m_osslParams);
#endif
    }

    std::string OpensslAuthentication::finalizeHexSignature() const
    {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        auto sig = std::vector<unsigned char>(EVP_MD_size(m_evp));
        HMAC_Final(m_hmac, sig.data(), nullptr);
#else
        size_t final_size(0);
        // Computes the final size
        EVP_MAC_final(m_evpMacCtx, nullptr, &final_size, size_t(0));
        auto sig = std::vector<unsigned char>(final_size);
        EVP_MAC_final(m_evpMacCtx, sig.data(), &final_size, sig.size());
#endif
        std::string hex_result;
        hex_result.reserve(sig.size() * 2); // Pre-allocate memory for exact size (2 hex chars per byte)

        for (unsigned char byte : sig) {
            // {:02x} formats the byte as exactly 2 lowercase hexadecimal characters
            std::format_to(std::back_inserter(hex_result), "{:02x}", byte);
        }

        return hex_result;
    }

    std::string NoAuthentication::signImpl(const RawBuffer& /*header*/,
        const RawBuffer& /*parent_header*/,
        const RawBuffer& /*meta_data*/,
        const RawBuffer& /*content*/) const
    {
        return {};
    }

    bool NoAuthentication::verifyImpl(const RawBuffer& /*signature*/,
        const RawBuffer& /*header*/,
        const RawBuffer& /*parent_header*/,
        const RawBuffer& /*meta_data*/,
        const RawBuffer& /*content*/) const
    {
        return true;
    }

    std::string NoAuthentication::signImpl(const RawBuffer&) const
    {
        return {};
    }

    bool NoAuthentication::verifyImpl(const RawBuffer&, const RawBuffer&) const
    {
        return true;
    }
}
