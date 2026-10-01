#include "elara/r/r_dynlib.hpp"
#include "elara/r/rtools.hpp"
#include "elara/r/json_convert.hpp"
#include "elara/r/comm_r.hpp"
#include "elara/log.hpp"
#include "elara/interpreter_r.hpp"
#include "adrastea/json.hpp"
#include "adrastea/input.hpp"
#include "adrastea/message.hpp"
#include "adrastea/comm.hpp"
#include "adrastea/logger.hpp"
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace elara
{
    namespace routines
    {
        SEXP toRJson(const adrastea::json &js)
        {
            SEXP out = PROTECT(Rf_mkString(js.dump(4).c_str()));
            Rf_classgets(out, Rf_mkString("json"));
            UNPROTECT(1);

            return out;
        }

        SEXP kernelInfoRequest()
        {
            auto info = adrastea::getInterpreter().kernelInfoRequest();
            SEXP out = PROTECT(Rf_mkString(info.dump(4).c_str()));
            Rf_classgets(out, Rf_mkString("json"));
            UNPROTECT(1);
            return out;
        }

        SEXP publishStream(SEXP name_, SEXP text_)
        {
            auto name = CHAR(STRING_ELT(name_, 0));
            auto text = CHAR(STRING_ELT(text_, 0));

            auto interpreter = getRInterpreter();
            interpreter->publishStream(name, text);

            return R_NilValue;
        }

        SEXP displayData(SEXP data_, SEXP metadata_)
        {
            auto data = jsonFromR(data_);
            auto metadata = jsonFromR(metadata_);

            getRInterpreter()->displayData(
                std::move(data), std::move(metadata), /* transient = */ adrastea::json::object());

            return R_NilValue;
        }

        SEXP updateDisplayData(SEXP data_, SEXP metadata_)
        {
            auto data = jsonFromR(data_);
            auto metadata = jsonFromR(metadata_);

            getRInterpreter()->updateDisplayData(
                std::move(data), std::move(metadata), /* transient = */ adrastea::json::object());

            return R_NilValue;
        }

        SEXP clearOutput(SEXP wait_)
        {
            bool wait = LOGICAL_ELT(wait_, 0) == TRUE;
            getRInterpreter()->clearOutput(wait);
            return R_NilValue;
        }

        SEXP isCompleteRequest(SEXP code_)
        {
            std::string code = CHAR(STRING_ELT(code_, 0));
            auto is_complete = getRInterpreter()->isCompleteRequest(code);

            SEXP out = PROTECT(Rf_mkString(is_complete.dump(4).c_str()));
            Rf_classgets(out, Rf_mkString("json"));
            UNPROTECT(1);
            return out;
        }

        // hera's log_*(): a line in the kernel's log (log.hpp), at the level given ("debug", "info", "warning",
        // "error"), when that level is written.
        SEXP elaraLog(SEXP level_, SEXP msg_)
        {
            auto level = log::parseLevel(Rf_translateCharUTF8(STRING_ELT(level_, 0)), log::Level::info);
            if (log::enabled(level))
            {
                log::write(level, std::string("hera: ") + Rf_translateCharUTF8(STRING_ELT(msg_, 0)));
            }
            return R_NilValue;
        }

        SEXP CommManager__getCommInfo(SEXP target_name_)
        {
            auto comms = adrastea::getInterpreter().getCommManager().comms();

            bool keep_all = Rf_isNull(target_name_);
            std::string target_name(keep_all ? "" : CHAR(STRING_ELT(target_name_, 0)));

            size_t comms_size = comms.size();
            size_t size = 0;
            if (keep_all)
            {
                size = comms_size;
            }
            else
            {
                auto comm_it = comms.begin();
                for (size_t i = 0; i < comms_size; i++, ++comm_it)
                {
                    if (target_name == comm_it->second->target().name())
                    {
                        size++;
                    }
                }
            }

            SEXP info = PROTECT(Rf_allocVector(VECSXP, size));
            SEXP info_names = PROTECT(Rf_allocVector(STRSXP, size));
            SEXP str_target_name = PROTECT(Rf_mkString("target_name"));
            auto comm_it = comms.begin();

            for (size_t i = 0; comm_it != comms.end(); ++comm_it)
            {
                auto *comm = comm_it->second;
                if (keep_all || target_name == comm->target().name())
                {
                    SEXP x = PROTECT(Rf_allocVector(STRSXP, 1));
                    Rf_namesgets(x, str_target_name);
                    SET_STRING_ELT(x, 0, Rf_mkChar(comm_it->second->target().name().c_str()));

                    SET_VECTOR_ELT(info, i, x);
                    UNPROTECT(1);

                    SET_STRING_ELT(info_names, i, Rf_mkChar(comm_it->first.toString().c_str()));
                    i++;
                }
            }
            Rf_namesgets(info, info_names);
            UNPROTECT(3);
            return info;
        }

        // ---- R values to and from JSON ----------------------------------------------------------------------------
        //
        // What hera used jsonlite for -- display data, comm messages, inspect replies -- so a session needs no CRAN
        // package for it. The rules are jsonlite's toJSON(auto_unbox = TRUE) and fromJSON(), for what goes through
        // here: a length-one vector is a scalar (unless I()), longer ones arrays; a named list is an object, an
        // unnamed one an array; NA, NaN and Inf are null; a factor is its labels; a matrix is an array of its rows;
        // a data frame an array of row objects (an NA field left out); a raw vector a base64 string (unwrapped:
        // jsonlite's had line breaks a strict decoder rejects); a "json" string is embedded as is.

        static std::string base64Encode(const unsigned char *data, size_t n)
        {
            static const char *const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((n + 2) / 3) * 4);
            size_t i = 0;
            for (; i + 2 < n; i += 3)
            {
                unsigned v = (unsigned(data[i]) << 16) | (unsigned(data[i + 1]) << 8) | unsigned(data[i + 2]);
                out += chars[(v >> 18) & 63];
                out += chars[(v >> 12) & 63];
                out += chars[(v >> 6) & 63];
                out += chars[v & 63];
            }
            if (i < n)
            {
                unsigned v = unsigned(data[i]) << 16;
                if (i + 1 < n) v |= unsigned(data[i + 1]) << 8;
                out += chars[(v >> 18) & 63];
                out += chars[(v >> 12) & 63];
                out += i + 1 < n ? chars[(v >> 6) & 63] : '=';
                out += '=';
            }
            return out;
        }

        static SEXP attribute(SEXP x, const char *name)
        {
            return Rf_getAttrib(x, Rf_install(name));
        }

        static SEXP mkCharUtf8(const std::string &s)
        {
            return Rf_mkCharLenCE(s.data(), static_cast<int>(s.size()), CE_UTF8);
        }

        // Element i of an atomic vector (a factor's when `levels` isn't R_NilValue) as a JSON scalar.
        static adrastea::json atomicElement(SEXP x, R_xlen_t i, SEXP levels)
        {
            switch (TYPEOF(x))
            {
            case LGLSXP:
            {
                int v = LOGICAL_ELT(x, i);
                return v == NA_LOGICAL ? adrastea::json() : adrastea::json(v != 0);
            }
            case INTSXP:
            {
                int v = INTEGER_ELT(x, i);
                if (v == NA_INTEGER) return adrastea::json();
                if (levels != R_NilValue)
                {
                    return v >= 1 && v <= XLENGTH(levels) ? adrastea::json(Rf_translateCharUTF8(STRING_ELT(levels, v - 1))) : adrastea::json();
                }
                return v;
            }
            case REALSXP:
            {
                double v = REAL_ELT(x, i);
                return std::isfinite(v) ? adrastea::json(v) : adrastea::json();
            }
            case STRSXP:
            {
                SEXP s = STRING_ELT(x, i);
                return s == NA_STRING ? adrastea::json() : adrastea::json(Rf_translateCharUTF8(s));
            }
            default:
                return adrastea::json();
            }
        }

        static bool isAtomic(int type)
        {
            return type == LGLSXP || type == INTSXP || type == REALSXP || type == STRSXP;
        }

        // `nullAsObject`: what NULL becomes -- {} (jsonlite's default) or null.
        adrastea::json sexpToJson(SEXP x, bool nullAsObject)
        {
            if (x == R_NilValue) return nullAsObject ? adrastea::json::object() : adrastea::json();
            int type = TYPEOF(x);

            if (type == STRSXP && XLENGTH(x) == 1 && Rf_inherits(x, "json"))
            {
                return adrastea::json::parse(Rf_translateCharUTF8(STRING_ELT(x, 0)), nullptr, false);
            }
            if (type == RAWSXP)
            {
                return base64Encode(RAW(x), static_cast<size_t>(XLENGTH(x)));
            }
            if (type == VECSXP && Rf_inherits(x, "data.frame"))
            {
                SEXP names = attribute(x, "names");
                // row names of its own (mtcars' car names, not 1, 2, ...) go in each row as "_row", as jsonlite does
                SEXP rowNames = attribute(x, "row.names");
                bool namedRows = TYPEOF(rowNames) == STRSXP;
                R_xlen_t columns = XLENGTH(x);
                R_xlen_t rows = columns ? Rf_xlength(VECTOR_ELT(x, 0)) : 0;
                adrastea::json out = adrastea::json::array();
                for (R_xlen_t r = 0; r < rows; ++r)
                {
                    adrastea::json row = adrastea::json::object();
                    if (namedRows && r < XLENGTH(rowNames)) row["_row"] = atomicElement(rowNames, r, R_NilValue);
                    for (R_xlen_t c = 0; c < columns; ++c)
                    {
                        SEXP column = VECTOR_ELT(x, c);
                        std::string name = Rf_translateCharUTF8(STRING_ELT(names, c));
                        if (TYPEOF(column) == VECSXP)
                        {
                            row[name] = sexpToJson(VECTOR_ELT(column, r), nullAsObject);
                        }
                        else if (isAtomic(TYPEOF(column)))
                        {
                            auto value = atomicElement(column, r, Rf_inherits(column, "factor") ? attribute(column, "levels") : R_NilValue);
                            if (!value.is_null()) row[name] = std::move(value);
                        }
                    }
                    out.push_back(std::move(row));
                }
                return out;
            }
            if (type == VECSXP)
            {
                SEXP names = attribute(x, "names");
                R_xlen_t n = XLENGTH(x);
                if (names != R_NilValue)
                {
                    adrastea::json out = adrastea::json::object();
                    for (R_xlen_t i = 0; i < n; ++i)
                    {
                        out[Rf_translateCharUTF8(STRING_ELT(names, i))] = sexpToJson(VECTOR_ELT(x, i), nullAsObject);
                    }
                    return out;
                }
                adrastea::json out = adrastea::json::array();
                for (R_xlen_t i = 0; i < n; ++i)
                {
                    out.push_back(sexpToJson(VECTOR_ELT(x, i), nullAsObject));
                }
                return out;
            }
            if (isAtomic(type))
            {
                SEXP levels = Rf_inherits(x, "factor") ? attribute(x, "levels") : R_NilValue;
                SEXP dim = attribute(x, "dim");
                if (dim != R_NilValue && XLENGTH(dim) == 2)
                {
                    int nrow = INTEGER_ELT(dim, 0), ncol = INTEGER_ELT(dim, 1);
                    adrastea::json out = adrastea::json::array();
                    for (int r = 0; r < nrow; ++r)
                    {
                        adrastea::json row = adrastea::json::array();
                        for (int c = 0; c < ncol; ++c)
                        {
                            row.push_back(atomicElement(x, r + static_cast<R_xlen_t>(c) * nrow, levels));
                        }
                        out.push_back(std::move(row));
                    }
                    return out;
                }
                R_xlen_t n = XLENGTH(x);
                if (n == 1 && !Rf_inherits(x, "AsIs")) return atomicElement(x, 0, levels);
                adrastea::json out = adrastea::json::array();
                for (R_xlen_t i = 0; i < n; ++i)
                {
                    out.push_back(atomicElement(x, i, levels));
                }
                return out;
            }
            // functions, environments, ...: nothing JSON can hold
            return adrastea::json();
        }

        // Whether a JSON number is a whole number an R integer holds (INT_MIN is NA_integer_).
        static bool fitsInteger(const adrastea::json &e)
        {
            if (e.is_number_unsigned()) return e.get<std::uint64_t>() <= static_cast<std::uint64_t>(INT_MAX);
            if (!e.is_number_integer()) return false;
            auto v = e.get<std::int64_t>();
            return v > INT_MIN && v <= INT_MAX;
        }

        // A JSON scalar as R's as.character() writes it: "1", "2.5", "TRUE".
        static std::string scalarText(const adrastea::json &e)
        {
            if (e.is_string()) return e.get<std::string>();
            if (e.is_boolean()) return e.get<bool>() ? "TRUE" : "FALSE";
            if (e.is_number_float())
            {
                char buffer[32];
                std::snprintf(buffer, sizeof buffer, "%.15g", e.get<double>());
                return buffer;
            }
            return e.dump();
        }

        // jsonlite's fromJSON(): an object is a named list; an array of scalars (null as NA) an atomic vector --
        // logical, integer, double or character, whichever holds them all -- and any other array a list.
        SEXP jsonToSexp(const adrastea::json &j)
        {
            using value_t = adrastea::json::value_t;
            switch (j.type())
            {
            case value_t::boolean:
                return Rf_ScalarLogical(j.get<bool>() ? 1 : 0);
            case value_t::number_integer:
            case value_t::number_unsigned:
            {
                if (fitsInteger(j)) return Rf_ScalarInteger(static_cast<int>(j.get<std::int64_t>()));
                SEXP out = PROTECT(Rf_allocVector(REALSXP, 1));
                SET_REAL_ELT(out, 0, j.get<double>());
                UNPROTECT(1);
                return out;
            }
            case value_t::number_float:
            {
                SEXP out = PROTECT(Rf_allocVector(REALSXP, 1));
                SET_REAL_ELT(out, 0, j.get<double>());
                UNPROTECT(1);
                return out;
            }
            case value_t::string:
            {
                SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
                SET_STRING_ELT(out, 0, mkCharUtf8(j.get_ref<const std::string &>()));
                UNPROTECT(1);
                return out;
            }
            case value_t::object:
            {
                SEXP out = PROTECT(Rf_allocVector(VECSXP, static_cast<R_xlen_t>(j.size())));
                SEXP names = PROTECT(Rf_allocVector(STRSXP, static_cast<R_xlen_t>(j.size())));
                R_xlen_t i = 0;
                for (auto it = j.begin(); it != j.end(); ++it, ++i)
                {
                    SET_VECTOR_ELT(out, i, jsonToSexp(it.value()));
                    SET_STRING_ELT(names, i, mkCharUtf8(it.key()));
                }
                Rf_namesgets(out, names);
                UNPROTECT(2);
                return out;
            }
            case value_t::array:
            {
                R_xlen_t n = static_cast<R_xlen_t>(j.size());
                bool scalars = n > 0, logical = true, integer = true, number = true, string = true;
                for (const auto &e : j)
                {
                    if (e.is_null()) continue;
                    if (!e.is_primitive()) { scalars = false; break; }
                    logical = logical && e.is_boolean();
                    string = string && e.is_string();
                    number = number && e.is_number();
                    integer = integer && fitsInteger(e);
                }
                if (!scalars)
                {
                    SEXP out = PROTECT(Rf_allocVector(VECSXP, n));
                    for (R_xlen_t i = 0; i < n; ++i) SET_VECTOR_ELT(out, i, jsonToSexp(j[static_cast<size_t>(i)]));
                    UNPROTECT(1);
                    return out;
                }
                // all null: logical NAs, as jsonlite gives; mixed scalars are coerced as jsonlite does, to character
                // when there is a string, else to double (true as 1)
                SEXPTYPE type = logical ? LGLSXP : integer ? INTSXP : number ? REALSXP : string ? STRSXP : VECSXP;
                if (type == VECSXP)
                {
                    bool anyString = false;
                    for (const auto &e : j) anyString = anyString || e.is_string();
                    type = anyString ? STRSXP : REALSXP;
                }
                SEXP out = PROTECT(Rf_allocVector(type, n));
                for (R_xlen_t i = 0; i < n; ++i)
                {
                    const auto &e = j[static_cast<size_t>(i)];
                    switch (type)
                    {
                    case LGLSXP: SET_LOGICAL_ELT(out, i, e.is_null() ? NA_LOGICAL : (e.get<bool>() ? 1 : 0)); break;
                    case INTSXP: SET_INTEGER_ELT(out, i, e.is_null() ? NA_INTEGER : static_cast<int>(e.get<std::int64_t>())); break;
                    case REALSXP: SET_REAL_ELT(out, i, e.is_null() ? NA_REAL : e.is_boolean() ? (e.get<bool>() ? 1.0 : 0.0) : e.get<double>()); break;
                    case STRSXP: SET_STRING_ELT(out, i, e.is_null() ? NA_STRING : mkCharUtf8(scalarText(e))); break;
                    default: SET_VECTOR_ELT(out, i, jsonToSexp(e)); break;
                    }
                }
                UNPROTECT(1);
                return out;
            }
            case value_t::null:
            default:
                return R_NilValue;
            }
        }

        adrastea::json jsonFromR(SEXP x)
        {
            if (TYPEOF(x) == STRSXP && XLENGTH(x) == 1 && attribute(x, "names") == R_NilValue)
            {
                return adrastea::json::parse(Rf_translateCharUTF8(STRING_ELT(x, 0)));
            }
            return sexpToJson(x, true);
        }

        // What the current device's display list is now, as text that changes whenever something is drawn or the plot
        // is cleared: the list and its last element (R appends to it) and its length. hera compares it before
        // recording the plot (recordPlot(), which copies the whole list -- all the points of a big scatter plot)
        // after each expression: with the plot device kept for the session, that cost every later cell once a big
        // plot had been drawn. Only called with a device open (GEcurrentDevice() would open one).
        SEXP displayListId()
        {
            pGEDevDesc device = GEcurrentDevice();
            if (!device) return R_NilValue;
            char buffer[96];
            std::snprintf(buffer, sizeof buffer, "%p:%p:%d", static_cast<void *>(device->displayList),
                          static_cast<void *>(device->DLlastElt), Rf_length(device->displayList));
            return Rf_mkString(buffer);
        }

        // hera's .jv.errors.handler(), R's global error handler: whether an error is the running cell's (otherwise
        // R handles it as usual), and that error's report -- message and traceback lines -- for the cell's reply.
        SEXP cellErrorWanted()
        {
            return Rf_ScalarLogical(getRInterpreter()->wantsCellError() ? 1 : 0);
        }

        SEXP recordCellError(SEXP evalue_, SEXP traceback_)
        {
            std::string evalue = TYPEOF(evalue_) == STRSXP && XLENGTH(evalue_) > 0 ? Rf_translateCharUTF8(STRING_ELT(evalue_, 0)) : "";
            std::vector<std::string> traceback;
            if (TYPEOF(traceback_) == STRSXP)
            {
                for (R_xlen_t i = 0; i < XLENGTH(traceback_); ++i)
                {
                    traceback.push_back(Rf_translateCharUTF8(STRING_ELT(traceback_, i)));
                }
            }
            getRInterpreter()->recordCellError(std::move(evalue), std::move(traceback));
            return R_NilValue;
        }

        // hera's .jv.ui.ask(): a question for the host's UI (rstudioapi::showPrompt(), showQuestion(), ...), sent as an
        // input_request whose content has `jovian_ui: {method, params}`; the answer is the reply's text (JSON), or
        // NULL when it could not be asked: an execution that allows no input, unless the kernel runs under Jovian's
        // supervisor (JOVIAN_SUPERVISED, set by elara.cpp), whose client answers every such question.
        SEXP uiAsk(SEXP method_, SEXP params_, SEXP password_)
        {
            adrastea::json ui = {
                {"method", TYPEOF(method_) == STRSXP && XLENGTH(method_) > 0 ? Rf_translateCharUTF8(STRING_ELT(method_, 0)) : ""},
                {"params", sexpToJson(params_, false)}};
            bool password = TYPEOF(password_) == LGLSXP && XLENGTH(password_) > 0 && LOGICAL_ELT(password_, 0) == 1;
            static const bool supervised = std::getenv("JOVIAN_SUPERVISED") != nullptr;
            std::string reply;
            try
            {
                reply = adrastea::blockingInputRequest("", password, supervised || getRInterpreter()->allowsStdin(), ui);
            }
            catch (const std::exception &e)
            {
                log::debug(std::string("a UI question was not asked: ") + e.what());
                return R_NilValue;
            }
            SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
            SET_STRING_ELT(out, 0, mkCharUtf8(reply));
            UNPROTECT(1);
            return out;
        }

        // hera's .jv.debug.on_interrupt(): whether the interrupt is a pause the debugger asked for
        SEXP debugTakePause()
        {
            return Rf_ScalarLogical(getRInterpreter()->takeDebugPause() ? 1 : 0);
        }

        // hera's to_json(): x as JSON text, NULL as {} or (null = "null") null.
        SEXP toJson(SEXP x, SEXP null_)
        {
            bool nullAsObject = !(TYPEOF(null_) == STRSXP && XLENGTH(null_) == 1 && std::string(Rf_translateCharUTF8(STRING_ELT(null_, 0))) == "null");
            std::string text = sexpToJson(x, nullAsObject).dump(-1, ' ', false, adrastea::json::error_handler_t::replace);
            SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
            SET_STRING_ELT(out, 0, mkCharUtf8(text));
            UNPROTECT(1);
            return out;
        }

        // hera's from_json(): JSON text as an R value.
        SEXP fromJson(SEXP text_)
        {
            auto parsed = adrastea::json::parse(Rf_translateCharUTF8(STRING_ELT(text_, 0)), nullptr, false);
            if (parsed.is_discarded()) return R_NilValue;
            return jsonToSexp(parsed);
        }

    }

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    void registerRRoutines()
    {
        DllInfo *info = R_getEmbeddingDllInfo();

        static const R_CallMethodDef callMethods[] = {
            {"elara_kernel_info_request", (DL_FUNC)&routines::kernelInfoRequest, 0},
            {"elara_publish_stream", (DL_FUNC)&routines::publishStream, 2},
            {"elara_display_data", (DL_FUNC)&routines::displayData, 2},
            {"elara_update_display_data", (DL_FUNC)&routines::updateDisplayData, 2},
            {"elara_clear_output", (DL_FUNC)&routines::clearOutput, 1},
            {"elara_is_complete_request", (DL_FUNC)&routines::isCompleteRequest, 1},
            {"elara_log", (DL_FUNC)&routines::elaraLog, 2},

            // CommManager
            {"CommManager__get_comm_info", (DL_FUNC)&routines::CommManager__getCommInfo, 1},



            // JSON (hera's to_json() / from_json())
            {"elara_to_json", (DL_FUNC)&routines::toJson, 2},
            {"elara_from_json", (DL_FUNC)&routines::fromJson, 1},
            {"elara_display_list_id", (DL_FUNC)&routines::displayListId, 0},
            {"elara_cell_error_wanted", (DL_FUNC)&routines::cellErrorWanted, 0},
            {"elara_ui_ask", (DL_FUNC)&routines::uiAsk, 3},
            {"elara_debug_take_pause", (DL_FUNC)&routines::debugTakePause, 0},
            {"elara_record_cell_error", (DL_FUNC)&routines::recordCellError, 2},

            // comms (comm_r.cpp)
            {"elara_comm_register_target", (DL_FUNC)&comms::registerTarget, 2},
            {"elara_comm_unregister_target", (DL_FUNC)&comms::unregisterTarget, 1},
            {"elara_comm_target_callback", (DL_FUNC)&comms::targetCallback, 1},
            {"elara_comm_new", (DL_FUNC)&comms::newComm, 2},
            {"elara_comm_list", (DL_FUNC)&comms::list, 0},
            {"elara_comm_target_name", (DL_FUNC)&comms::targetName, 1},
            {"elara_comm_description", (DL_FUNC)&comms::description, 1},
            {"elara_comm_open", (DL_FUNC)&comms::open, 4},
            {"elara_comm_send", (DL_FUNC)&comms::send, 4},
            {"elara_comm_close", (DL_FUNC)&comms::close, 4},
            {"elara_comm_on_message", (DL_FUNC)&comms::setOnMessage, 2},
            {"elara_comm_on_close", (DL_FUNC)&comms::setOnClose, 2},

            {NULL, NULL, 0}};

        R_registerRoutines(info, NULL, callMethods, NULL, NULL);
    }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

}
