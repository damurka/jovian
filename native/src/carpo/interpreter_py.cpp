#include "carpo/interpreter_py.hpp"
#include "adrastea/helper.hpp"
#include "adrastea/input.hpp"

#include <cstdlib>
#include <stdexcept>

#include <signal.h>
#ifndef _WIN32
#include <pthread.h>
#endif

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
import inspect as _carpo_inspect_mod
import os
import re
import rlcompleter
import signal
import sys
import traceback


class _CarpoStream:
    encoding = "utf-8"
    errors = "replace"

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

    def writable(self):
        return True


# The streams stay replaced for the life of the kernel, not only while a cell
# runs: a thread (or an asyncio task) still printing after its cell has
# finished reaches the client too, as part of the latest request.
sys.stdout = _CarpoStream(__carpo_native_write_stdout)
sys.stderr = _CarpoStream(__carpo_native_write_stderr)


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


# Embedded, sys.executable is the kernel (carpo.exe), and anything that starts
# "another Python" with it -- multiprocessing's spawn/forkserver workers,
# concurrent.futures.ProcessPoolExecutor, subprocess.run([sys.executable,
# "-m", "pip", ...]) -- would start a second kernel instead. Point it at the
# real interpreter: the venv's when there is one (its packages), else the
# base installation's.
def _carpo_find_python():
    ver = "%d.%d" % (sys.version_info.major, sys.version_info.minor)
    def first(*paths):
        for p in paths:
            if os.path.isfile(p):
                return p
        return None
    homes = [h for h in (os.environ.get("PYTHONHOME", ""), sys.base_prefix, sys.base_exec_prefix) if h]
    base = None
    for home in homes:
        base = first(os.path.join(home, "python.exe"),
                     os.path.join(home, "bin", "python" + ver),
                     os.path.join(home, "bin", "python3"),
                     os.path.join(home, "bin", "python"))
        if base:
            break
    venv = None
    if _carpo_venv:
        venv = first(os.path.join(_carpo_venv, "Scripts", "python.exe"),
                     os.path.join(_carpo_venv, "bin", "python" + ver),
                     os.path.join(_carpo_venv, "bin", "python3"),
                     os.path.join(_carpo_venv, "bin", "python"))
    return venv or base, base


_carpo_python, _carpo_base_python = _carpo_find_python()
if _carpo_python:
    sys.executable = _carpo_python
    if _carpo_base_python:
        sys._base_executable = _carpo_base_python
    if "multiprocessing" in sys.modules:
        sys.modules["multiprocessing"].set_executable(_carpo_python)


# Routes every input() call -- from user code AND from anything the standard
# library itself calls input() from -- through __carpo_native_input (wired
# into this module's globals alongside __carpo_native_write_stdout/_stderr;
# see this file's header comment and PyInterpreter's constructor). This
# reassigns the process-wide builtins module's attribute, so it takes effect
# for user code executed against m_userGlobals too, not just this bootstrap
# module -- both share the same single `builtins` module instance, the same
# way every Python module does.
import builtins as _carpo_builtins


def __carpo_input(prompt=""):
    return __carpo_native_input(str(prompt))


_carpo_builtins.input = __carpo_input


# Interrupts (PyErr_SetInterrupt, and a real SIGINT on POSIX) are ignored by
# Python unless SIGINT has its Python-level handler -- which Py_Initialize()
# only installs if the process did not start with SIGINT ignored (a child
# of a supervisor without a console may). Make it unconditional. This runs
# on the interpreter thread, the only thread signal.signal() allows.
try:
    signal.signal(signal.SIGINT, signal.default_int_handler)
except (ValueError, OSError):
    pass


# One event loop for the whole session, created on first use. A cell with a
# top-level `await` runs on it (as in IPython), and between cells
# __carpo_idle() lets it run the tasks those cells left behind, so a task
# started with asyncio.create_task() keeps going after its cell. A cell that
# calls asyncio.run() itself still gets its own, separate loop.
_carpo_loop = None


def _carpo_event_loop():
    global _carpo_loop
    import asyncio
    if _carpo_loop is None or _carpo_loop.is_closed():
        _carpo_loop = asyncio.new_event_loop()
    asyncio.set_event_loop(_carpo_loop)
    return _carpo_loop


