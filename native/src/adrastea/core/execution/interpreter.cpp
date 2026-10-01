#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "adrastea/json.hpp"
#include "adrastea/interpreter.hpp"

namespace adrastea
{
    // The framework's global interpreter registry -- exactly one
    // Interpreter (whichever language backend is actually embedded in this
    // process, e.g. elara::RInterpreter) calls registerInterpreter() on
    // itself once constructed; everything else that needs "the" active
    // interpreter (input.cpp's blocking input request, routine.cpp's R
    // routine callbacks) goes through getInterpreter() instead of knowing
    // about any specific language backend. This used to live in
    // interpreter_r.cpp (the concrete R-specific implementation file)
    // rather than here, with getInterpreter() falling back to
    // elara::getRInterpreter() if nothing had registered yet -- a
    // dependency from adrastea (the framework) back onto elara (a specific
    // product built on it) that made Adrastea impossible to extract into
    // its own reusable target/library. That fallback was also always
    // redundant in practice: RInterpreter's constructor calls
    // registerInterpreter() and sets elara's own pointer one line apart,
    // so the two were never actually out of sync. Removed rather than
    // ported: an interpreter's own constructor not having run yet by the
    // time something calls getInterpreter() is a genuine caller-ordering
    // bug worth surfacing loudly, not something to silently paper over.
    Interpreter*& getRegisteredInterpreter()
    {
        static Interpreter* interpreter = nullptr;
        return interpreter;
    }

    bool registerInterpreter(Interpreter* new_interpreter)
    {
        Interpreter*& interp = getRegisteredInterpreter();
        if (interp != nullptr)
        {
            return false;
        }
        else
        {
            interp = new_interpreter;
            return true;
        }
    }

    Interpreter& getInterpreter()
    {
        Interpreter* interp = getRegisteredInterpreter();
        if (interp == nullptr)
        {
            throw std::runtime_error(
                "adrastea::getInterpreter() called before any Interpreter registered itself "
                "via registerInterpreter() -- this is a caller-ordering bug (the interpreter "
                "must be fully constructed before anything can request it), not a normal "
                "runtime condition.");
        }
        return *interp;
    }

    Interpreter::Interpreter()
        : m_executionCount(0)
    {
    }

    Interpreter::~Interpreter()
    {
        {
            std::lock_guard<std::mutex> lock(m_streamMutex);
            m_streamQuit = true;
        }
        m_streamCv.notify_all();
        if (m_streamFlusher.joinable())
        {
            m_streamFlusher.join();
        }
    }

    void Interpreter::configure()
    {
        configureImpl();
    }

    void Interpreter::executeRequest(RequestContext context,
        send_reply_callback callback,
        const std::string& code,
        ExecuteRequestConfig config,
        json user_expressions)
    {
        setRequestContext(std::move(context));
        m_allowStdin = config.allow_stdin;
        if (!config.silent)
        {
            ++m_executionCount;
            publishExecutionInput(code, m_executionCount);
        }
        // copy m_executionCount in a local variable to capture it in the lambda
        auto execution_count = m_executionCount;

        auto callback_impl = [this, execution_count, callback = std::move(callback)](json reply)
            {
                flushStreams();
                reply["execution_count"] = execution_count;
                callback(std::move(reply));
            };

        executeRequestImpl(
            std::move(callback_impl),
            m_executionCount,
            code,
            std::move(config),
            user_expressions
        );
    }

    json Interpreter::completeRequest(const std::string& code, int cursor_pos)
    {
        return completeRequestImpl(code, cursor_pos);
    }

    json Interpreter::inspectRequest(const std::string& code, int cursor_pos, int detail_level)
    {
        return inspectRequestImpl(code, cursor_pos, detail_level);
    }

    json Interpreter::isCompleteRequest(const std::string& code)
    {
        return isCompleteRequestImpl(code);
    }

    json Interpreter::kernelInfoRequest()
    {
        return kernelInfoRequestImpl();
    }

    bool Interpreter::answersWhileBusy(const std::string& msg_type) const
    {
        return answersWhileBusyImpl(msg_type);
    }

    void Interpreter::idle()
    {
        idleImpl();
    }

    json Interpreter::shutdownRequest(bool restart)
    {
        return shutdownRequestImpl(restart);
    }

    json Interpreter::interruptRequest()
    {
        return interruptRequestImpl();
    }

