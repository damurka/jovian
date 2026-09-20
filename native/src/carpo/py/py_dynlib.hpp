#ifndef CARPO_PY_DYNLIB_HPP
#define CARPO_PY_DYNLIB_HPP

// Single include point for Python's C API across this codebase.
//
// carpo loads Python's shared library (pythonXY.dll / libpythonX.Y.so* /
// libpythonX.Y.dylib) at RUNTIME via LoadLibrary+GetProcAddress (Windows) or
// dlopen+dlsym (Linux/macOS) instead of linking against it at build time --
// the same architecture native/src/elara/r/r_dynlib.hpp uses for R (see that
// file's comment for the full "why"; the short version: switching Python
// installations/versions needs no rebuild, and a missing/broken Python
// becomes an ordinary catchable std::runtime_error instead of the whole
// process failing to start).
//
// Unlike r_dynlib.hpp, this file does NOT include the real Python.h. R's
// SEXP is already a single opaque pointer typedef with no stable-layout
// concerns; PyObject's layout, by contrast, genuinely varies across Python
// versions once you look past its reference count/type pointer header, and
// several of Python's own macros (Py_INCREF et al.) reach into that layout
// directly for speed. Every function declared below is instead one that
// CPython treats as stable, documented ("limited"/stable ABI) C API surface
// -- plain function calls, no direct struct field access -- so the exact
// struct layout never matters here: PyObject stays a fully opaque type, and
// even reference counting goes through the real Py_IncRef/Py_DecRef
// *functions* (not the Py_INCREF/Py_DECREF macros, which would read/write
// ob_refcnt directly). PyMethodDef/PyModuleDef, which a real native-callback
// module would need, are deliberately NOT declared here at all: this
// implementation runs all interpreter logic (packages/hera's equivalent, see
// py_bootstrap in interpreter_py.cpp) as pure Python source executed via
// PyRun_String, with no native-callback module needed.
//
// Only the handful of stable-ABI functions this codebase actually calls are
// declared -- not an attempt at a general-purpose CPython binding.

#include <cstddef>
#include <string>

namespace carpo { namespace py {

    // Loads Python's shared library (searched under EnvironmentConfig's
    // python_home, mirroring R_HOME) and resolves every symbol this codebase
    // calls (see py_dynlib.cpp for the full list). Throws
    // std::runtime_error with a specific, actionable message if the library
    // (or the specific version file within pythonHome) or any expected
    // symbol can't be found. Safe to call more than once -- only the first
    // call does any work.
    //
    // Must be called before touching ANY of the Python API names below (they
    // are null function pointers until this succeeds).
    void loadPyApi(const std::string& pythonHome);

    bool isPyApiLoaded();

} }

extern "C" {
    // Deliberately opaque -- see this file's comment. Every function below
    // only ever passes PyObject* around by pointer; nothing here reads or
    // writes through it.
    typedef struct _object PyObject;

    // Matches CPython's own typedef (pyport.h: Py_ssize_t is Py_intptr_t) --
    // a pointer-width signed integer on every platform this project targets.
    using Py_ssize_t = std::ptrdiff_t;
}

extern "C" {
    using Py_Initialize_t = void (*)(void);
    using Py_IsInitialized_t = int (*)(void);
    using Py_FinalizeEx_t = int (*)(void);

    using PyErr_Occurred_t = PyObject* (*)(void);
    using PyErr_Fetch_t = void (*)(PyObject**, PyObject**, PyObject**);
    using PyErr_NormalizeException_t = void (*)(PyObject**, PyObject**, PyObject**);
    using PyErr_Clear_t = void (*)(void);

    using PyObject_Str_t = PyObject* (*)(PyObject*);
    using PyObject_GetAttrString_t = PyObject* (*)(PyObject*, const char*);
    using PyObject_CallObject_t = PyObject* (*)(PyObject*, PyObject*);

    using Py_IncRef_t = void (*)(PyObject*);
    using Py_DecRef_t = void (*)(PyObject*);

    using PyUnicode_FromString_t = PyObject* (*)(const char*);
    using PyUnicode_AsUTF8String_t = PyObject* (*)(PyObject*);
    using PyBytes_AsString_t = char* (*)(PyObject*);

    using PyTuple_New_t = PyObject* (*)(Py_ssize_t);
    using PyTuple_SetItem_t = int (*)(PyObject*, Py_ssize_t, PyObject*);
    using PyTuple_GetItem_t = PyObject* (*)(PyObject*, Py_ssize_t);

    using PyDict_New_t = PyObject* (*)(void);
    using PyDict_GetItemString_t = PyObject* (*)(PyObject*, const char*);

    using PyList_Size_t = Py_ssize_t (*)(PyObject*);
    using PyList_GetItem_t = PyObject* (*)(PyObject*, Py_ssize_t);

    using PyLong_AsLong_t = long (*)(PyObject*);

    using PyImport_AddModule_t = PyObject* (*)(const char*);
    using PyModule_GetDict_t = PyObject* (*)(PyObject*);

    using PyRun_String_t = PyObject* (*)(const char*, int, PyObject*, PyObject*);
}

namespace carpo { namespace py { namespace api {
    extern Py_Initialize_t p_Py_Initialize;
    extern Py_IsInitialized_t p_Py_IsInitialized;
    extern Py_FinalizeEx_t p_Py_FinalizeEx;

    extern PyErr_Occurred_t p_PyErr_Occurred;
    extern PyErr_Fetch_t p_PyErr_Fetch;
    extern PyErr_NormalizeException_t p_PyErr_NormalizeException;
    extern PyErr_Clear_t p_PyErr_Clear;