def _carpo_evaluate(compiled, g):
    # Compiled with PyCF_ALLOW_TOP_LEVEL_AWAIT: code that awaits comes back
    # as a coroutine, run to completion on the session's loop.
    if compiled.co_flags & _carpo_inspect_mod.CO_COROUTINE:
        return _carpo_event_loop().run_until_complete(eval(compiled, g))
    return eval(compiled, g)


def __carpo_idle():
    loop = _carpo_loop
    if loop is None or loop.is_closed() or loop.is_running():
        return
    if not (getattr(loop, "_ready", None) or getattr(loop, "_scheduled", None)):
        return
    # One pass: run what is ready and the timers that are due, without
    # waiting for the next one.
    loop.call_soon(loop.stop)
    try:
        loop.run_forever()
    except BaseException:
        traceback.print_exc()


def __carpo_run(code, g):
    status = "ok"
    ename = ""
    evalue = ""
    tb_lines = []
    has_result = 0
    result_repr = ""
    flags = ast.PyCF_ALLOW_TOP_LEVEL_AWAIT
    try:
        # The session's loop is the current one in every cell -- also after
        # a cell's asyncio.run() unset it -- so asyncio.get_event_loop()
        # .create_task(...) schedules onto the loop __carpo_idle() runs.
        _carpo_event_loop()
        tree = ast.parse(code, mode="exec")
        trailing_expr = None
        if tree.body and isinstance(tree.body[-1], ast.Expr):
            trailing_expr = tree.body.pop()
        if tree.body:
            _carpo_evaluate(compile(tree, "<carpo>", "exec", flags=flags), g)
        if trailing_expr is not None:
            value_expr = ast.Expression(trailing_expr.value)
            ast.copy_location(value_expr, trailing_expr.value)
            ast.fix_missing_locations(value_expr)
            result = _carpo_evaluate(compile(value_expr, "<carpo>", "eval", flags=flags), g)
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


def __carpo_eval_expr(expr, g):
    # Backs execute_request's user_expressions: (status, text_or_ename, evalue).
    try:
        value = eval(expr, g)
        return ("ok", repr(value), "")
    except BaseException as e:
        return ("error", type(e).__name__, str(e))


# ---- the session's variables, for a variables pane and a data viewer (Jovian's Session.listVariables(), readTable()):
# user expressions Carpo answers itself, with JSON -- see __carpo_answer

import json as _carpo_json
import reprlib as _carpo_reprlib
import types as _carpo_types

_CARPO_MAX_OBJECTS = 5000
_carpo_short = _carpo_reprlib.Repr()
_carpo_short.maxstring = 120
_carpo_short.maxother = 120


def _carpo_table_kind(v):
    # "pandas", "polars" or "numpy" for what the data viewer shows (a DataFrame, a Series, an array of 1 or 2
    # dimensions), else None. By type name: none of these modules is imported for it
    t = type(v)
    module = t.__module__.split(".")[0]
    if module == "pandas" and t.__name__ in ("DataFrame", "Series"):
        return "pandas"
    if module == "polars" and t.__name__ in ("DataFrame", "Series"):
        return "polars"
    if module == "numpy" and t.__name__ == "ndarray" and getattr(v, "ndim", 0) in (1, 2):
        return "numpy"
    return None


def _carpo_size(v):
    shape = getattr(v, "shape", None)
    if isinstance(shape, tuple) and all(isinstance(d, int) for d in shape):
        return " \u00d7 ".join(f"{d:,}" for d in shape)
    if callable(v) or isinstance(v, type):
        return ""
    try:
        return f"{len(v):,}"
    except Exception:
        return ""


def _carpo_preview(v):
    try:
        if _carpo_table_kind(v) in ("pandas", "polars") and type(v).__name__ == "DataFrame":
            rows, columns = v.shape
            return f"{rows:,} rows \u00d7 {columns} columns"
        if callable(v) and not isinstance(v, type):
            try:
                return getattr(v, "__name__", "function") + str(_carpo_inspect_mod.signature(v))
            except (TypeError, ValueError):
                pass
        text = _carpo_short.repr(v)
    except Exception:
        text = ""
    text = " ".join(text.split())
    return text if len(text) <= 200 else text[:200] + "..."


