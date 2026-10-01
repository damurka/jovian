// Comms for R code: the Jupyter comm API hera exports (CommManager, Comm, received messages), held here in C++.
//
// A comm, to R, is its id with class "Comm" (methods through hera's `$.Comm`); a received message an R list with
// class "Message" (content, header, parent_header, metadata, buffers). The comms, their targets and the R functions
// they call are kept here -- the R functions preserved from R's garbage collector for as long as they are in use --
// and every R function is run with R_tryEval: an error in the user's code is logged, never lets R long-jump through
// this code.

#include "elara/r/comm_r.hpp"

#include "elara/r/r_dynlib.hpp"
#include "elara/r/rtools.hpp"
#include "elara/r/json_convert.hpp"
#include "elara/log.hpp"
#include "adrastea/comm.hpp"
#include "adrastea/interpreter.hpp"
#include "adrastea/message.hpp"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace elara
{
    namespace comms
    {
        namespace
        {
            // An R function kept alive while it is in use
            class Preserved
            {
            public:
                Preserved() = default;
                explicit Preserved(SEXP value) { set(value); }
                ~Preserved() { set(R_NilValue); }
                Preserved(const Preserved &) = delete;
                Preserved &operator=(const Preserved &) = delete;

                void set(SEXP value)
                {
                    if (m_value && m_value != R_NilValue) R_ReleaseObject(m_value);
                    m_value = value;
                    if (m_value && m_value != R_NilValue) R_PreserveObject(m_value);
                }
                SEXP get() const { return m_value ? m_value : R_NilValue; }
                bool empty() const { return !m_value || m_value == R_NilValue; }

            private:
                SEXP m_value = nullptr;
            };

            struct Entry
            {
                std::unique_ptr<adrastea::Comm> comm;
                std::string description;
                Preserved onMessage;
                Preserved onClose;
            };

            std::map<std::string, std::unique_ptr<Entry>> &openComms()
            {
                static std::map<std::string, std::unique_ptr<Entry>> comms;
                return comms;
            }

            // comms closed by the frontend: destroyed once the close has been handled (not inside their own handler)
            std::vector<std::unique_ptr<Entry>> &closedComms()
            {
                static std::vector<std::unique_ptr<Entry>> closed;
                return closed;
            }

            std::map<std::string, std::unique_ptr<Preserved>> &targetCallbacks()
            {
                static std::map<std::string, std::unique_ptr<Preserved>> callbacks;
                return callbacks;
            }

            void reapClosed()
            {
                closedComms().clear();
            }

            std::string text(SEXP x)
            {
                return TYPEOF(x) == STRSXP && XLENGTH(x) > 0 && STRING_ELT(x, 0) != NA_STRING
                    ? std::string(Rf_translateCharUTF8(STRING_ELT(x, 0)))
                    : std::string();
            }

            SEXP mkStringUtf8(const std::string &s)
            {
                SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
                SET_STRING_ELT(out, 0, Rf_mkCharLenCE(s.data(), static_cast<int>(s.size()), CE_UTF8));
                UNPROTECT(1);
                return out;
            }

            // A comm as R sees it: its id, of class "Comm"
            SEXP commObject(const std::string &id)
            {
                SEXP out = PROTECT(mkStringUtf8(id));
                Rf_classgets(out, Rf_mkString("Comm"));
                UNPROTECT(1);
                return out;
            }

            // A received message as R sees it
            SEXP messageObject(const adrastea::Message &message)
            {
                const char *names[] = {"content", "header", "parent_header", "metadata", "buffers"};
                SEXP out = PROTECT(Rf_allocVector(VECSXP, 5));
                SET_VECTOR_ELT(out, 0, routines::jsonToSexp(message.content()));
                SET_VECTOR_ELT(out, 1, routines::jsonToSexp(message.header()));
                SET_VECTOR_ELT(out, 2, routines::jsonToSexp(message.parentHeader()));
                SET_VECTOR_ELT(out, 3, routines::jsonToSexp(message.metadata()));
                const auto &buffers = message.buffers();
                SEXP rawBuffers = PROTECT(Rf_allocVector(VECSXP, static_cast<R_xlen_t>(buffers.size())));
                for (size_t i = 0; i < buffers.size(); ++i)
                {
                    SEXP raw = PROTECT(Rf_allocVector(RAWSXP, static_cast<R_xlen_t>(buffers[i].size())));
                    if (!buffers[i].empty()) std::memcpy(RAW(raw), buffers[i].data(), buffers[i].size());
                    SET_VECTOR_ELT(rawBuffers, static_cast<R_xlen_t>(i), raw);
                    UNPROTECT(1);
                }
                SET_VECTOR_ELT(out, 4, rawBuffers);
                UNPROTECT(1);
                SEXP outNames = PROTECT(Rf_allocVector(STRSXP, 5));
                for (int i = 0; i < 5; ++i) SET_STRING_ELT(outNames, i, Rf_mkChar(names[i]));
                Rf_namesgets(out, outNames);
                Rf_classgets(out, Rf_mkString("Message"));
                UNPROTECT(2);
                return out;
            }

            // Runs an R function with the given arguments, logging an error in it
            void call(SEXP fn, SEXP args, const std::string &what)
            {
                if (fn == R_NilValue) return;
                SEXP expr = PROTECT(Rf_lcons(fn, args));
                int error = 0;
                R_tryEval(expr, R_GlobalEnv, &error);
                UNPROTECT(1);
                if (error)
                {
                    log::error(what + " failed: " + R_curErrorBuf());
                }
            }

            Entry *find(SEXP id_)
            {
                auto it = openComms().find(text(id_));
                return it == openComms().end() ? nullptr : it->second.get();
            }

            void onMessage(const std::string &id, adrastea::Message message)
            {
                auto it = openComms().find(id);
                if (it == openComms().end() || it->second->onMessage.empty()) return;
                SEXP args = PROTECT(Rf_cons(messageObject(message), R_NilValue));
                call(it->second->onMessage.get(), args, "a comm message handler");
                UNPROTECT(1);
            }

            void onClose(const std::string &id, adrastea::Message message)
            {
                auto it = openComms().find(id);
                if (it == openComms().end()) return;
                std::unique_ptr<Entry> entry = std::move(it->second);
                openComms().erase(it);
                if (!entry->onClose.empty())
                {
                    SEXP args = PROTECT(Rf_cons(messageObject(message), R_NilValue));
                    call(entry->onClose.get(), args, "a comm close handler");
                    UNPROTECT(1);
                }
                entry->onMessage.set(R_NilValue);
                entry->onClose.set(R_NilValue);
                // destroyed later: this runs inside the comm's own handleClose()
                closedComms().push_back(std::move(entry));
            }

            // Keeps a comm, with the handlers that route its messages here; returns its id
            std::string keep(std::unique_ptr<adrastea::Comm> comm, std::string description)
            {
                std::string id = comm->id().toString();
                comm->onMessage([id](adrastea::Message message) { onMessage(id, std::move(message)); });
                comm->onClose([id](adrastea::Message message) { onClose(id, std::move(message)); });
                auto entry = std::make_unique<Entry>();
                entry->comm = std::move(comm);
                entry->description = std::move(description);
                openComms()[id] = std::move(entry);
                return id;
            }

            adrastea::buffer_sequence buffersOf(SEXP buffers)
            {
                adrastea::buffer_sequence out;
                if (TYPEOF(buffers) != VECSXP) return out;
                for (R_xlen_t i = 0; i < XLENGTH(buffers); ++i)
                {
                    SEXP raw = VECTOR_ELT(buffers, i);
                    if (TYPEOF(raw) != RAWSXP) continue;
                    out.emplace_back(RAW(raw), RAW(raw) + XLENGTH(raw));
                }
                return out;
            }

            // A comm message's metadata ({} when NULL) and data (null when NULL), as JSON
            adrastea::json metadataOf(SEXP metadata)
            {
                return metadata == R_NilValue ? adrastea::json::object() : routines::sexpToJson(metadata, false);
            }

            adrastea::json dataOf(SEXP data)
            {
                return routines::sexpToJson(data, false);
            }
        }

        // CommManager$register_comm_target(target_name, callback): `callback(comm, message)` when the frontend
        // opens a comm to the target
        SEXP registerTarget(SEXP target_, SEXP callback)
        {
            reapClosed();
            std::string target = text(target_);
            targetCallbacks()[target] = std::make_unique<Preserved>(callback);
            adrastea::getInterpreter().getCommManager().registerCommTarget(target,
                [target](adrastea::Comm &&comm, adrastea::Message request)
                {
                    std::string id = keep(std::make_unique<adrastea::Comm>(std::move(comm)), "");
                    auto it = targetCallbacks().find(target);
                    if (it == targetCallbacks().end()) return;
                    SEXP args = PROTECT(Rf_cons(commObject(id), Rf_cons(messageObject(request), R_NilValue)));
                    call(it->second->get(), args, "the callback of comm target '" + target + "'");
                    UNPROTECT(1);
                });
            return R_NilValue;
        }

        SEXP unregisterTarget(SEXP target_)
        {
            reapClosed();
            std::string target = text(target_);
            adrastea::getInterpreter().getCommManager().unregisterCommTarget(target);
            targetCallbacks().erase(target);
            return R_NilValue;
        }

        SEXP targetCallback(SEXP target_)
        {
            auto it = targetCallbacks().find(text(target_));
            return it == targetCallbacks().end() ? R_NilValue : it->second->get();
        }

        // CommManager$new_comm(target_name, description): a comm to the target the kernel opens (comm$open()); NULL
        // when the target isn't registered
        SEXP newComm(SEXP target_, SEXP description_)
        {
            reapClosed();
            auto target = adrastea::getInterpreter().getCommManager().target(text(target_));
            if (target == nullptr) return R_NilValue;
            std::string id = keep(std::make_unique<adrastea::Comm>(target), text(description_));
            return commObject(id);
        }

        // CommManager$comms(): the comms open, by id
        SEXP list()
        {
            reapClosed();
            auto &comms = openComms();
            SEXP out = PROTECT(Rf_allocVector(VECSXP, static_cast<R_xlen_t>(comms.size())));
            SEXP outNames = PROTECT(Rf_allocVector(STRSXP, static_cast<R_xlen_t>(comms.size())));
            R_xlen_t i = 0;
            for (const auto &item : comms)
            {
                SET_VECTOR_ELT(out, i, commObject(item.first));
                SET_STRING_ELT(outNames, i, Rf_mkCharLenCE(item.first.data(), static_cast<int>(item.first.size()), CE_UTF8));
                ++i;
            }
            Rf_namesgets(out, outNames);
            UNPROTECT(2);
            return out;
        }

        SEXP targetName(SEXP id_)
        {
            Entry *entry = find(id_);
            return entry ? mkStringUtf8(entry->comm->target().name()) : R_NilValue;
        }

        SEXP description(SEXP id_)
        {
            Entry *entry = find(id_);
            return entry ? mkStringUtf8(entry->description) : R_NilValue;
        }

        // comm$open(), $send(), $close(): FALSE for a comm that is closed
        SEXP open(SEXP id_, SEXP data, SEXP metadata, SEXP buffers)
        {
            Entry *entry = find(id_);
            if (!entry) return Rf_ScalarLogical(0);
            entry->comm->open(metadataOf(metadata), dataOf(data), buffersOf(buffers));
            return Rf_ScalarLogical(1);
        }

        SEXP send(SEXP id_, SEXP data, SEXP metadata, SEXP buffers)
        {
            Entry *entry = find(id_);
            if (!entry) return Rf_ScalarLogical(0);
            entry->comm->send(metadataOf(metadata), dataOf(data), buffersOf(buffers));
            return Rf_ScalarLogical(1);
        }

        SEXP close(SEXP id_, SEXP data, SEXP metadata, SEXP buffers)
        {
            reapClosed();
            auto it = openComms().find(text(id_));
            if (it == openComms().end()) return Rf_ScalarLogical(0);
            it->second->comm->close(metadataOf(metadata), dataOf(data), buffersOf(buffers));
            openComms().erase(it);
            return Rf_ScalarLogical(1);
        }

        // comm$on_message(handler), comm$on_close(handler): `handler(message)`
        SEXP setOnMessage(SEXP id_, SEXP handler)
        {
            Entry *entry = find(id_);
            if (!entry) return Rf_ScalarLogical(0);
            entry->onMessage.set(handler);
            return Rf_ScalarLogical(1);
        }

        SEXP setOnClose(SEXP id_, SEXP handler)
        {
            Entry *entry = find(id_);
            if (!entry) return Rf_ScalarLogical(0);
            entry->onClose.set(handler);
            return Rf_ScalarLogical(1);
        }
    }
}
