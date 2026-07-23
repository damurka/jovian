#ifndef DATASUITE_CONTEXT_HPP
#define DATASUITE_CONTEXT_HPP

#include <memory>

#include "datasuite.hpp"

namespace datasuite
{

    template <class T>
    class ContextImpl;

    class DATASUITE_API Context
    {
    public:

        virtual ~Context() = default;

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        template <class T>
        T& getWrappedContext();

    protected:

        Context() = default;
    };

    template <class T>
    class ContextImpl : public Context
    {

    public:

        template <class... U>
        ContextImpl(U&&... u)
            : m_context(std::forward<U>(u)...)
        {
        }

        virtual ~ContextImpl() = default;

        T m_context;
    };

    template <class T>
    T& Context::getWrappedContext()
    {
        auto* impl = static_cast<ContextImpl<T>*>(this);
        return impl->m_context;
    };

    DATASUITE_API
    std::unique_ptr<Context> makeZmqContext();

}

#endif
