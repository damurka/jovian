#include "carpo/interpreter_py.hpp"
#include "adrastea/helper.hpp"

namespace carpo
{
    namespace
    {
        PyInterpreter* p_interpreter = nullptr;

        const char* kNotImplementedMessage =
            "Carpo (the Python kernel) is scaffolding only -- no Python code execution is "
            "implemented yet. See docs/cpp-usage.md's \"Writing a new interpreter\" section "
            "for what a real implementation needs.";
    }

    PyInterpreter* getPyInterpreter()
    {
        return p_interpreter;
    }

    PyInterpreter::PyInterpreter(int /*argc*/, char* /*argv*/[])
    {
        // A real implementation embeds Python here the way elara::RInterpreter
        // embeds R (native/src/elara/r/interpreter_r.cpp's constructor calling
        // Rf_initEmbeddedR) -- most likely by dynamically loading libpython at
        // runtime (mirroring native/src/elara/r/r_dynlib.hpp) rather than
        // linking against a specific Python version at build time, for the
        // same "switch versions without a rebuild, fail cleanly if missing"
        // reasons documented there.
        adrastea::registerInterpreter(this);
        p_interpreter = this;
    }

    void PyInterpreter::configureImpl()
    {
        printf("[carpo] PyInterpreter::configureImpl() -- scaffolding only, nothing to configure yet\n");
        fflush(stdout);
    }

    void PyInterpreter::executeRequestImpl(
        send_reply_callback cb,
        int /*execution_count*/,
        const std::string& /*code*/,
        adrastea::ExecuteRequestConfig /*config*/,
        adrastea::json /*user_expressions*/)
    {
        publishExecutionError("NotImplementedError", kNotImplementedMessage, {});
        cb(adrastea::createErrorReply("NotImplementedError", kNotImplementedMessage, {}));
    }

    adrastea::json PyInterpreter::completeRequestImpl(const std::string& /*code*/, int cursor_pos)
    {
        // Empty match list rather than an error: complete_request failing
        // loudly would be a worse editor experience than "no suggestions" --
        // matches how a real completion engine reports "nothing found".
        return adrastea::createCompleteReply(adrastea::json::array(), cursor_pos, cursor_pos);
    }

    adrastea::json PyInterpreter::inspectRequestImpl(const std::string& /*code*/, int /*cursor_pos*/, int /*detail_level*/)
    {
        return adrastea::createInspectReply(false);
    }

    adrastea::json PyInterpreter::isCompleteRequestImpl(const std::string& /*code*/)
    {
        // "unknown" (not "complete"/"incomplete"/"invalid") is the honest
        // answer -- Carpo doesn't parse Python at all yet, so it genuinely
        // can't tell.
        return adrastea::createIsCompleteReply("unknown");
    }

    adrastea::json PyInterpreter::shutdownRequestImpl(bool restart)
    {
        // No real interpreter state exists yet to tear down, so this can
        // genuinely succeed rather than being another not-implemented stub.
        return adrastea::createShutdownReply(restart);
    }

    adrastea::json PyInterpreter::interruptRequestImpl()
    {
        return adrastea::createInterruptReply();
    }

    adrastea::json PyInterpreter::kernelInfoRequestImpl()
    {
        // The one fully-implemented *RequestImpl() -- lets kernel discovery/
        // jupyter_client tooling see this kernel identify itself correctly
        // even though it can't execute anything yet.
        const std::string implementation = "carpo";
        const std::string implementation_version{ adrastea::version::kernel_protocol_version };
        const std::string language_name = "python";
        const std::string language_version = "unknown (not yet embedded)";
        const std::string language_mimetype = "text/x-python";
        const std::string language_file_extension = ".py";
        const std::string language_pygments_lexer = "python3";
        const std::string language_codemirror_mode = "python";
        const std::string language_nbconvert_exporter = "";
        const std::string banner = "carpo (scaffolding -- Python execution not yet implemented)";
        const adrastea::json help_links = adrastea::json::array();

        return adrastea::createInfoReply(
            implementation,
            implementation_version,
            language_name,
            language_version,
            language_mimetype,
            language_file_extension,
            language_pygments_lexer,
            language_codemirror_mode,
            language_nbconvert_exporter,
            banner,
            help_links
        );
    }
}
