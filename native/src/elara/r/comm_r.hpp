#ifndef ELARA_R_COMM_R_HPP
#define ELARA_R_COMM_R_HPP

// Comms for R code, held in C++ (see comm_r.cpp): the .Call routines behind hera's CommManager and Comm.

#include "elara/r/r_dynlib.hpp"

namespace elara
{
    namespace comms
    {
        SEXP registerTarget(SEXP target, SEXP callback);
        SEXP unregisterTarget(SEXP target);
        SEXP targetCallback(SEXP target);
        SEXP newComm(SEXP target, SEXP description);
        SEXP list();
        SEXP targetName(SEXP id);
        SEXP description(SEXP id);
        SEXP open(SEXP id, SEXP data, SEXP metadata, SEXP buffers);
        SEXP send(SEXP id, SEXP data, SEXP metadata, SEXP buffers);
        SEXP close(SEXP id, SEXP data, SEXP metadata, SEXP buffers);
        SEXP setOnMessage(SEXP id, SEXP handler);
        SEXP setOnClose(SEXP id, SEXP handler);
    }
}

#endif // ELARA_R_COMM_R_HPP
