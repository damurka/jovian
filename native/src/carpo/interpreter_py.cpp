#include "carpo/interpreter_py.hpp"
#include "adrastea/helper.hpp"

#include <cstdlib>
#include <stdexcept>

#include "carpo/py/py_dynlib.hpp"

// Real CPython embedding for Carpo, mirroring elara::RInterpreter's overall
// shape (native/src/elara/r/interpreter_r.cpp): Python is loaded dynamically
// at runtime (py/py_dynlib.hpp, mirroring py/../elara/r/r_dynlib.hpp), and
// the actual execute/is-complete/complete/inspect LOGIC lives in a small
// Python-side bootstrap module -- the same design principle as hera
// (packages/hera), just written as an inline Python source string instead
// of an installable package, since it's small enough not to need one.
//
// Why a bootstrap module instead of driving everything from raw C API
// calls: getting "run this code, auto-display the last expression's value
// if it's not None, catch and structure any exception", "is this code
// complete", "complete this identifier", and "describe this object" right
// from C is exactly the kind of logic that's trivial to write correctly in
// Python (ast.parse + traceback.format_exception + rlcompleter + inspect,
// all standard library, no third-party dependency) and treacherous to
// reimplement by hand against the C API.  The bootstrap runs in its OWN
// private globals dict (not the user's __main__ namespace), so none of its
// internals ever show up in the user's dir()/globals().
//
// Real-time stdout/stderr streaming: unlike RInterpreter (which gets this
// "for free" from R's WriteConsoleEx callback hook), Python needs an
// explicit native callback exposed to Python code -- carpoNativeWrite()
// below, wrapped into a callable via PyCFunction_NewEx/PyMethodDef (see
// py_dynlib.hpp's file comment for why that one struct is declared there
// despite this file's otherwise-opaque-PyObject discipline) and inserted
// directly into the bootstrap's globals dict before it runs. The bootstrap
// source's _CarpoStream class calls it from write(), so every write to
// stdout/stderr during execution is published immediately, the same
// granularity R's WriteConsoleEx gives -- not batched until execution ends.
namespace carpo
{
    namespace
    {
        PyInterpreter* p_interpreter = nullptr;

        // Defines __carpo_run(code, g), __carpo_is_complete(code),
        // __carpo_complete(code, cursor_pos, g), and
        // __carpo_inspect(code, cursor_pos, g) in a private namespace, exec'd
        // once at construction (see PyInterpreter::PyInterpreter()) into its
        // own dict -- kept alive only transitively, through these functions'
        // own __globals__ references, once extracted.
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
        // __carpo_run returns a 6-tuple (index-based, matching how
        // interpreter_r.cpp reads hera's own results by VECTOR_ELT index
        // rather than by name):
        //   0 status        "ok" | "error"
        //   1 has_result    0 | 1
        //   2 result_repr   repr() of the last expression's value, if any
        //   3 ename
        //   4 evalue
        //   5 traceback     list[str]
        // (stdout/stderr are no longer part of this tuple -- they're
        // published in real time via the native write callbacks instead of
        // captured into a buffer and returned at the end.)
        //
        // __carpo_complete returns (matches: list[str], cursor_start, cursor_end).
        // __carpo_inspect returns (found: 0|1, text: str).
        const char* kBootstrapSource = R"PY(
import ast
import codeop
import contextlib
import inspect as _carpo_inspect_mod
import os
import re
import rlcompleter
import sys
import traceback


class _CarpoStream:
    def __init__(self, native_write):
        self._native_write = native_write

    def write(self, s):
        if s:
            self._native_write(s)
        return len(s)

    def flush(self):
        pass

    def isatty(self):
        return False


# venv "activation" for an embedded interpreter: PYTHONHOME still points at
# the base install (Server::setupEnvironment(), bridge/engine.cpp -- a venv
# has no libpython/stdlib of its own to point PYTHONHOME at), so the only
# thing left to do is make the venv's own installed packages importable by
# prepending its site-packages directory to sys.path. Tries both layouts
# since this doesn't otherwise know which platform created the venv.
_carpo_venv = os.environ.get("CARPO_VENV_PATH", "")
if _carpo_venv:
    _carpo_ver = "%d.%d" % (sys.version_info.major, sys.version_info.minor)
    for _carpo_site in (
        os.path.join(_carpo_venv, "Lib", "site-packages"),
        os.path.join(_carpo_venv, "lib", "python" + _carpo_ver, "site-packages"),
    ):
        if os.path.isdir(_carpo_site) and _carpo_site not in sys.path:
            sys.path.insert(0, _carpo_site)


def __carpo_run(code, g):
    status = "ok"
    ename = ""
    evalue = ""
    tb_lines = []
    has_result = 0
    result_repr = ""
    stdout_stream = _CarpoStream(__carpo_native_write_stdout)
    stderr_stream = _CarpoStream(__carpo_native_write_stderr)
    try:
        tree = ast.parse(code, mode="exec")
        trailing_expr = None
        if tree.body and isinstance(tree.body[-1], ast.Expr):
            trailing_expr = tree.body.pop()
        with contextlib.redirect_stdout(stdout_stream), contextlib.redirect_stderr(stderr_stream):
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
    return (status, has_result, result_repr, ename, evalue, tb_lines)


def __carpo_is_complete(code):
    try:
        result = codeop.compile_command(code)
    except (SyntaxError, OverflowError, ValueError):
        return "invalid"
    return "incomplete" if result is None else "complete"


_carpo_token_re = re.compile(r"[A-Za-z_][A-Za-z0-9_.]*$")


def __carpo_complete(code, cursor_pos, g):
    prefix = code[:cursor_pos]
    m = _carpo_token_re.search(prefix)
    token = m.group(0) if m else ""
    cursor_start = cursor_pos - len(token)

