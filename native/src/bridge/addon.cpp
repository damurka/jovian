#include <napi.h>
#include <string>
#include <memory>
#include "datasuite/datasuite_engine.hpp"

using namespace datasuite;

std::unique_ptr<DatasuiteEngine> engine;
Napi::ThreadSafeFunction tsfn;

// ============================================================================
// 1. CREATE ENGINE WITH CONFIGURATION
// ============================================================================
Napi::Value createEngine(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    EnvironmentConfig env_config;
    
    // Parse options if provided
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object opts = info[0].As<Napi::Object>();
        
        if (opts.Has("rHome") && opts.Get("rHome").IsString()) {
            env_config.r_home = opts.Get("rHome").As<Napi::String>().Utf8Value();
            printf("[Addon] Setting R_HOME: %s\n", env_config.r_home.c_str());
            fflush(stdout);
        }
        if (opts.Has("rPath") && opts.Get("rPath").IsString()) {
            env_config.r_path = opts.Get("rPath").As<Napi::String>().Utf8Value();
            printf("[Addon] Setting R_PATH: %s\n", env_config.r_path.c_str());
            fflush(stdout);
        }
        if (opts.Has("rLibs") && opts.Get("rLibs").IsString()) {
            env_config.r_libs = opts.Get("rLibs").As<Napi::String>().Utf8Value();
            printf("[Addon] Setting R_LIBS: %s\n", env_config.r_libs.c_str());
            fflush(stdout);
        }
        if (opts.Has("pandocPath") && opts.Get("pandocPath").IsString()) {
            env_config.pandoc_path = opts.Get("pandocPath").As<Napi::String>().Utf8Value();
            printf("[Addon] Setting pandoc path: %s\n", env_config.pandoc_path.c_str());
            fflush(stdout);
        }
        if (opts.Has("heraSrcPath") && opts.Get("heraSrcPath").IsString()) {
            env_config.hera_src_path = opts.Get("heraSrcPath").As<Napi::String>().Utf8Value();
            printf("[Addon] Setting hera source path: %s\n", env_config.hera_src_path.c_str());
            fflush(stdout);
        }
    }
    
    // Create engine with environment configuration
    engine = std::make_unique<DatasuiteEngine>(env_config);
    printf("[Addon] DatasuiteEngine created with environment config\n");
    fflush(stdout);
    
    return env.Null();
}

// ============================================================================
// 2. Boot Server (Background) & Connect Client (Foreground)
// ============================================================================
Napi::Value start(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!engine) {
        // Create default engine if not already created
        engine = std::make_unique<DatasuiteEngine>();
    }

    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Expected a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Create a ThreadSafeFunction from the JavaScript callback
    tsfn = Napi::ThreadSafeFunction::New(
        env, 
        info[0].As<Napi::Function>(), // The JS callback function from index.ts
        "DatasuiteEngineCallback",    // Resource name for debugging
        0,                            // Unlimited queue size
        1                             // One thread will be writing to this
    );

    // Boot the engine, passing a C++ lambda that triggers the ThreadSafeFunction
    engine->start([](std::string raw_payload) {

        // This inner callback executes safely on the Node.js Main Thread!
        auto js_runner = [](Napi::Env env, Napi::Function jsCallback, std::string* data) {
            // Convert C++ std::string to V8 JS String and execute the JS callback
            // Use New() instead of Utf8Value to handle potentially invalid UTF-8 gracefully
            try {
                // Check if the string is valid UTF-8 before passing to V8
                bool is_valid_utf8 = true;
                for (size_t i = 0; i < data->length(); ++i) {
                    unsigned char c = (*data)[i];
                    if (c >= 0x80) {
                        // Simple check: if it has high bit set, it might be invalid UTF-8
                        // V8 is very strict about UTF-8. If it's invalid, Napi::String::New will throw.
                    }
                }
                
                jsCallback.Call({ Napi::String::New(env, *data) });
            } catch (const Napi::Error& e) {
                // If string conversion fails (e.g. invalid UTF-8), try to pass it as a buffer or fallback string
                printf("[Addon] Warning: Failed to convert payload to JS string: %s\n", e.what());
                
                // Create a safe fallback JSON string
                std::string safe_json = "{\"topic\":\"error\",\"content\":{\"ename\":\"EncodingError\",\"evalue\":\"Failed to decode R output as UTF-8. The output contained invalid characters.\"}}";
                jsCallback.Call({ Napi::String::New(env, safe_json) });
            }
            delete data; // Prevent memory leaks
        };

        // Allocate data on the heap so it survives the thread boundary
        std::string* data_ptr = new std::string(raw_payload);

        // Queue the execution on the Node.js Main Thread
        tsfn.BlockingCall(data_ptr, js_runner);
    });

    return env.Null();
}

// =========================================================================
// 3. EXPORT: addon.init()
// =========================================================================
Napi::Value init(const Napi::CallbackInfo& info) {
    if (!engine) {
        engine = std::make_unique<DatasuiteEngine>();
    }
    // Boots the background C++ Server threads immediately
    engine->init();
    return info.Env().Null();
}

// =========================================================================
// 4. EXPORT: addon.execute(code)
// =========================================================================
Napi::Value execute(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!engine) {
        Napi::Error::New(env, "Engine not initialized. Call createEngine() or init() first.").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected a string of R code").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Convert JS String to C++ std::string
    std::string code = info[0].As<Napi::String>().Utf8Value();

    // Beam it to the Client ZeroMQ Socket, returning the msg_id so the
    // TypeScript layer can correlate later execute_reply/iopub messages.
    std::string msg_id = engine->execute(code);

    return Napi::String::New(env, msg_id);
}

// =========================================================================
// 4. EXPORT: addon.stop()
// =========================================================================
Napi::Value stop(const Napi::CallbackInfo& info) {
    if (!engine) {
        return info.Env().Null();
    }

    // Gracefully shut down the Embedded R Kernel
    engine->stop();

    // Release the ThreadSafeFunction so Node.js is allowed to exit gracefully
    if (tsfn) {
        tsfn.Release();
        tsfn = nullptr;
    }

    return info.Env().Null();
}

// =========================================================================
// 5. N-API MODULE REGISTRATION
// =========================================================================
Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "createEngine"), Napi::Function::New(env, createEngine));
    exports.Set(Napi::String::New(env, "init"), Napi::Function::New(env, init));
    exports.Set(Napi::String::New(env, "start"), Napi::Function::New(env, start));
    exports.Set(Napi::String::New(env, "execute"), Napi::Function::New(env, execute));
    exports.Set(Napi::String::New(env, "stop"), Napi::Function::New(env, stop));
    return exports;
}

NODE_API_MODULE(datasuite_addon, InitModule);
