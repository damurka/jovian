#define R_NO_REMAP

#ifndef ELARA_R_RTOOLS_HPP
#define ELARA_R_RTOOLS_HPP

#include "R.h"
#include "Rinternals.h"

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
            SEXP result = Rf_eval(call, R_GlobalEnv);

            UNPROTECT(2);
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