#define R_NO_REMAP

#ifdef _MSC_VER
#define _Complex
#endif

#include "R.h"
#include "Rinternals.h"
#include "R_ext/Rdynload.h"

#include "r/rtools.hpp"
#include "datasuite/interpreter_r.hpp"
#include "datasuite/json.hpp"
#include "datasuite/message.hpp"
#include "datasuite/comm.hpp"
#include "datasuite/logger.hpp"

#ifdef _MSC_VER
#undef _Complex
#endif

namespace datasuite
{
    namespace routines
    {
        SEXP to_r_json(const json &js)
        {
            SEXP out = PROTECT(Rf_mkString(js.dump(4).c_str()));
            Rf_classgets(out, Rf_mkString("json"));
            UNPROTECT(1);

            return out;
        }

        SEXP kernel_info_request()
        {
            auto info = get_interpreter().kernel_info_request();
            SEXP out = PROTECT(Rf_mkString(info.dump(4).c_str()));
            Rf_classgets(out, Rf_mkString("json"));
            UNPROTECT(1);
            return out;
        }

        SEXP publish_stream(SEXP name_, SEXP text_)
        {
            auto name = CHAR(STRING_ELT(name_, 0));
            auto text = CHAR(STRING_ELT(text_, 0));

            auto interpreter = get_r_interpreter();
            interpreter->publish_stream(name, text);

            return R_NilValue;
        }

        SEXP display_data(SEXP js_data, SEXP js_metadata)
        {
            auto data = json::parse(CHAR(STRING_ELT(js_data, 0)));
            auto metadata = json::parse(CHAR(STRING_ELT(js_metadata, 0)));

            get_r_interpreter()->display_data(
                std::move(data), std::move(metadata), /* transient = */ json::object());

            return R_NilValue;
        }

        SEXP update_display_data(SEXP js_data, SEXP js_metadata)
        {
            auto data = json::parse(CHAR(STRING_ELT(js_data, 0)));
            auto metadata = json::parse(CHAR(STRING_ELT(js_metadata, 0)));

            get_r_interpreter()->update_display_data(
                std::move(data), std::move(metadata), /* transient = */ json::object());

            return R_NilValue;
        }

        SEXP clear_output(SEXP wait_)
        {
            bool wait = LOGICAL_ELT(wait_, 0) == TRUE;
            get_r_interpreter()->clear_output(wait);
            return R_NilValue;
        }

        SEXP is_complete_request(SEXP code_)
        {
            std::string code = CHAR(STRING_ELT(code_, 0));
            auto is_complete = get_r_interpreter()->is_complete_request(code);

            SEXP out = PROTECT(Rf_mkString(is_complete.dump(4).c_str()));
            Rf_classgets(out, Rf_mkString("json"));
            UNPROTECT(1);
            return out;
        }

        SEXP datasuite_log(SEXP level_, SEXP msg_)
        {
            std::string level = CHAR(STRING_ELT(level_, 0));
            std::string msg = CHAR(STRING_ELT(msg_, 0));

            // TODO: actually do some logging
            return R_NilValue;
        }

        SEXP CommManager__register_target(SEXP name_)
        {
            std::string name = CHAR(STRING_ELT(name_, 0));

            auto callback = [name](comm &&comm, message request)
            {
                // comm
                auto ptr_comm = new datasuite::comm(std::move(comm));
                SEXP xp_comm = PROTECT(R_MakeExternalPtr(
                    reinterpret_cast<void *>(ptr_comm), R_NilValue, R_NilValue));
                R_RegisterCFinalizerEx(xp_comm, [](SEXP xp)
                                       { delete reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp)); }, FALSE);
                SEXP r6_comm = PROTECT(r::new_hera_r6("Comm", xp_comm));

                // request
                auto ptr_request = new message(std::move(request));
                SEXP xptr_request = PROTECT(R_MakeExternalPtr(
                    reinterpret_cast<void *>(ptr_request), R_NilValue, R_NilValue));
                R_RegisterCFinalizerEx(xptr_request, [](SEXP xp)
                                       { delete reinterpret_cast<message *>(R_ExternalPtrAddr(xp)); }, FALSE);
                SEXP r6_request = PROTECT(r::new_hera_r6("Message", xptr_request));

