#ifndef DATASUITE_CONTEXT_HPP
#define DATASUITE_CONTEXT_HPP

#include <memory>

#include "datasuite.hpp"

namespace datasuite
{

    template <class T>
    class context_impl;

    class DATASUITE_API context
    {
    public:

        virtual ~context() = default;

        context(const context&) = delete;
        context& operator=(const context&) = delete;

        context(context&&) = delete;
        context& operator=(context&&) = delete;

        template <class T>
        T& get_wrapped_context();

    protected:

        context() = default;
    };

    template <class T>
    class context_impl : public context
    {

    public:

        template <class... U>
        context_impl(U&&... u)
            : m_context(std::forward<U>(u)...)
        {
        }

        virtual ~context_impl() = default;

        T m_context;
    };

    template <class T>
    T& context::get_wrapped_context()
    {
        auto* impl = static_cast<context_impl<T>*>(this);
        return impl->m_context;
    };

    DATASUITE_API
    std::unique_ptr<context> make_zmq_context();

}

#endif