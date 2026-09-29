#ifndef CALLISTO_ENGINE_HPP
#define CALLISTO_ENGINE_HPP

#include <functional>
#include <string>

#include "adrastea/adrastea.hpp"
#include "adrastea/kernel_configuration.hpp"

namespace callisto
{
    // Stata's equivalent of elara::EnvironmentConfig / carpo::EnvironmentConfig.
    // stata_home is the directory Stata is installed in (the one holding its
    // executable and shared library); stata_edition picks "mp", "se" or "be"
    // when more than one is installed there (empty: the first of those found).
    struct ADRASTEA_API EnvironmentConfig
    {
        std::string stata_home;
        std::string stata_edition;
    };

    // Same shape as carpo::Server: start() runs the kernel on the calling
    // thread until it shuts down.
    class ADRASTEA_API Server
    {
    public:
        Server() = default;
        explicit Server(const EnvironmentConfig& env) : env_config(env) {}

        void start(const adrastea::KernelConfiguration& config, std::function<void()> on_ready = nullptr);

    private:
        void setupEnvironment();

        EnvironmentConfig env_config;
    };
}

#endif // CALLISTO_ENGINE_HPP