                // callback
                r::invoke_hera_fn(".CommManager__register_target_callback", r6_comm, r6_request);

                UNPROTECT(4);
            };

            get_interpreter().get_comm_manager().register_comm_target(name, callback);
            return R_NilValue;
        }

        SEXP CommManager__unregister_target(SEXP name_)
        {
            std::string name = CHAR(STRING_ELT(name_, 0));

            get_interpreter().get_comm_manager().unregister_comm_target(name);
            return R_NilValue;
        }

        SEXP CommManager__new_comm(SEXP target_name_, SEXP s_description)
        {
            auto target = get_interpreter().get_comm_manager().target(CHAR(STRING_ELT(target_name_, 0)));
            if (target == nullptr)
            {
                return R_NilValue;
            }

            auto id = new_guid();
            auto comm = new datasuite::comm(target, id);
            SEXP xp_comm = PROTECT(R_MakeExternalPtr(
                reinterpret_cast<void *>(comm), R_NilValue, R_NilValue));
            R_RegisterCFinalizerEx(xp_comm, [](SEXP xp)
                                   { delete reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp)); }, FALSE);
            SEXP r6_comm = PROTECT(r::new_hera_r6("Comm", xp_comm, s_description));

            UNPROTECT(2);

            return r6_comm;
        }

        SEXP CommManager__get_comm_info(SEXP target_name_)
        {
            auto comms = get_interpreter().get_comm_manager().comms();

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

                    SET_STRING_ELT(info_names, i, Rf_mkChar(comm_it->first.to_string().c_str()));
                    i++;
                }
            }
            Rf_namesgets(info, info_names);
            UNPROTECT(3);
            return info;
        }

        SEXP Comm__id(SEXP xp_comm)
        {
            auto comm = reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp_comm));
            return Rf_mkString(comm->id().to_string().c_str());
        }

        SEXP Comm__target_name(SEXP xp_comm)
        {
            auto comm = reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp_comm));
            return Rf_mkString(comm->target().name().c_str());
        }

        namespace
        {
            buffer_sequence to_buffer_sequence(SEXP r_buffers)
            {
                buffer_sequence out;
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
            auto metadata = json::parse(CHAR(STRING_ELT(js_metadata, 0)));
            auto data = json::parse(CHAR(STRING_ELT(js_data, 0)));

            auto *comm = reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp_comm));
            comm->open(metadata, data, to_buffer_sequence(r_buffers));

            return R_NilValue;
        }

        SEXP Comm__close(SEXP xp_comm, SEXP js_metadata, SEXP js_data, SEXP r_buffers)
        {
            auto metadata = json::parse(CHAR(STRING_ELT(js_metadata, 0)));
            auto data = json::parse(CHAR(STRING_ELT(js_data, 0)));

            auto *comm = reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp_comm));
            comm->close(metadata, data, to_buffer_sequence(r_buffers));

            return R_NilValue;
        }

        SEXP Comm__send(SEXP xp_comm, SEXP js_metadata, SEXP js_data, SEXP r_buffers)
        {
            auto metadata = json::parse(CHAR(STRING_ELT(js_metadata, 0)));
            auto data = json::parse(CHAR(STRING_ELT(js_data, 0)));

            auto *comm = reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp_comm));
            comm->send(metadata, data, to_buffer_sequence(r_buffers));

            return R_NilValue;
        }

        class Comm_Message_handler
        {
        public:
            Comm_Message_handler(SEXP handler) : m_handler(handler) {}

            inline void operator()(message message)
            {
                auto ptr_message = new datasuite::message(std::move(message));
                SEXP xptr_message = PROTECT(R_MakeExternalPtr(
                    reinterpret_cast<void *>(ptr_message), R_NilValue, R_NilValue));
                R_RegisterCFinalizerEx(xptr_message, [](SEXP xp)
                                       { delete reinterpret_cast<datasuite::message *>(R_ExternalPtrAddr(xp)); }, FALSE);

                SEXP call = PROTECT(r::r_call(
                    m_handler,
                    r::new_hera_r6("Message", xptr_message)));

                Rf_eval(call, R_GlobalEnv);

                UNPROTECT(2);
            }

        private:
            SEXP m_handler;
        };

        SEXP Comm__on_close(SEXP xp_comm, SEXP handler)
        {
            reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp_comm))->on_close(Comm_Message_handler(handler));
            return R_NilValue;
        }

        SEXP Comm__on_message(SEXP xp_comm, SEXP handler)
        {
            reinterpret_cast<datasuite::comm *>(R_ExternalPtrAddr(xp_comm))->on_message(Comm_Message_handler(handler));
            return R_NilValue;
        }

        SEXP Message__get_content(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<message *>(R_ExternalPtrAddr(xptr_msg));
            return to_r_json(ptr_msg->content());
        }

        SEXP Message__get_header(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<message *>(R_ExternalPtrAddr(xptr_msg));
            return to_r_json(ptr_msg->header());
        }

        SEXP Message__get_parent_header(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<message *>(R_ExternalPtrAddr(xptr_msg));
            return to_r_json(ptr_msg->parent_header());
        }

        SEXP Message__get_metadata(SEXP xptr_msg)
        {
            auto ptr_msg = reinterpret_cast<message *>(R_ExternalPtrAddr(xptr_msg));
            return to_r_json(ptr_msg->metadata());
        }

        SEXP Message__get_buffers(SEXP xptr_msg)
        {
            auto *msg = reinterpret_cast<message *>(R_ExternalPtrAddr(xptr_msg));
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
    void register_r_routines()
    {
        DllInfo *info = R_getEmbeddingDllInfo();

        static const R_CallMethodDef callMethods[] = {
            {"datasuite_kernel_info_request", (DL_FUNC)&routines::kernel_info_request, 0},
            {"datasuite_publish_stream", (DL_FUNC)&routines::publish_stream, 2},
            {"datasuite_display_data", (DL_FUNC)&routines::display_data, 2},
            {"datasuite_update_display_data", (DL_FUNC)&routines::update_display_data, 2},
            {"datasuite_clear_output", (DL_FUNC)&routines::clear_output, 1},
            {"datasuite_is_complete_request", (DL_FUNC)&routines::is_complete_request, 1},
            {"datasuite_log", (DL_FUNC)&routines::datasuite_log, 2},

            // CommManager
            {"CommManager__register_target", (DL_FUNC)&routines::CommManager__register_target, 1},
            {"CommManager__unregister_target", (DL_FUNC)&routines::CommManager__unregister_target, 1},
            {"CommManager__new_comm", (DL_FUNC)&routines::CommManager__new_comm, 2},
            {"CommManager__get_comm_info", (DL_FUNC)&routines::CommManager__get_comm_info, 1},

            // Comm
            {"Comm__id", (DL_FUNC)&routines::Comm__id, 1},
            {"Comm__target_name", (DL_FUNC)&routines::Comm__target_name, 1},
            {"Comm__open", (DL_FUNC)&routines::Comm__open, 4},
            {"Comm__close", (DL_FUNC)&routines::Comm__close, 4},
            {"Comm__send", (DL_FUNC)&routines::Comm__send, 4},
            {"Comm__on_close", (DL_FUNC)&routines::Comm__on_close, 2},
            {"Comm__on_message", (DL_FUNC)&routines::Comm__on_message, 2},

            // Message aka message
            {"Message__get_content", (DL_FUNC)&routines::Message__get_content, 1},
            {"Message__get_header", (DL_FUNC)&routines::Message__get_header, 1},
            {"Message__get_parent_header", (DL_FUNC)&routines::Message__get_parent_header, 1},
            {"Message__get_metadata", (DL_FUNC)&routines::Message__get_metadata, 1},
            {"Message__get_buffers", (DL_FUNC)&routines::Message__get_buffers, 1},

            {NULL, NULL, 0}};

        R_registerRoutines(info, NULL, callMethods, NULL, NULL);
    }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

}