    json Interpreter::debugRequest(const json& request)
    {
        return debugRequestImpl(request);
    }

    json Interpreter::debugRequestImpl(const json& request)
    {
        return json{
            { "type", "response" },
            { "seq", 0 },
            { "request_seq", request.value("seq", 0) },
            { "success", false },
            { "command", request.value("command", "") },
            { "message", "this kernel has no debugger" }
        };
    }

    void Interpreter::publishDebugEvent(json event, bool flushOutput)
    {
        if (flushOutput)
        {
            flushStreams();
        }
        if (m_publisher)
        {
            m_publisher(getRequestContext(), "debug_event", json::object(), std::move(event), buffer_sequence());
        }
    }

    json Interpreter::internalRequest(const json& message)
    {
        return internalRequestImpl(message);
    }

    void Interpreter::registerPublisher(const publisher_type& publisher)
    {
        m_publisher = publisher;
    }

    namespace
    {
        // How long text may wait, and how much may pile up, before it goes
        // out as one stream message. Tunable (read once) through
        // JOVIAN_STREAM_FLUSH_MS and JOVIAN_STREAM_FLUSH_BYTES, for measuring
        // the trade-off; the defaults are what the kernels ship with.
        long streamSetting(const char* name, long fallback, long minimum)
        {
            const char* value = std::getenv(name);
            if (!value || !*value) return fallback;
            char* end = nullptr;
            long parsed = std::strtol(value, &end, 10);
            return end && *end == '\0' && parsed >= minimum ? parsed : fallback;
        }

        const auto kStreamFlushInterval = std::chrono::milliseconds(streamSetting("JOVIAN_STREAM_FLUSH_MS", 50, 0));
        const std::size_t kStreamFlushBytes = static_cast<std::size_t>(streamSetting("JOVIAN_STREAM_FLUSH_BYTES", 16 * 1024, 1));
    }

    // Text is held per stream, as ipykernel does: stdout and stderr written in
    // turn (cat() and message() in a loop, print() and a warning) go out as one
    // message each per interval, not one message per switch -- 25 000 of each
    // were 50 000 messages. Within an interval stdout's text comes first if it
    // was written first; the order of the lines across the two is not kept
    // (a frontend shows the streams apart anyway). Anything else that is
    // published (display data, errors, the reply) flushes both first.
    void Interpreter::publishStream(const std::string& name, const std::string& text)
    {
        if (!m_publisher || text.empty())
        {
            return;
        }

        std::unique_lock<std::mutex> lock(m_streamMutex);
        if (!m_streamFlusher.joinable())
        {
            m_streamFlusher = std::thread(&Interpreter::streamFlusherLoop, this);
        }

        if (m_streamPending.empty() && m_streamLastFlush == std::chrono::steady_clock::time_point{})
        {
            m_streamLastFlush = std::chrono::steady_clock::now() - kStreamFlushInterval;
        }
        // The text belongs to the request that is running NOW; a later flush
        // (possibly from the flusher thread) must not re-read it. Text of
        // another request goes out first.
        const RequestContext& context = getRequestContext();
        if (!m_streamPending.empty() && !(m_streamPending.front().context.header() == context.header()))
        {
            flushStreamsLocked();
        }
        auto pending = std::find_if(m_streamPending.begin(), m_streamPending.end(),
            [&](const PendingStream& p) { return p.name == name; });
        if (pending == m_streamPending.end())
        {
            m_streamPending.push_back(PendingStream{ name, std::string(), context });
            pending = std::prev(m_streamPending.end());
        }
        pending->text += text;
        m_streamPendingBytes += text.size();

        if (m_streamPendingBytes >= kStreamFlushBytes ||
            std::chrono::steady_clock::now() - m_streamLastFlush >= kStreamFlushInterval)
        {
            flushStreamsLocked();
        }
        else
        {
            m_streamCv.notify_one();
        }
    }