def _carpo_variables(g, expr):
    out = []
    for name in sorted(g):
        if name.startswith("_"):
            continue
        v = g[name]
        if isinstance(v, _carpo_types.ModuleType):
            continue
        out.append({"name": name, "type": type(v).__name__, "size": _carpo_size(v), "summary": _carpo_preview(v),
                    "table": _carpo_table_kind(v) is not None})
        if len(out) >= _CARPO_MAX_OBJECTS:
            break
    return _carpo_json.dumps(out)


def _carpo_cell(x):
    if x is None:
        return "None"
    if isinstance(x, float):
        # the shortest text that reads back as the same number (1.0, 0.1); numpy's floats as plain ones
        return "NaN" if x != x else repr(float(x))
    text = str(x)
    return text if len(text) <= 1000 else text[:1000]


def _carpo_table(g, expr):
    request = _carpo_json.loads(expr or "{}")
    name = request.get("name")
    if name not in g:
        raise KeyError(f"no variable {name}")
    v = g[name]
    kind = _carpo_table_kind(v)
    if kind is None:
        raise TypeError(f"{name} is not a DataFrame, a Series or an array")
    start = max(1, int(request.get("start", 1)))
    count = max(0, min(int(request.get("count", 100)), 100000))
    labels = None
    if kind == "pandas":
        frame = v.to_frame() if type(v).__name__ == "Series" else v
        n = len(frame)
        columns = [{"name": str(c), "type": str(t)} for c, t in zip(frame.columns, frame.dtypes)]
        page = frame.iloc[start - 1:start - 1 + count]
        rows = [[_carpo_cell(x) for x in row] for row in page.itertuples(index=False, name=None)]
        index = frame.index
        if not (type(index).__name__ == "RangeIndex" and index.start == 0 and index.step == 1):
            labels = [_carpo_cell(i) for i in page.index]
    elif kind == "polars":
        frame = v.to_frame() if type(v).__name__ == "Series" else v
        n = frame.height
        columns = [{"name": str(c), "type": str(t)} for c, t in zip(frame.columns, frame.dtypes)]
        rows = [[_carpo_cell(x) for x in row] for row in frame.slice(start - 1, count).rows()]
    else:
        array = v.reshape(-1, 1) if v.ndim == 1 else v
        n = array.shape[0]
        columns = [{"name": str(j), "type": str(v.dtype)} for j in range(array.shape[1])]
        rows = [[_carpo_cell(x) for x in row] for row in array[start - 1:start - 1 + count].tolist()]
    return _carpo_json.dumps({"name": name, "rowCount": n, "columns": columns, "start": start, "count": len(rows),
                              "rowLabels": labels, "rows": rows})