    completer = rlcompleter.Completer(g)
    matches = []
    seen = set()
    state = 0
    while state < 500:
        try:
            candidate = completer.complete(token, state)
        except Exception:
            break
        if candidate is None:
            break
        if candidate not in seen:
            seen.add(candidate)
            matches.append(candidate)
        state += 1
    return (matches, cursor_start, cursor_pos)


def __carpo_inspect(code, cursor_pos, g):
    prefix = code[:cursor_pos]
    m = _carpo_token_re.search(prefix)
    token = m.group(0) if m else ""
    if not token:
        return (0, "")
    try:
        obj = eval(token, g)
    except Exception:
        return (0, "")

    parts = []
    try:
        parts.append(token + str(_carpo_inspect_mod.signature(obj)))
    except (ValueError, TypeError):
        pass
    parts.append("Type: " + type(obj).__name__)
    doc = _carpo_inspect_mod.getdoc(obj)
    if doc:
        parts.append("")
        parts.append(doc)
    else:
        try:
            parts.append("")
            parts.append(repr(obj))
        except Exception:
            pass
    return (1, "\n".join(parts))


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

        // Native callbacks exposed to the bootstrap source as
        // __carpo_native_write_stdout/__carpo_native_write_stderr (see this
        // file's header comment) -- called synchronously from Python's own
        // write() dispatch, on the same single thread this whole interpreter
        // ever runs on (matching elara::WriteConsoleEx's threading model
        // exactly), so no GIL considerations beyond what's already implicit
        // in that single-threaded embedding.
        PyObject* carpoNativeWrite(const char* streamName, PyObject* args)
        {
            std::string text = pyUnicodeToStdString(PyTuple_GetItem(args, 0));
            if (p_interpreter && !text.empty())
            {
                p_interpreter->publishStream(streamName, text);
            }
            Py_IncRef(Py_None);
            return Py_None;
        }

        PyObject* carpoNativeWriteStdout(PyObject* /*self*/, PyObject* args)
        {
            return carpoNativeWrite("stdout", args);
        }

        PyObject* carpoNativeWriteStderr(PyObject* /*self*/, PyObject* args)
        {
            return carpoNativeWrite("stderr", args);
        }

        PyMethodDef kWriteStdoutDef = { "__carpo_native_write_stdout", carpoNativeWriteStdout, CARPO_PY_METH_VARARGS, nullptr };
        PyMethodDef kWriteStderrDef = { "__carpo_native_write_stderr", carpoNativeWriteStderr, CARPO_PY_METH_VARARGS, nullptr };
    }

    PyInterpreter* getPyInterpreter()
    {
        return p_interpreter;
    }

    PyInterpreter::PyInterpreter(int /*argc*/, char* /*argv*/[])
        : m_userGlobals(nullptr)
        , m_bootstrapRunFn(nullptr)
        , m_bootstrapIsCompleteFn(nullptr)
        , m_bootstrapCompleteFn(nullptr)
        , m_bootstrapInspectFn(nullptr)
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

        // Wire the native stdout/stderr callbacks into the bootstrap's
        // globals BEFORE running kBootstrapSource, so __carpo_run (defined
        // by that source) can see __carpo_native_write_stdout/_stderr as
        // ordinary already-bound globals when it references them.
        py::Ref stdoutFn(PyCFunction_NewEx(&kWriteStdoutDef, nullptr, nullptr));
        py::Ref stderrFn(PyCFunction_NewEx(&kWriteStderrDef, nullptr, nullptr));
        if (!stdoutFn || !stderrFn)
        {
            throw std::runtime_error(describePythonError("Could not create Carpo's native stdout/stderr callbacks"));
        }
        PyDict_SetItemString(bootstrapGlobals.get(), "__carpo_native_write_stdout", stdoutFn.get());
        PyDict_SetItemString(bootstrapGlobals.get(), "__carpo_native_write_stderr", stderrFn.get());