    extern PyObject_Str_t p_PyObject_Str;
    extern PyObject_GetAttrString_t p_PyObject_GetAttrString;
    extern PyObject_CallObject_t p_PyObject_CallObject;

    extern Py_IncRef_t p_Py_IncRef;
    extern Py_DecRef_t p_Py_DecRef;

    extern PyUnicode_FromString_t p_PyUnicode_FromString;
    extern PyUnicode_AsUTF8String_t p_PyUnicode_AsUTF8String;
    extern PyBytes_AsString_t p_PyBytes_AsString;

    extern PyTuple_New_t p_PyTuple_New;
    extern PyTuple_SetItem_t p_PyTuple_SetItem;
    extern PyTuple_GetItem_t p_PyTuple_GetItem;

    extern PyDict_New_t p_PyDict_New;
    extern PyDict_GetItemString_t p_PyDict_GetItemString;

    extern PyList_Size_t p_PyList_Size;
    extern PyList_GetItem_t p_PyList_GetItem;

    extern PyLong_AsLong_t p_PyLong_AsLong;

    extern PyImport_AddModule_t p_PyImport_AddModule;
    extern PyModule_GetDict_t p_PyModule_GetDict;

    extern PyRun_String_t p_PyRun_String;
} } }

#define Py_Initialize (*::carpo::py::api::p_Py_Initialize)
#define Py_IsInitialized (*::carpo::py::api::p_Py_IsInitialized)
#define Py_FinalizeEx (*::carpo::py::api::p_Py_FinalizeEx)

#define PyErr_Occurred (*::carpo::py::api::p_PyErr_Occurred)
#define PyErr_Fetch (*::carpo::py::api::p_PyErr_Fetch)
#define PyErr_NormalizeException (*::carpo::py::api::p_PyErr_NormalizeException)
#define PyErr_Clear (*::carpo::py::api::p_PyErr_Clear)

#define PyObject_Str (*::carpo::py::api::p_PyObject_Str)
#define PyObject_GetAttrString (*::carpo::py::api::p_PyObject_GetAttrString)
#define PyObject_CallObject (*::carpo::py::api::p_PyObject_CallObject)

#define Py_IncRef (*::carpo::py::api::p_Py_IncRef)
#define Py_DecRef (*::carpo::py::api::p_Py_DecRef)

#define PyUnicode_FromString (*::carpo::py::api::p_PyUnicode_FromString)
#define PyUnicode_AsUTF8String (*::carpo::py::api::p_PyUnicode_AsUTF8String)
#define PyBytes_AsString (*::carpo::py::api::p_PyBytes_AsString)

#define PyTuple_New (*::carpo::py::api::p_PyTuple_New)
#define PyTuple_SetItem (*::carpo::py::api::p_PyTuple_SetItem)
#define PyTuple_GetItem (*::carpo::py::api::p_PyTuple_GetItem)

#define PyDict_New (*::carpo::py::api::p_PyDict_New)
#define PyDict_GetItemString (*::carpo::py::api::p_PyDict_GetItemString)

#define PyList_Size (*::carpo::py::api::p_PyList_Size)
#define PyList_GetItem (*::carpo::py::api::p_PyList_GetItem)

#define PyLong_AsLong (*::carpo::py::api::p_PyLong_AsLong)

#define PyImport_AddModule (*::carpo::py::api::p_PyImport_AddModule)
#define PyModule_GetDict (*::carpo::py::api::p_PyModule_GetDict)

#define PyRun_String (*::carpo::py::api::p_PyRun_String)

// Py_file_input's numeric value (Python.h's Include/compile.h /
// cpython/compile.h) -- part of the same stable ABI everything else in this
// file relies on, unchanged since Python's earliest 3.x releases (it's baked
// into every compiled .pyc's expectations and can't change without breaking
// every extension module ever built). Used as PyRun_String's "start" symbol
// to compile/run a whole chunk of code as a module body, the same mode
// `python script.py` itself runs under.
#define CARPO_PY_FILE_INPUT 257

namespace carpo { namespace py {

    // Owns a new (strong) Python reference and Py_DecRef()s it on
    // destruction -- the RAII equivalent of R's PROTECT/UNPROTECT pairing
    // (elara/r/interpreter_r.cpp), just scope-based instead of stack-based.
    // Every *_dynlib function that returns "a new reference" per the Python
    // C API docs should have its result wrapped in one of these immediately.
    class Ref
    {
    public:
        Ref() noexcept : m_obj(nullptr) {}
        explicit Ref(PyObject* obj) noexcept : m_obj(obj) {}
        ~Ref() { reset(); }

        Ref(const Ref&) = delete;
        Ref& operator=(const Ref&) = delete;

        Ref(Ref&& other) noexcept : m_obj(other.release()) {}
        Ref& operator=(Ref&& other) noexcept
        {
            if (this != &other)
            {
                reset(other.release());
            }
            return *this;
        }

        PyObject* get() const noexcept { return m_obj; }
        explicit operator bool() const noexcept { return m_obj != nullptr; }

        PyObject* release() noexcept
        {
            PyObject* tmp = m_obj;
            m_obj = nullptr;
            return tmp;
        }

        void reset(PyObject* obj = nullptr)
        {
            PyObject* old = m_obj;
            m_obj = obj;
            if (old != nullptr)
            {
                Py_DecRef(old);
            }
        }

    private:
        PyObject* m_obj;
    };

} }

#endif // CARPO_PY_DYNLIB_HPP
