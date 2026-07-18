#ifndef DATASUITE_AUTHENTICATION_HPP
#define DATASUITE_AUTHENTICATION_HPP

#include <memory>
#include <string>

namespace datasuite
{
    class raw_buffer
    {
    public:

        raw_buffer(const unsigned char* data,
            size_t size);

        const unsigned char* data() const;
        size_t size() const;

    private:

        const unsigned char* m_data;
        size_t m_size;
    };

    class authentication
    {
    public:

        virtual ~authentication() = default;

        authentication(const authentication&) = delete;
        authentication& operator=(const authentication&) = delete;

        authentication(authentication&&) = delete;
        authentication& operator=(authentication&&) = delete;

        std::string sign(const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const;

        bool verify(const raw_buffer& signature,
            const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const;

        std::string sign(const raw_buffer& content) const;
        bool verify(const raw_buffer& signature, const raw_buffer& content) const;

    protected:

        authentication() = default;

    private:

        virtual std::string sign_impl(const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const = 0;

        virtual bool verify_impl(const raw_buffer& signature,
            const raw_buffer& header,
            const raw_buffer& parent_header,
            const raw_buffer& meta_data,
            const raw_buffer& content) const = 0;

        virtual std::string sign_impl(const raw_buffer& content) const = 0;
        virtual bool verify_impl(const raw_buffer& signature, const raw_buffer& content) const = 0;
    };

    std::unique_ptr<authentication> make_authentication(const std::string& scheme,
        const std::string& key);
}

#endif