        py::Ref bootstrapResult(PyRun_String(
            kBootstrapSource, CARPO_PY_FILE_INPUT, bootstrapGlobals.get(), bootstrapGlobals.get()));
        if (!bootstrapResult)
        {
            throw std::runtime_error(
                describePythonError("Failed to initialize Carpo's internal Python bootstrap runtime"));
        }

        PyObject* runFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_run");
        PyObject* isCompleteFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_is_complete");
        PyObject* completeFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_complete");
        PyObject* inspectFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_inspect");
        PyObject* versionObj = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_version");
        if (!runFn || !isCompleteFn || !completeFn || !inspectFn || !versionObj)
        {
            throw std::runtime_error(
                "Carpo's internal Python bootstrap runtime did not define the expected functions -- "
                "this is a bug in carpo itself, not a user-facing configuration problem.");
        }

        Py_IncRef(runFn);
        Py_IncRef(isCompleteFn);
        Py_IncRef(completeFn);
        Py_IncRef(inspectFn);
        m_bootstrapRunFn = runFn;
        m_bootstrapIsCompleteFn = isCompleteFn;
        m_bootstrapCompleteFn = completeFn;
        m_bootstrapInspectFn = inspectFn;
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
        if (m_bootstrapCompleteFn)
        {
            Py_DecRef(static_cast<PyObject*>(m_bootstrapCompleteFn));
            m_bootstrapCompleteFn = nullptr;
        }
        if (m_bootstrapInspectFn)
        {
            Py_DecRef(static_cast<PyObject*>(m_bootstrapInspectFn));
            m_bootstrapInspectFn = nullptr;
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
        long hasResult = PyLong_AsLong(PyTuple_GetItem(result.get(), 1));
        std::string resultRepr = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 2));
        std::string ename = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 3));
        std::string evalue = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 4));

        PyObject* tbList = PyTuple_GetItem(result.get(), 5);
        std::vector<std::string> traceBack;
        if (tbList)
        {
            Py_ssize_t n = PyList_Size(tbList);
            for (Py_ssize_t i = 0; i < n; ++i)
            {
                traceBack.push_back(pyUnicodeToStdString(PyList_GetItem(tbList, i)));
            }
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

    adrastea::json PyInterpreter::completeRequestImpl(const std::string& code, int cursor_pos)
    {
        py::Ref args(PyTuple_New(3));
        PyTuple_SetItem(args.get(), 0, PyUnicode_FromString(code.c_str()));
        PyTuple_SetItem(args.get(), 1, PyLong_FromLong(cursor_pos));
        Py_IncRef(static_cast<PyObject*>(m_userGlobals));
        PyTuple_SetItem(args.get(), 2, static_cast<PyObject*>(m_userGlobals));

        py::Ref result(PyObject_CallObject(static_cast<PyObject*>(m_bootstrapCompleteFn), args.get()));
        if (!result)
        {
            PyErr_Clear();
            return adrastea::createCompleteReply(adrastea::json::array(), cursor_pos, cursor_pos);
        }

        PyObject* matchesList = PyTuple_GetItem(result.get(), 0);
        std::vector<std::string> matches;
        Py_ssize_t n = matchesList ? PyList_Size(matchesList) : 0;
        for (Py_ssize_t i = 0; i < n; ++i)
        {
            matches.push_back(pyUnicodeToStdString(PyList_GetItem(matchesList, i)));
        }
        int cursorStart = static_cast<int>(PyLong_AsLong(PyTuple_GetItem(result.get(), 1)));
        int cursorEnd = static_cast<int>(PyLong_AsLong(PyTuple_GetItem(result.get(), 2)));

        return adrastea::createCompleteReply(adrastea::json(matches), cursorStart, cursorEnd);
    }

    adrastea::json PyInterpreter::inspectRequestImpl(const std::string& code, int cursor_pos, int /*detail_level*/)
    {
        py::Ref args(PyTuple_New(3));
        PyTuple_SetItem(args.get(), 0, PyUnicode_FromString(code.c_str()));
        PyTuple_SetItem(args.get(), 1, PyLong_FromLong(cursor_pos));
        Py_IncRef(static_cast<PyObject*>(m_userGlobals));
        PyTuple_SetItem(args.get(), 2, static_cast<PyObject*>(m_userGlobals));

        py::Ref result(PyObject_CallObject(static_cast<PyObject*>(m_bootstrapInspectFn), args.get()));
        if (!result)
        {
            PyErr_Clear();
            return adrastea::createInspectReply(false);
        }

        long found = PyLong_AsLong(PyTuple_GetItem(result.get(), 0));
        if (!found)
        {
            return adrastea::createInspectReply(false);
        }
        std::string text = pyUnicodeToStdString(PyTuple_GetItem(result.get(), 1));
        adrastea::json data = { { "text/plain", text } };
        return adrastea::createInspectReply(true, data);
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
