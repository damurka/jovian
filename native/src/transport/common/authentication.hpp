#ifndef DATASUITE_AUTHENTICATION_HPP
#define DATASUITE_AUTHENTICATION_HPP

#include <memory>
#include <string>

namespace datasuite
{
    class RawBuffer
    {
    public:

        RawBuffer(const unsigned char* data,
            size_t size);

        const unsigned char* data() const;
        size_t size() const;

    private:

        const unsigned char* m_data;
        size_t m_size;
    };

    class Authentication
    {
    public:

        virtual ~Authentication() = default;

        Authentication(const Authentication&) = delete;
        Authentication& operator=(const Authentication&) = delete;

        Authentication(Authentication&&) = delete;
        Authentication& operator=(Authentication&&) = delete;

        std::string sign(const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const;

        bool verify(const RawBuffer& signature,
            const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const;

        std::string sign(const RawBuffer& content) const;
        bool verify(const RawBuffer& signature, const RawBuffer& content) const;

    protected:

        Authentication() = default;

    private:

        virtual std::string sign_impl(const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const = 0;

        virtual bool verify_impl(const RawBuffer& signature,
            const RawBuffer& header,
            const RawBuffer& parent_header,
            const RawBuffer& meta_data,
            const RawBuffer& content) const = 0;

        virtual std::string sign_impl(const RawBuffer& content) const = 0;
        virtual bool verify_impl(const RawBuffer& signature, const RawBuffer& content) const = 0;
    };

    std::unique_ptr<Authentication> make_authentication(const std::string& scheme,
        const std::string& key);
}

#endif
