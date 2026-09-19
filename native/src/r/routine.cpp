#define R_NO_REMAP

#ifdef _MSC_VER
#define _Complex
#endif

#include "R.h"
#include "Rinternals.h"
#include "R_ext/Rdynload.h"

#include "r/rtools.hpp"
#include "elara/interpreter_r.hpp"
#include "adrastea/json.hpp"
#include "adrastea/message.hpp"
#include "adrastea/comm.hpp"
#include "adrastea/logger.hpp"

#ifdef _MSC_VER
#undef _Complex
#endif

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

        SEXP displayData(SEXP js_data, SEXP js_metadata)
        {
            auto data = adrastea::json::parse(CHAR(STRING_ELT(js_data, 0)));
            auto metadata = adrastea::json::parse(CHAR(STRING_ELT(js_metadata, 0)));

            getRInterpreter()->displayData(
                std::move(data), std::move(metadata), /* transient = */ adrastea::json::object());

            return R_NilValue;
        }

        SEXP updateDisplayData(SEXP js_data, SEXP js_metadata)
        {
            auto data = adrastea::json::parse(CHAR(STRING_ELT(js_data, 0)));
            auto metadata = adrastea::json::parse(CHAR(STRING_ELT(js_metadata, 0)));

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

        SEXP elaraLog(SEXP level_, SEXP msg_)
        {
            std::string level = CHAR(STRING_ELT(level_, 0));
            std::string msg = CHAR(STRING_ELT(msg_, 0));

            // TODO: actually do some logging
            return R_NilValue;
        }

        SEXP CommManager__registerTarget(SEXP name_)
        {
            std::string name = CHAR(STRING_ELT(name_, 0));

            auto callback = [name](adrastea::Comm &&comm, adrastea::Message request)
            {
                // comm
                auto ptr_comm = new adrastea::Comm(std::move(comm));
                SEXP xp_comm = PROTECT(R_MakeExternalPtr(
                    reinterpret_cast<void *>(ptr_comm), R_NilValue, R_NilValue));
                R_RegisterCFinalizerEx(xp_comm, [](SEXP xp)
                                       { delete reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp)); }, FALSE);
                SEXP r6_comm = PROTECT(r::newHeraR6("Comm", xp_comm));

                // request
                auto ptr_request = new adrastea::Message(std::move(request));
                SEXP xptr_request = PROTECT(R_MakeExternalPtr(
                    reinterpret_cast<void *>(ptr_request), R_NilValue, R_NilValue));
                R_RegisterCFinalizerEx(xptr_request, [](SEXP xp)
                                       { delete reinterpret_cast<adrastea::Message *>(R_ExternalPtrAddr(xp)); }, FALSE);
                SEXP r6_request = PROTECT(r::newHeraR6("Message", xptr_request));

                // callback
                r::invokeHeraFn(".CommManager__register_target_callback", r6_comm, r6_request);

                UNPROTECT(4);
            };

            adrastea::getInterpreter().getCommManager().registerCommTarget(name, callback);
            return R_NilValue;
        }

        SEXP CommManager__unregisterTarget(SEXP name_)
        {
            std::string name = CHAR(STRING_ELT(name_, 0));

            adrastea::getInterpreter().getCommManager().unregisterCommTarget(name);
            return R_NilValue;
        }

        SEXP CommManager__newComm(SEXP target_name_, SEXP s_description)
        {
            auto target = adrastea::getInterpreter().getCommManager().target(CHAR(STRING_ELT(target_name_, 0)));
            if (target == nullptr)
            {
                return R_NilValue;
            }

            auto id = adrastea::newGuid();
            auto comm = new adrastea::Comm(target, id);
            SEXP xp_comm = PROTECT(R_MakeExternalPtr(
                reinterpret_cast<void *>(comm), R_NilValue, R_NilValue));
            R_RegisterCFinalizerEx(xp_comm, [](SEXP xp)
                                   { delete reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp)); }, FALSE);
            SEXP r6_comm = PROTECT(r::newHeraR6("Comm", xp_comm, s_description));

            UNPROTECT(2);

            return r6_comm;
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

        SEXP Comm__id(SEXP xp_comm)
        {
            auto comm = reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp_comm));
            return Rf_mkString(comm->id().toString().c_str());
        }

        SEXP Comm__targetName(SEXP xp_comm)
        {
            auto comm = reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp_comm));
            return Rf_mkString(comm->target().name().c_str());
        }

        namespace
        {
            adrastea::buffer_sequence toBufferSequence(SEXP r_buffers)
            {
                adrastea::buffer_sequence out;
                if (r_buffers == R_NilValue)
                {
                    return out;
                }
                R_xlen_t n = Rf_xlength(r_buffers);
                out.reserve(n);
                for (R_xlen_t i = 0; i < n; ++i)
                {
                    SEXP raw = VECTOR_ELT(r_buffers, i);
                    R_xlen_t len = Rf_xlength(raw);
                    out.emplace_back(RAW(raw), RAW(raw) + len);
                }
                return out;
            }
        }

        SEXP Comm__open(SEXP xp_comm, SEXP js_metadata, SEXP js_data, SEXP r_buffers)
        {
            auto metadata = adrastea::json::parse(CHAR(STRING_ELT(js_metadata, 0)));
            auto data = adrastea::json::parse(CHAR(STRING_ELT(js_data, 0)));

            auto *comm = reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp_comm));
            comm->open(metadata, data, toBufferSequence(r_buffers));

            return R_NilValue;
        }

        SEXP Comm__close(SEXP xp_comm, SEXP js_metadata, SEXP js_data, SEXP r_buffers)
        {
            auto metadata = adrastea::json::parse(CHAR(STRING_ELT(js_metadata, 0)));
            auto data = adrastea::json::parse(CHAR(STRING_ELT(js_data, 0)));

            auto *comm = reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp_comm));
            comm->close(metadata, data, toBufferSequence(r_buffers));

            return R_NilValue;
        }

        SEXP Comm__send(SEXP xp_comm, SEXP js_metadata, SEXP js_data, SEXP r_buffers)
        {
            auto metadata = adrastea::json::parse(CHAR(STRING_ELT(js_metadata, 0)));
            auto data = adrastea::json::parse(CHAR(STRING_ELT(js_data, 0)));

            auto *comm = reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp_comm));
            comm->send(metadata, data, toBufferSequence(r_buffers));

            return R_NilValue;
        }

        class CommMessageHandler
        {
        public:
            CommMessageHandler(SEXP handler) : m_handler(handler) {}

            inline void operator()(adrastea::Message message)
            {
                auto ptr_message = new adrastea::Message(std::move(message));
                SEXP xptr_message = PROTECT(R_MakeExternalPtr(
                    reinterpret_cast<void *>(ptr_message), R_NilValue, R_NilValue));
                R_RegisterCFinalizerEx(xptr_message, [](SEXP xp)
                                       { delete reinterpret_cast<adrastea::Message *>(R_ExternalPtrAddr(xp)); }, FALSE);

                SEXP call = PROTECT(r::rCall(
                    m_handler,
                    r::newHeraR6("Message", xptr_message)));

                Rf_eval(call, R_GlobalEnv);

                UNPROTECT(2);
            }

        private:
            SEXP m_handler;
        };

        SEXP Comm__onClose(SEXP xp_comm, SEXP handler)
        {
            reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp_comm))->onClose(CommMessageHandler(handler));
            return R_NilValue;
        }

        SEXP Comm__onMessage(SEXP xp_comm, SEXP handler)
        {
            reinterpret_cast<adrastea::Comm *>(R_ExternalPtrAddr(xp_comm))->onMessage(CommMessageHandler(handler));
            return R_NilValue;
        }

        SEXP Message__getContent(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<adrastea::Message *>(R_ExternalPtrAddr(xptr_msg));
            return toRJson(ptr_msg->content());
        }

        SEXP Message__getHeader(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<adrastea::Message *>(R_ExternalPtrAddr(xptr_msg));
            return toRJson(ptr_msg->header());
        }

        SEXP Message__getParentHeader(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<adrastea::Message *>(R_ExternalPtrAddr(xptr_msg));
            return toRJson(ptr_msg->parentHeader());
        }

        SEXP Message__getMetadata(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<adrastea::Message *>(R_ExternalPtrAddr(xptr_msg));
            return toRJson(ptr_msg->metadata());
        }

        SEXP Message__getBuffers(SEXP xptr_msg)
        {
            auto *msg = reinterpret_cast<adrastea::Message *>(R_ExternalPtrAddr(xptr_msg));
            const auto &bufs = msg->buffers();
            SEXP out = PROTECT(Rf_allocVector(VECSXP, bufs.size()));
            for (size_t i = 0; i < bufs.size(); ++i)
            {
                SEXP raw = PROTECT(Rf_allocVector(RAWSXP, bufs[i].size()));
                std::memcpy(RAW(raw), bufs[i].data(), bufs[i].size());
                SET_VECTOR_ELT(out, i, raw);
                UNPROTECT(1);
            }
            UNPROTECT(1);
            return out;
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
            {"CommManager__register_target", (DL_FUNC)&routines::CommManager__registerTarget, 1},
            {"CommManager__unregister_target", (DL_FUNC)&routines::CommManager__unregisterTarget, 1},
            {"CommManager__new_comm", (DL_FUNC)&routines::CommManager__newComm, 2},
            {"CommManager__get_comm_info", (DL_FUNC)&routines::CommManager__getCommInfo, 1},

            // Comm
            {"Comm__id", (DL_FUNC)&routines::Comm__id, 1},
            {"Comm__target_name", (DL_FUNC)&routines::Comm__targetName, 1},
            {"Comm__open", (DL_FUNC)&routines::Comm__open, 4},
            {"Comm__close", (DL_FUNC)&routines::Comm__close, 4},
            {"Comm__send", (DL_FUNC)&routines::Comm__send, 4},
            {"Comm__on_close", (DL_FUNC)&routines::Comm__onClose, 2},
            {"Comm__on_message", (DL_FUNC)&routines::Comm__onMessage, 2},

            // Message aka message
            {"Message__get_content", (DL_FUNC)&routines::Message__getContent, 1},
            {"Message__get_header", (DL_FUNC)&routines::Message__getHeader, 1},
            {"Message__get_parent_header", (DL_FUNC)&routines::Message__getParentHeader, 1},
            {"Message__get_metadata", (DL_FUNC)&routines::Message__getMetadata, 1},
            {"Message__get_buffers", (DL_FUNC)&routines::Message__getBuffers, 1},

            {NULL, NULL, 0}};

        R_registerRoutines(info, NULL, callMethods, NULL, NULL);
    }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

}
