#ifndef DATASUITE_THREAD_HPP
#define DATASUITE_THREAD_HPP

#include <thread>
#include <type_traits>

namespace datasuite
{

    /**
     * Joining std::thread
     */
    class thread
    {
    public:

        using id = std::thread::id;
        using native_handle_type = std::thread::native_handle_type;

        thread() noexcept = default;

        // Last arguments SFINAE out copy constructor
        template <class Function, class... Args,
            typename = std::enable_if_t<!std::is_same<std::decay_t<Function>, thread>::value>>
            explicit thread(Function&& f, Args&&... args);

        ~thread();

        thread(const thread&) = delete;
        thread& operator=(const thread&) = delete;

        thread(thread&&) = default;
        thread& operator=(thread&&);

        bool joinable() const noexcept;
        id get_id() const noexcept;
        native_handle_type native_handle();
        static unsigned int hardware_concurrency() noexcept;

        void join();
        void detach();
        void swap(thread& other) noexcept;

    private:

        std::thread m_thread;

    };

    /**************************
     * thread implementation *
     **************************/
    template <class Function, class... Args, typename>
    inline thread::thread(Function&& func, Args&&... args)
        : m_thread{
            std::forward<Function>(func),
            std::forward<Args>(args)...
        }

    {
    }

    inline thread::~thread()
    {
        if (joinable())
        {
            join();
        }
    }

    inline thread& thread::operator=(thread&& rhs)
    {
        if (joinable())
        {
            join();
        }
        m_thread = std::move(rhs.m_thread);
        return *this;
    }

    inline bool thread::joinable() const noexcept
    {
        return m_thread.joinable();
    }

    inline thread::id thread::get_id() const noexcept
    {
        return m_thread.get_id();
    }

    inline thread::native_handle_type thread::native_handle()
    {
        return m_thread.native_handle();
    }

    inline unsigned int thread::hardware_concurrency() noexcept
    {
        return std::thread::hardware_concurrency();
    }

    inline void thread::join()
    {
        m_thread.join();
    }

    inline void thread::detach()
    {
        m_thread.detach();
    }

    inline void thread::swap(thread& other) noexcept
    {
        m_thread.swap(other.m_thread);
    }
}

#endif