def __carpo_answer(key, expr, g):
    # The user expressions Carpo answers itself: (status, text, evalue), or None for an ordinary expression
    if key == ".jovian_variables":
        answer = _carpo_variables
    elif key == ".jovian_table":
        answer = _carpo_table
    else:
        return None
    try:
        return ("ok", answer(g, expr), "")
    except BaseException as e:
        return ("error", type(e).__name__, str(e))


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

        // Holds the GIL for one scope, from whichever thread: the kernel
        // thread between requests has released it (see the constructor), so
        // Python threads run while the kernel waits, and every call into
        // Python takes it here first.
        struct Gil
        {
            PyGILState_STATE state;
            Gil() : state(PyGILState_Ensure()) {}
            ~Gil() { PyGILState_Release(state); }
            Gil(const Gil&) = delete;
            Gil& operator=(const Gil&) = delete;
        };

        // Native callbacks exposed to the bootstrap source as
        // __carpo_native_write_stdout/__carpo_native_write_stderr (see this
        // file's header comment) -- called from Python's own write()
        // dispatch with the GIL held, on whichever thread wrote: the kernel
        // thread during a cell, or a thread / asyncio task of the user's at
        // any time (sys.stdout stays replaced). publishStream() is
        // thread-safe and attributes the text to the latest request.
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

        // Backs __carpo_input() (see kBootstrapSource's builtins.input
        // override) -- genuinely blocks this single execution thread via
        // adrastea::blockingInputRequest(), the same call R's ReadConsole()
        // (interpreter_r.cpp) makes. Any exception it throws (allow_stdin
        // was false, or some lower-level failure) is converted into a real
        // Python exception here rather than being allowed to unwind across
        // this C callback boundary into CPython's own C call stack -- the
        // same rule ReadConsole() follows for R, just expressed the way
        // Python callbacks are required to report failure (return nullptr
        // with an exception set, per the C API's own contract), so the
        // user's code sees an ordinary catchable exception raised from
        // input(), structured by __carpo_run's own except clause like any
        // other.
        PyObject* carpoNativeInput(PyObject* /*self*/, PyObject* args)
        {
            std::string prompt = pyUnicodeToStdString(PyTuple_GetItem(args, 0));
            std::string value;
            std::string error;
            // The user may take minutes to answer: other Python threads keep
            // running meanwhile.
            PyThreadState* saved = PyEval_SaveThread();
            try
            {
                value = adrastea::blockingInputRequest(prompt, false, p_interpreter && p_interpreter->allowsStdin());
            }
            catch (const std::exception& e)
            {
                error = e.what();
                if (error.empty()) error = "input request failed";
            }
            PyEval_RestoreThread(saved);
            if (!error.empty())
            {
                PyErr_SetString(PyExc_RuntimeError, error.c_str());
                return nullptr;
            }
            return PyUnicode_FromString(value.c_str());
        }

        PyMethodDef kInputDef = { "__carpo_native_input", carpoNativeInput, CARPO_PY_METH_VARARGS, nullptr };
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
        , m_bootstrapEvalExprFn(nullptr)
        , m_bootstrapAnswerFn(nullptr)
        , m_ownsInterpreter(false)
        , m_mainThread()
        , m_finalized(false)
    {
#ifndef _WIN32
        // See m_mainThread in the header: interrupts are delivered here.
        m_mainThread = pthread_self();
#endif
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
            // Py_Initialize() leaves this thread holding the GIL. Give it up
            // for good: while the kernel waits for requests, Python threads
            // the user started must run, and each call into Python below and
            // in the request handlers takes it for just that call (Gil).
            m_mainThreadState = PyEval_SaveThread();
        }
        Gil gil;

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
        py::Ref inputFn(PyCFunction_NewEx(&kInputDef, nullptr, nullptr));
        if (!stdoutFn || !stderrFn || !inputFn)
        {
            throw std::runtime_error(describePythonError("Could not create Carpo's native stdout/stderr/input callbacks"));
        }
        PyDict_SetItemString(bootstrapGlobals.get(), "__carpo_native_write_stdout", stdoutFn.get());
        PyDict_SetItemString(bootstrapGlobals.get(), "__carpo_native_write_stderr", stderrFn.get());
        PyDict_SetItemString(bootstrapGlobals.get(), "__carpo_native_input", inputFn.get());

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
        PyObject* evalExprFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_eval_expr");
        PyObject* answerFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_answer");
        PyObject* idleFn = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_idle");
        PyObject* versionObj = PyDict_GetItemString(bootstrapGlobals.get(), "__carpo_version");
        if (!runFn || !isCompleteFn || !completeFn || !inspectFn || !evalExprFn || !answerFn || !idleFn || !versionObj)
        {
            throw std::runtime_error(
                "Carpo's internal Python bootstrap runtime did not define the expected functions -- "
                "this is a bug in carpo itself, not a user-facing configuration problem.");
        }

        Py_IncRef(runFn);
        Py_IncRef(isCompleteFn);
        Py_IncRef(completeFn);
        Py_IncRef(inspectFn);
        Py_IncRef(evalExprFn);
        Py_IncRef(answerFn);
        Py_IncRef(idleFn);
        m_bootstrapRunFn = runFn;
        m_bootstrapIsCompleteFn = isCompleteFn;
        m_bootstrapCompleteFn = completeFn;
        m_bootstrapInspectFn = inspectFn;
        m_bootstrapEvalExprFn = evalExprFn;
        m_bootstrapAnswerFn = answerFn;
        m_bootstrapIdleFn = idleFn;
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

        // Py_FinalizeEx() must run on the thread that initialized Python,
        // holding the GIL with that thread's own state -- take it back the
        // way the constructor gave it up. Without ownership, just the GIL.
        std::unique_ptr<Gil> gil;
        if (m_ownsInterpreter)
        {
            PyEval_RestoreThread(static_cast<PyThreadState*>(m_mainThreadState));
        }
        else
        {
            gil = std::make_unique<Gil>();
        }

        // Release our own references before finalizing -- Py_FinalizeEx()
        // reclaims everything regardless, but doing this unconditionally
        // keeps this function's shape the same whether or not
        // m_ownsInterpreter ends up true below, and costs nothing.
        if (m_bootstrapIdleFn)
        {
            Py_DecRef(static_cast<PyObject*>(m_bootstrapIdleFn));
            m_bootstrapIdleFn = nullptr;
        }
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
        if (m_bootstrapEvalExprFn)
        {
            Py_DecRef(static_cast<PyObject*>(m_bootstrapEvalExprFn));
            m_bootstrapEvalExprFn = nullptr;
        }
        if (m_bootstrapAnswerFn)
        {
            Py_DecRef(static_cast<PyObject*>(m_bootstrapAnswerFn));
            m_bootstrapAnswerFn = nullptr;
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
        adrastea::json user_expressions)
    {
        struct ExecutingScope {
            std::atomic<bool>& flag;
            explicit ExecutingScope(std::atomic<bool>& f) : flag(f) { flag = true; }
            ~ExecutingScope() { flag = false; }
        } executing(m_executing);

        // Everything that touches Python runs holding the GIL; the reply is
        // sent after it has been released, so sending it (and aborting what
        // is queued behind a failure) does not hold up the user's threads.
        adrastea::json reply;
        {
            Gil gil;
            reply = runCell(execution_count, code, std::move(user_expressions));
        }
        cb(std::move(reply));
    }

    adrastea::json PyInterpreter::runCell(int execution_count, const std::string& code, adrastea::json user_expressions)
    {
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
            return adrastea::createErrorReply("CarpoInternalError", message, {});
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
            return adrastea::createErrorReply(ename, evalue, std::move(traceBack));
        }

        if (hasResult)
        {
            adrastea::json data = { { "text/plain", resultRepr } };
            publishExecutionResult(execution_count, data, adrastea::json::object());
        }
        // Evaluated after the code itself, and only on success, per the
        // spec. Each expression is independent: one failing is reported as
        // that expression's own error, not the execution's.
        adrastea::json userExpressionResults = adrastea::json::object();
        if (user_expressions.is_object())
        {
            for (auto it = user_expressions.begin(); it != user_expressions.end(); ++it)
            {
                const std::string expr = it.value().is_string() ? it.value().get<std::string>() : std::string();

                // `.jovian_variables` and `.jovian_table` are answered by the bootstrap (__carpo_answer), with JSON
                py::Ref answerArgs(PyTuple_New(3));
                PyTuple_SetItem(answerArgs.get(), 0, PyUnicode_FromString(it.key().c_str()));
                PyTuple_SetItem(answerArgs.get(), 1, PyUnicode_FromString(expr.c_str()));
                Py_IncRef(static_cast<PyObject*>(m_userGlobals));
                PyTuple_SetItem(answerArgs.get(), 2, static_cast<PyObject*>(m_userGlobals));
                py::Ref answer(PyObject_CallObject(static_cast<PyObject*>(m_bootstrapAnswerFn), answerArgs.get()));
                if (!answer)
                {
                    PyErr_Clear();
                }
                else if (answer.get() != Py_None)
                {
                    const std::string status = pyUnicodeToStdString(PyTuple_GetItem(answer.get(), 0));
                    const std::string text = pyUnicodeToStdString(PyTuple_GetItem(answer.get(), 1));
                    if (status == "ok")
                    {
                        userExpressionResults[it.key()] = { { "status", "ok" },
                            { "data", { { "text/plain", text } } }, { "metadata", adrastea::json::object() } };
                    }
                    else
                    {
                        userExpressionResults[it.key()] = { { "status", "error" }, { "ename", text },
                            { "evalue", pyUnicodeToStdString(PyTuple_GetItem(answer.get(), 2)) },
                            { "traceback", adrastea::json::array() } };
                    }
                    continue;
                }

                py::Ref evalArgs(PyTuple_New(2));
                PyTuple_SetItem(evalArgs.get(), 0, PyUnicode_FromString(expr.c_str()));
                Py_IncRef(static_cast<PyObject*>(m_userGlobals));
                PyTuple_SetItem(evalArgs.get(), 1, static_cast<PyObject*>(m_userGlobals));

                py::Ref evalResult(PyObject_CallObject(static_cast<PyObject*>(m_bootstrapEvalExprFn), evalArgs.get()));
                if (!evalResult)
                {
                    PyErr_Clear();
                    userExpressionResults[it.key()] = { { "status", "error" }, { "ename", "EvaluationError" },
                        { "evalue", "could not evaluate expression" }, { "traceback", adrastea::json::array() } };
                    continue;
                }

                std::string exprStatus = pyUnicodeToStdString(PyTuple_GetItem(evalResult.get(), 0));
                std::string second = pyUnicodeToStdString(PyTuple_GetItem(evalResult.get(), 1));
                if (exprStatus == "ok")
                {
                    userExpressionResults[it.key()] = { { "status", "ok" },
                        { "data", { { "text/plain", second } } }, { "metadata", adrastea::json::object() } };
                }
                else
                {
                    userExpressionResults[it.key()] = { { "status", "error" }, { "ename", second },
                        { "evalue", pyUnicodeToStdString(PyTuple_GetItem(evalResult.get(), 2)) },
                        { "traceback", adrastea::json::array() } };
                }
            }
        }
        return adrastea::createSuccessfulReply(adrastea::json::array(), userExpressionResults);
    }

    bool PyInterpreter::answersWhileBusyImpl(const std::string& msg_type) const
    {
        // Answered from another thread while a cell runs: they only need the
        // GIL, which the running cell gives up every few milliseconds (and
        // for as long as it waits on I/O or sleeps). Code holding the GIL in
        // a long native call delays the answer until it returns.
        return msg_type == "complete_request" || msg_type == "inspect_request" || msg_type == "is_complete_request";
    }

    void PyInterpreter::idleImpl()
    {
        // Advances the session's asyncio loop (tasks left running by earlier
        // cells) -- see __carpo_idle in kBootstrapSource.
        if (m_finalized || !m_bootstrapIdleFn) return;
        Gil gil;
        py::Ref result(PyObject_CallObject(static_cast<PyObject*>(m_bootstrapIdleFn), nullptr));
        if (!result) PyErr_Clear();
    }

    adrastea::json PyInterpreter::completeRequestImpl(const std::string& code, int cursor_pos)
    {
        if (m_finalized) return adrastea::createCompleteReply(adrastea::json::array(), cursor_pos, cursor_pos);
        Gil gil;
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
        if (m_finalized) return adrastea::createInspectReply(false);
        Gil gil;
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
        if (m_finalized) return adrastea::createIsCompleteReply("unknown");
        Gil gil;
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
        // Runs on the kernel's control-channel thread while the interpreter
        // thread is busy (see adrastea::ServerZmqImpl's control watcher).
        // Only while a cell is actually executing: an interrupt with
        // nothing to interrupt would otherwise linger and abort the next
        // one.
        if (m_executing.load())
        {
            // A real SIGINT, not PyErr_SetInterrupt(): that only sets the
            // pending-signal flag, which a blocked time.sleep() / I/O call
            // never looks at (confirmed: it left time.sleep(60) running).
            // Python's own SIGINT handler (installed by the bootstrap, see
            // signal.signal() there) both sets the flag and wakes the
            // blocked call, then raises KeyboardInterrupt on the interpreter
            // thread. It never kills the process because that handler is
            // installed.
#ifdef _WIN32
            // The CRT runs the registered handler synchronously on this
            // thread; it sets the flag and signals the event time.sleep()
            // waits on.
            raise(SIGINT);
#else
            // POSIX: blocking calls only wake for a signal delivered to the
            // thread that is blocked.
            pthread_kill(m_mainThread, SIGINT);
#endif
        }
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
