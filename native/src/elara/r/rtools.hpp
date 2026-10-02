#ifndef ELARA_R_RTOOLS_HPP
#define ELARA_R_RTOOLS_HPP

#include "elara/r/r_dynlib.hpp"

#include <stdexcept>
#include <string>

namespace elara
{
    namespace r
    {

        inline SEXP rPairlist(SEXP head) {
            return Rf_cons(head, R_NilValue);
        }

        inline SEXP rCall(SEXP head) {
            return Rf_lcons(head, R_NilValue);
        }

        template<class... Types>
        SEXP rPairlist(SEXP head, Types... tail) {
            PROTECT(head);
            head = Rf_cons(head, rPairlist(tail...));
            UNPROTECT(1);
            return head;
        }

        template<class... Types>
        SEXP rCall(SEXP head, Types... tail) {
            PROTECT(head);
            head = Rf_lcons(head, rPairlist(tail...));
            UNPROTECT(1);
            return head;
        }

        // `base::pkg_fn`, a call head no user object can mask
        inline SEXP baseFunction(const char* name) {
            return rCall(Rf_install("::"), Rf_install("base"), Rf_install(name));
        }

        // The environment the kernel's R code is in: "tools:jovian" on the search path (the loader attaches it, see
        // RInterpreter's hera_loader), found once; R_NilValue before it is loaded
        inline SEXP elaraEnvironment() {
            static SEXP env = R_NilValue;
            if (env == R_NilValue) {
                SEXP call = PROTECT(rCall(baseFunction("as.environment"), Rf_mkString("tools:jovian")));
                int error = 0;
                SEXP found = R_tryEval(call, R_GlobalEnv, &error);
                UNPROTECT(1);
                if (!error) {
                    R_PreserveObject(found);
                    env = found;
                }
            }
            return env;
        }

        // A function of the kernel's R code (`.jv.call`, `.jv.debug.set_breakpoints`, ...), or R_NilValue
        inline SEXP elaraFunction(const char* name) {
            SEXP env = elaraEnvironment();
            if (env == R_NilValue) return R_NilValue;
            SEXP call = PROTECT(rCall(baseFunction("get0"), Rf_mkString(name), env));
            int error = 0;
            SEXP fn = R_tryEval(call, R_GlobalEnv, &error);
            UNPROTECT(1);
            return error ? R_NilValue : fn;
        }

        template<class... Types>
        SEXP invokeHeraFn(const char* f, Types... args) {
            static SEXP jv_call = R_NilValue;
            if (jv_call == R_NilValue) {
                jv_call = elaraFunction(".jv.call");
                if (jv_call == R_NilValue) {
                    throw std::runtime_error(std::string("R evaluation of .jv.call(\"") + f + "\", ...) failed: the kernel's R code (tools:jovian) is not loaded");
                }
                R_PreserveObject(jv_call);
            }
            SEXP call = PROTECT(rCall(jv_call, Rf_mkString(f), args...));

            // .jv.call(f, ...) itself failing to evaluate (most
            // commonly: the kernel's R code could not be loaded -- the
            // kernel still starts, see RInterpreter::configureImpl()) is different
            // from a normal *user* code error, which hera's own R-level
            // execute() already catches internally and returns as a
            // hera-shaped "error_reply" R object (see executeRequestImpl's
            // Rf_inherits(result, "error_reply") check) -- there's no such
            // object here, since the call never produced a result at all.
            // This used to be a raw, unprotected Rf_eval(): an uncaught
            // R-level error had no established recovery context to longjmp
            // back to, which hung the whole kernel forever on the very
            // first execute_request if hera wasn't loaded (confirmed via a
            // real repro, not hypothetical) rather than failing just that
            // one request. R_tryEval() catches it safely; throwing here is
            // caught by KernelCore's existing per-message try/catch
            // (kernel_core.cpp's handleMessage, "ERROR: received bad
            // message"), which logs it and keeps the kernel alive instead.
            int errorOccurred = 0;
            SEXP result = R_tryEval(call, R_GlobalEnv, &errorOccurred);

            UNPROTECT(1);

            if (errorOccurred) {
                throw std::runtime_error(std::string("R evaluation of .jv.call(\"") + f + "\", ...) failed: " + R_curErrorBuf());
            }
            return result;
        }


    }
}

#endif