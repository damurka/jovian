#include "carpo/interpreter_py.hpp"
#include "adrastea/helper.hpp"

#include <cstdlib>
#include <stdexcept>

#include "carpo/py/py_dynlib.hpp"

// Real CPython embedding for Carpo, mirroring elara::RInterpreter's overall
// shape (native/src/elara/r/interpreter_r.cpp): Python is loaded dynamically
// at runtime (py/py_dynlib.hpp, mirroring py/../elara/r/r_dynlib.hpp), and
// the actual execute/is-complete LOGIC lives in a small Python-side
// bootstrap module -- the same design principle as hera (packages/hera),
// just written as an inline Python source string instead of an installable
// package, since it's small enough not to need one.
//
// Why a bootstrap module instead of driving everything from raw C API
// calls: getting "run this code, capture stdout/stderr, auto-display the
// last expression's value if it's not None, catch and structure any
// exception" right from C is exactly the kind of logic that's trivial to
// write correctly in Python (ast.parse + contextlib.redirect_stdout +
// traceback.format_exception) and treacherous to reimplement by hand against
// the C API (manual AST-node poking, manual stdout-fd juggling, GIL
// considerations). The bootstrap runs in its OWN private globals dict (not
// the user's __main__ namespace), so none of its internals ever show up in
// the user's dir()/globals().
//
// Deliberately NOT implemented: real-time (mid-execution) stdout/stderr
// streaming. RInterpreter gets that "for free" from R's WriteConsoleEx
// callback hook; doing the same for Python would need a native callback
// module (PyCFunction/PyModuleDef, dynamically-declared struct layouts) that
// this pass intentionally left out of scope -- see py_dynlib.hpp's file
// comment. Output is captured into an in-memory buffer for the whole
// execution and published as ordinary stream messages once it finishes,
// which is indistinguishable from real-time streaming for the common case
// (code that isn't itself long-running) and only visibly differs for a
// long loop that prints incrementally.
namespace carpo
{
    namespace
    {
        PyInterpreter* p_interpreter = nullptr;

        // Defines __carpo_run(code, g) and __carpo_is_complete(code) in a
        // private namespace, exec'd once at construction (see
        // PyInterpreter::PyInterpreter()) into its own dict -- kept alive
        // only long enough to fetch both functions out of it; the dict
        // itself isn't retained.
        //
        // __carpo_run's last-expression auto-display splits the parsed
        // module into "everything but the last statement" (exec'd
        // normally) and, if the last statement is a bare expression, that
        // expression alone (eval'd, then repr()'d and reported back if not
        // None) -- the same thing a real Python REPL/`python -i` does,
        // reimplemented here because Py_file_input (used to run more than
        // one statement per PyRun_String call) never invokes sys.displayhook
        // the way Py_single_input does.
        //
        // Returns an 8-tuple (index-based, matching how interpreter_r.cpp
        // reads hera's own results by VECTOR_ELT index rather than by name):
        //   0 status        "ok" | "error"
        //   1 stdout_text
        //   2 stderr_text
        //   3 has_result    0 | 1
        //   4 result_repr   repr() of the last expression's value, if any
        //   5 ename
        //   6 evalue
        //   7 traceback     list[str]
        const char* kBootstrapSource = R"PY(
import ast
import codeop
import contextlib
import io
import sys
import traceback


def __carpo_run(code, g):
    stdout_buf = io.StringIO()
    stderr_buf = io.StringIO()
    status = "ok"
    ename = ""
    evalue = ""
    tb_lines = []
    has_result = 0
    result_repr = ""
    try:
        tree = ast.parse(code, mode="exec")
        trailing_expr = None
        if tree.body and isinstance(tree.body[-1], ast.Expr):
            trailing_expr = tree.body.pop()
        with contextlib.redirect_stdout(stdout_buf), contextlib.redirect_stderr(stderr_buf):
            if tree.body:
                exec(compile(tree, "<carpo>", "exec"), g)
            if trailing_expr is not None:
                value_expr = ast.Expression(trailing_expr.value)
                ast.copy_location(value_expr, trailing_expr.value)
                ast.fix_missing_locations(value_expr)
                result = eval(compile(value_expr, "<carpo>", "eval"), g)
                if result is not None:
                    has_result = 1
                    result_repr = repr(result)
    except BaseException as e:
        status = "error"
        ename = type(e).__name__
        evalue = str(e)
        tb_lines = traceback.format_exception(type(e), e, e.__traceback__)
    return (status, stdout_buf.getvalue(), stderr_buf.getvalue(), has_result, result_repr, ename, evalue, tb_lines)


def __carpo_is_complete(code):
    try:
        result = codeop.compile_command(code)
    except (SyntaxError, OverflowError, ValueError):
        return "invalid"
    return "incomplete" if result is None else "complete"


__carpo_version = "%d.%d.%d" % (sys.version_info.major, sys.version_info.minor, sys.version_info.micro)
)PY";

        std::string pyUnicodeToStdString(PyObject* strObj)
        {
            if (!strObj) return std::string();
            py::Ref bytesObj(PyUnicode_AsUTF8String(strObj));
            if (!bytesObj)
            {
                PyErr_Clear();
                return std::string();
            }
            char* raw = PyBytes_AsString(bytesObj.get());
            return raw ? std::string(raw) : std::string();
        }