    void Interpreter::flushStreams()
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        flushStreamsLocked();
    }

    void Interpreter::flushStreamsLocked()
    {
        std::vector<PendingStream> pending;
        pending.swap(m_streamPending);
        m_streamPendingBytes = 0;
        if (pending.empty() || !m_publisher)
        {
            return;
        }
        m_streamLastFlush = std::chrono::steady_clock::now();
        for (auto& stream : pending)
        {
            json content;
            content["name"] = std::move(stream.name);
            content["text"] = std::move(stream.text);
            m_publisher(
                stream.context,
                "stream",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::streamFlusherLoop()
    {
        std::unique_lock<std::mutex> lock(m_streamMutex);
        while (!m_streamQuit)
        {
            if (m_streamPending.empty())
            {
                m_streamCv.wait(lock, [this] { return m_streamQuit || !m_streamPending.empty(); });
                continue;
            }
            const auto due = m_streamLastFlush + kStreamFlushInterval;
            if (std::chrono::steady_clock::now() >= due)
            {
                flushStreamsLocked();
            }
            else
            {
                m_streamCv.wait_until(lock, due);
            }
        }
    }

    void Interpreter::displayData(json data, json metadata, json transient)
    {
        flushStreams();
        if (m_publisher)
        {
            m_publisher(
                getRequestContext(),
                "display_data",
                json::object(),
                buildDisplayContent(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void Interpreter::updateDisplayData(json data, json metadata, json transient)
    {
        flushStreams();
        if (m_publisher)
        {
            m_publisher(
                getRequestContext(),
                "update_display_data",
                json::object(),
                buildDisplayContent(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publishExecutionInput(const std::string& code, int execution_count)
    {
        if (m_publisher)
        {
            json content;
            content["code"] = code;
            content["execution_count"] = execution_count;
            m_publisher(
                getRequestContext(),
                "execute_input",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publishExecutionResult(int execution_count, json data, json metadata)
    {
        flushStreams();
        if (m_publisher)
        {
            json content;
            content["execution_count"] = execution_count;
            content["data"] = std::move(data);
            content["metadata"] = std::move(metadata);
            m_publisher(
                getRequestContext(),
                "execute_result",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publishExecutionError(const std::string& ename,
        const std::string& evalue,
        const std::vector<std::string>& trace_back)
    {
        flushStreams();
        if (m_publisher)
        {
            json content;
            content["ename"] = ename;
            content["evalue"] = evalue;
            content["traceback"] = trace_back;
            m_publisher(
                getRequestContext(),
                "error",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::clearOutput(bool wait)
    {
        flushStreams();
        if (m_publisher)
        {
            json content;
            content["wait"] = wait;
            m_publisher(
                getRequestContext(),
                "clear_output",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::registerStdinSender(const stdin_sender_type& sender)
    {
        m_stdin = sender;
    }

    void Interpreter::registerInputHandler(const input_reply_handler_type& handler)
    {
        m_inputReplyHandler = handler;
    }

    void Interpreter::registerCommManager(adrastea::CommManager* manager)
    {
        p_commManager = manager;
    }

    const json& Interpreter::parentHeader() const noexcept
    {

        return getRequestContext().header();
    }

    void Interpreter::registerControlMessenger(ControlMessenger& messenger)
    {
        p_messenger = &messenger;
    }

    void Interpreter::registerHistoryManager(const HistoryManager& history)
    {
        p_history = &history;
    }

    const HistoryManager& Interpreter::getHistoryManager() const noexcept
    {
        return *p_history;
    }

    ControlMessenger& Interpreter::getControlMessenger()
    {
        return *p_messenger;
    }

    void Interpreter::inputRequest(const std::string& prompt, bool pwd, const json& ui)
    {
        // The prompt text ("name? ") was written to stdout just before this.
        flushStreams();
        if (m_stdin)
        {
            json content;
            content["prompt"] = prompt;
            content["password"] = pwd;
            if (!ui.is_null())
            {
                content["jovian_ui"] = ui;
            }
            m_stdin(
                getRequestContext(),
                "input_request",
                json::object(),
                std::move(content)
            );
        }
    }

    void Interpreter::inputReply(const std::string& value)
    {
        if (m_inputReplyHandler)
        {
            m_inputReplyHandler(value);
        }
    }

    json Interpreter::internalRequestImpl(const json&)
    {
        json res;
        res["status"] = "error";
        res["what"] = "internal request not supported";
        return res;
    }

    json Interpreter::buildDisplayContent(json data, json metadata, json transient)
    {
        json res;
        res["data"] = std::move(data);
        res["metadata"] = std::move(metadata);
        res["transient"] = std::move(transient);
        return res;
    }

    void Interpreter::setRequestContext(RequestContext context)
    {
        m_requestContext = std::move(context);
    }

    const RequestContext& Interpreter::getRequestContext() const noexcept
    {
        return m_requestContext;
    }
}
