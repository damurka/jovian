#ifndef ADRASTEA_THREAD_HPP
#define ADRASTEA_THREAD_HPP

#include <thread>
#include <type_traits>

namespace adrastea
{

    /**
     * Joining std::thread
     */
    class Thread
    {
    public:

        using id = std::thread::id;
        using native_handle_type = std::thread::native_handle_type;

        Thread() noexcept = default;

        // Last arguments SFINAE out copy constructor
        template <class Function, class... Args,
            typename = std::enable_if_t<!std::is_same<std::decay_t<Function>, Thread>::value>>
            explicit Thread(Function&& f, Args&&... args);

        ~Thread();

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        Thread(Thread&&) = default;
        Thread& operator=(Thread&&);

        bool joinable() const noexcept;
        id getId() const noexcept;
        native_handle_type nativeHandle();
        static unsigned int hardwareConcurrency() noexcept;

        void join();
        void detach();
        void swap(Thread& other) noexcept;

    private:

        std::thread m_thread;

    };

    /**************************
     * Thread implementation *
     **************************/
    template <class Function, class... Args, typename>
    inline Thread::Thread(Function&& func, Args&&... args)
        : m_thread{
            std::forward<Function>(func),
            std::forward<Args>(args)...
        }

    {
    }

    inline Thread::~Thread()
    {
        if (joinable())
        {
            join();
        }
    }

    inline Thread& Thread::operator=(Thread&& rhs)
    {
        if (joinable())
        {
            join();
        }
        m_thread = std::move(rhs.m_thread);
        return *this;
    }

    inline bool Thread::joinable() const noexcept
    {
        return m_thread.joinable();
    }

    inline Thread::id Thread::getId() const noexcept
    {
        return m_thread.get_id();
    }

    inline Thread::native_handle_type Thread::nativeHandle()
    {
        return m_thread.native_handle();
    }

    inline unsigned int Thread::hardwareConcurrency() noexcept
    {
        return std::thread::hardware_concurrency();
    }

    inline void Thread::join()
    {
        m_thread.join();
    }

    inline void Thread::detach()
    {
        m_thread.detach();
    }

    inline void Thread::swap(Thread& other) noexcept
    {
        m_thread.swap(other.m_thread);
    }
}

#endif
