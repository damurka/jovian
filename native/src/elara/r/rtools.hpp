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

        template<class... Types>
        SEXP invokeHeraFn(const char* f, Types... args) {
            SEXP sym_hera = Rf_install("hera");
            SEXP sym_hera_call = Rf_install("hera_call");
            SEXP sym_triple_colon = Rf_install(":::");

            SEXP call_triple_colon = PROTECT(rCall(sym_triple_colon, sym_hera, sym_hera_call));
            SEXP call = PROTECT(rCall(call_triple_colon, Rf_mkString(f), args...));

            // hera:::hera_call(f, ...) itself failing to evaluate (most
            // commonly: hera isn't installed/loadable at all -- it's
            // optional, see RInterpreter::configureImpl()) is different
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

            UNPROTECT(2);

            if (errorOccurred) {
                throw std::runtime_error(
                    std::string("R evaluation of hera:::hera_call(\"") + f + "\", ...) failed "
                    "(is the 'hera' package installed?): " + R_curErrorBuf());
            }
            return result;
        }

        template <class... Types>
        inline SEXP newHeraR6(const char* klass, SEXP xp, Types... args) {
            SEXP sym_hera = Rf_install("hera");
            SEXP sym_hera_new = Rf_install("hera_new");
            SEXP sym_triple_colon = Rf_install(":::");

            SEXP call_triple_colon = PROTECT(rCall(sym_triple_colon, sym_hera, sym_hera_new));
            SEXP call = PROTECT(rCall(call_triple_colon, Rf_mkString(klass), xp, args...));
            SEXP result = Rf_eval(call, R_GlobalEnv);

            UNPROTECT(2);
            return result;
        }

    }
}

#endif