        // Only meant for failures in OUR OWN bootstrap plumbing (a bug in
        // kBootstrapSource, or Python itself misbehaving) -- every exception
        // a user's own code raises is already caught and structured by
        // __carpo_run above, never surfacing as a raw PyErr here.
        std::string describePythonError(const std::string& context)
        {
            if (!PyErr_Occurred())
            {
                return context;
            }
            PyObject* type = nullptr;
            PyObject* value = nullptr;
            PyObject* traceback = nullptr;
            PyErr_Fetch(&type, &value, &traceback);
            PyErr_NormalizeException(&type, &value, &traceback);
            py::Ref typeRef(type);
            py::Ref valueRef(value);
            py::Ref tracebackRef(traceback);

            std::string ename = "PythonError";
            if (typeRef)
            {
                py::Ref nameObj(PyObject_GetAttrString(typeRef.get(), "__name__"));
                if (nameObj)
                {
                    std::string name = pyUnicodeToStdString(nameObj.get());
                    if (!name.empty()) ename = name;
                }
            }
            std::string evalue;
            if (valueRef)
            {
                py::Ref strObj(PyObject_Str(valueRef.get()));
                evalue = pyUnicodeToStdString(strObj.get());
            }
            PyErr_Clear();
            return context + ": " + ename + ": " + evalue;
        }
    }

    PyInterpreter* getPyInterpreter()
    {
        return p_interpreter;
    }

    PyInterpreter::PyInterpreter(int /*argc*/, char* /*argv*/[])
        : m_userGlobals(nullptr)
        , m_bootstrapRunFn(nullptr)
        , m_bootstrapIsCompleteFn(nullptr)
        , m_ownsInterpreter(false)
        , m_finalized(false)
    {
        // carpo::Server::setupEnvironment() (bridge/engine.cpp) has already
        // set PYTHONHOME from EnvironmentConfig by the time this runs, the
        // same ordering elara::Server::start() uses for R_HOME -- see that
        // function's comment.
        const char* pythonHome = std::getenv("PYTHONHOME");
        py::loadPyApi(pythonHome ? pythonHome : "");

        // Safe to construct more than one PyInterpreter in the same process
        // (e.g. sequential gtest TEST cases): only the first one actually
        // owns the interpreter's lifecycle. Py_FinalizeEx() followed by a
        // fresh Py_Initialize() in the same process is explicitly a
        // supported (if imperfect -- see Py_FinalizeEx's own documented
        // caveats) CPython use case, which is all repeated *sequential*
        // construct-then-destruct cycles rely on here; concurrent instances
        // are not a scenario this codebase creates.
        m_ownsInterpreter = !Py_IsInitialized();
        if (m_ownsInterpreter)
        {
            Py_Initialize();
        }

        PyObject* mainModule = PyImport_AddModule("__main__");
        if (!mainModule)
        {
            throw std::runtime_error(describePythonError("Could not create Python's __main__ module"));
        }
        m_userGlobals = PyModule_GetDict(mainModule);

        py::Ref bootstrapGlobals(PyDict_New());
        py::Ref bootstrapResult(PyRun_String(
            kBootstrapSource, CARPO_PY_FILE_INPUT, bootstrapGlobals.get(), bootstrapGlobals.get()));
        if (!bootstrapResult)
        {
            throw std::runtime_error(
                describePythonError("Failed to initialize Carpo's internal Python bootstrap runtime"));
        }

        PyObject* runFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_run");
        PyObject* isCompleteFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_is_complete");
        PyObject* versionObj = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_version");
        if (!runFn || !isCompleteFn || !versionObj)
        {
            throw std::runtime_error(
                "Carpo's internal Python bootstrap runtime did not define the expected functions -- "
                "this is a bug in carpo itself, not a user-facing configuration problem.");
        }

        Py_IncRef(runFn);
        Py_IncRef(isCompleteFn);
        m_bootstrapRunFn = runFn;
        m_bootstrapIsCompleteFn = isCompleteFn;
        m_languageVersion = pyUnicodeToStdString(versionObj);

        adrastea::registerInterpreter(this);
        p_interpreter = this;
    }

    PyInterpreter::~PyInterpreter()
    {
        finalizeIfOwned();
    }

    void PyInterpreter::finalizeIfOwned()
    {
        if (m_finalized) return;

        // Release our own references before finalizing -- Py_FinalizeEx()
        // reclaims everything regardless, but doing this unconditionally
        // keeps this function's shape the same whether or not
        // m_ownsInterpreter ends up true below, and costs nothing.
        if (m_bootstrapRunFn)
        {
            Py_DecRef(static_cast<PyObject*>(m_bootstrapRunFn));
            m_bootstrapRunFn = nullptr;
        }
        if (m_bootstrapIsCompleteFn)
        {
            Py_DecRef(static_cast<PyObject*>(m_bootstrapIsCompleteFn));
            m_bootstrapIsCompleteFn = nullptr;
        }

        if (m_ownsInterpreter)
        {
            Py_FinalizeEx();
        }
        m_finalized = true;
    }

    void PyInterpreter::configureImpl()
    {
        printf("[carpo] PyInterpreter::configureImpl() -- Python %s ready\n", m_languageVersion.c_str());
        fflush(stdout);
    }

    void PyInterpreter::executeRequestImpl(
        send_reply_callback cb,
        int execution_count,
        const std::string& code,
        adrastea::ExecuteRequestConfig config,
        adrastea::json /*user_expressions*/)
    {
        if (config.store_history)
        {
            const_cast<adrastea::HistoryManager&>(getHistoryManager()).storeInputs(0, execution_count, code);
        }

        py::Ref args(PyTuple_New(2));
        PyTuple_SetItem(args.get(), 0, PyUnicode_FromString(code.c_str())); // steals the new ref
        Py_IncRef(static_cast<PyObject*>(m_userGlobals)); // PyTuple_SetItem steals; m_userGlobals is only borrowed
        PyTuple_SetItem(args.get(), 1, static_cast<PyObject*>(m_userGlobals));

        py::Ref result(PyObject_CallObject(static_cast<PyObject*>(m_bootstrapRunFn), args.get()));
        if (!result)
        {
            // The bootstrap itself failed to run at all (a bug in
            // kBootstrapSource, not a user code error -- __carpo_run catches
            // every exception the user's own code can raise internally).
            std::string message = describePythonError("Carpo's internal Python bootstrap runtime failed");
            publishExecutionError("CarpoInternalError", message, {});
            cb(adrastea::createErrorReply("CarpoInternalError", message, {}));
            return;
        }

        std::string status = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 0));
        std::string stdoutText = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 1));
        std::string stderrText = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 2));
        long hasResult = PyLong_AsLong(PyTuple_GetItem(result.get(), 3));
        std::string resultRepr = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 4));
        std::string ename = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 5));
        std::string evalue = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 6));

        PyObject* tbList = PyTuple_GetItem(result.get(), 7);
        std::vector<std::string> traceBack;
        if (tbList)
        {
            Py_ssize_t n = PyList_Size(tbList);
            for (Py_ssize_t i = 0; i < n; ++i)
            {
                traceBack.push_back(pyUnicodeToStdString(PyList_GetItem(tbList, i)));
            }
        }

        if (!stdoutText.empty())
        {
            publishStream("stdout", stdoutText);
        }
        if (!stderrText.empty())
        {
            publishStream("stderr", stderrText);
        }

        if (status == "error")
        {
            publishExecutionError(ename, evalue, traceBack);
            cb(adrastea::createErrorReply(ename, evalue, std::move(traceBack)));
            return;
        }

        if (hasResult)
        {
            adrastea::json data = { { "text/plain", resultRepr } };
            publishExecutionResult(execution_count, data, adrastea::json::object());
        }
        cb(adrastea::createSuccessfulReply());
    }

    adrastea::json PyInterpreter::completeRequestImpl(const std::string& /*code*/, int cursor_pos)
    {
        // Empty match list rather than an error: complete_request failing
        // loudly would be a worse editor experience than "no suggestions" --
        // matches how a real completion engine reports "nothing found".
        // Real completion support is a later pass (see this class's file
        // comment) -- Carpo can execute code today but doesn't yet
        // introspect it.
        return adrastea::createCompleteReply(adrastea::json::array(), cursor_pos, cursor_pos);
    }

    adrastea::json PyInterpreter::inspectRequestImpl(const std::string& /*code*/, int /*cursor_pos*/, int /*detail_level*/)
    {
        return adrastea::createInspectReply(false);
    }

    adrastea::json PyInterpreter::isCompleteRequestImpl(const std::string& code)
    {
        py::Ref args(PyTuple_New(1));
        PyTuple_SetItem(args.get(), 0, PyUnicode_FromString(code.c_str()));

        py::Ref result(PyObject_CallObject(static_cast<PyObject*>(m_bootstrapIsCompleteFn), args.get()));
        if (!result)
        {
            PyErr_Clear();
            return adrastea::createIsCompleteReply("unknown");
        }
        return adrastea::createIsCompleteReply(pyUnicodeToStdString(result.get()));
    }

    adrastea::json PyInterpreter::shutdownRequestImpl(bool restart)
    {
        finalizeIfOwned();
        return adrastea::createShutdownReply(restart);
    }

    adrastea::json PyInterpreter::interruptRequestImpl()
    {
        return adrastea::createInterruptReply();
    }

    adrastea::json PyInterpreter::kernelInfoRequestImpl()
    {
        const std::string implementation = "carpo";
        const std::string implementation_version{ adrastea::version::kernel_protocol_version };
        const std::string language_name = "python";
        const std::string language_version = m_languageVersion;
        const std::string language_mimetype = "text/x-python";
        const std::string language_file_extension = ".py";
        const std::string language_pygments_lexer = "python3";
        const std::string language_codemirror_mode = "python";
        const std::string language_nbconvert_exporter = "";
        const std::string banner = "carpo (Python " + m_languageVersion + ")";
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
