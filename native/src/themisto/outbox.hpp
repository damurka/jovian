#ifndef THEMISTO_OUTBOX_HPP
#define THEMISTO_OUTBOX_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace themisto
{
    // Everything the supervisor sends to one WebSocket client, sent by a
    // thread of its own.
    //
    // Why: ixwebsocket sends through the connection's own thread, which
    // flushes its send buffer until it is empty and reads nothing meanwhile.
    // When the relay handed every kernel message straight to ws->send(), a
    // kernel producing messages faster than the client drained them (tens of
    // thousands a second) kept that buffer from ever emptying: the
    // connection stopped reading the client's requests for good, and the
    // session no longer answered. So:
    //   - producers (the session's poll thread, the request handlers) only
    //     push() -- no network I/O, and no lock held across one;
    //   - the sender thread takes everything queued at once and sends it as
    //     few frames as possible: frames joined with '\n' (the JSON the relay
    //     sends never contains a raw newline), up to kFrameBytes each;
    //   - and before sending more it waits while ixwebsocket still holds more
    //     than kMaxBuffered unsent bytes, so the connection thread finishes
    //     flushing and gets back to reading.
    class Outbox : public std::enable_shared_from_this<Outbox>
    {
    public:
        // `send` writes one frame (ixwebsocket's send); `buffered` is how much
        // is still unsent (ixwebsocket's bufferedAmount). Both are called on
        // the sender thread only.
        using Send = std::function<void(const std::string&)>;
        using Buffered = std::function<std::size_t()>;

        static constexpr std::size_t kFrameBytes = 1 << 20;
        static constexpr std::size_t kMaxBuffered = 1 << 20;

        static std::shared_ptr<Outbox> start(Send send, Buffered buffered)
        {
            auto outbox = std::shared_ptr<Outbox>(new Outbox(std::move(send), std::move(buffered)));
            // The thread keeps the outbox alive until it has finished, so
            // stop() never has to wait for it (it may run on the very thread
            // ixwebsocket needs to finish a send).
            std::thread([self = outbox]() { self->run(); }).detach();
            return outbox;
        }

        void push(std::string text)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopping) return;
                m_queue.push_back(std::move(text));
            }
            m_cv.notify_one();
        }

        // Sends what is queued, then ends the thread (without waiting for it).
        void stop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
            }
            m_cv.notify_all();
        }

        // Stops at once, dropping what is queued (the connection is gone).
        void abandon()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
                m_abandoned = true;
                m_queue.clear();
            }
            m_cv.notify_all();
        }

    private:
        Outbox(Send send, Buffered buffered) : m_send(std::move(send)), m_buffered(std::move(buffered)) {}

        bool abandoned()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_abandoned;
        }

        void run()
        {
            for (;;)
            {
                std::deque<std::string> batch;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                    if (m_queue.empty()) return; // stopping, nothing left
                    batch.swap(m_queue);
                }

                std::string frame;
                auto flush = [&]() {
                    if (frame.empty()) return;
                    m_send(frame);
                    frame.clear();
                    while (m_buffered() > kMaxBuffered && !abandoned())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                };
                for (auto& text : batch)
                {
                    if (abandoned()) return;
                    if (!frame.empty() && frame.size() + text.size() + 1 > kFrameBytes) flush();
                    if (!frame.empty()) frame += '\n';
                    frame += text;
                }
                flush();
            }
        }

        Send m_send;
        Buffered m_buffered;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<std::string> m_queue;
        bool m_stopping = false;
        bool m_abandoned = false;
    };
}

#endif
