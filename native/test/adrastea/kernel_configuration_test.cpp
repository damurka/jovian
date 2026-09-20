// Coverage for KernelConfiguration::loadConfiguration()
// (native/src/adrastea/core/kernel/kernel_configuration.cpp) -- parses a Jupyter-
// style connection file into either a KernelConfiguration or a
// RegistrationConfiguration depending on whether "registration_ip" is
// present. Nothing in this codebase calls it today (confirmed by grep when
// this test was added -- native/src/elara/elara.cpp and the supervisor
// build their configs in-memory instead), but the function is still part
// of the public API surface (native/include/adrastea/kernel_configuration.hpp)
// and had zero coverage validating it actually parses what its own doc
// comment says it should.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <variant>

#include <gtest/gtest.h>

#include "adrastea/kernel_configuration.hpp"

using namespace adrastea;

namespace
{
    class TempConnectionFile
    {
    public:
        explicit TempConnectionFile(const std::string& contents)
        {
            m_path = std::filesystem::temp_directory_path()
                / ("adrastea_test_connection_" + std::to_string(std::rand()) + ".json");
            std::ofstream ofs(m_path);
            ofs << contents;
        }

        ~TempConnectionFile()
        {
            std::error_code ec;
            std::filesystem::remove(m_path, ec);
        }

        std::string path() const { return m_path.string(); }

    private:
        std::filesystem::path m_path;
    };
}

TEST(KernelConfigurationTest, LoadsAKernelConfigurationWhenNoRegistrationIp)
{
    TempConnectionFile file(R"({
        "transport": "tcp",
        "ip": "127.0.0.1",
        "signature_scheme": "hmac-sha256",
        "key": "shared-secret",
        "control_port": 10001,
        "shell_port": 10002,
        "stdin_port": 10003,
        "iopub_port": 10004,
        "hb_port": 10005
    })");

    configuration config = loadConfiguration(file.path());

    ASSERT_TRUE(std::holds_alternative<KernelConfiguration>(config));
    const auto& kernelConfig = std::get<KernelConfiguration>(config);
    EXPECT_EQ(kernelConfig.m_transport, "tcp");
    EXPECT_EQ(kernelConfig.m_ip, "127.0.0.1");
    EXPECT_EQ(kernelConfig.m_signatureScheme, "hmac-sha256");
    EXPECT_EQ(kernelConfig.m_key, "shared-secret");
    EXPECT_EQ(kernelConfig.m_controlPort, "10001");
    EXPECT_EQ(kernelConfig.m_shellPort, "10002");
    EXPECT_EQ(kernelConfig.m_stdinPort, "10003");
    EXPECT_EQ(kernelConfig.m_iopubPort, "10004");
    EXPECT_EQ(kernelConfig.m_hbPort, "10005");
}

TEST(KernelConfigurationTest, LoadsARegistrationConfigurationWhenRegistrationIpPresent)
{
    TempConnectionFile file(R"({
        "transport": "tcp",
        "ip": "127.0.0.1",
        "signature_scheme": "hmac-sha256",
        "key": "shared-secret",
        "kernel_id": "kernel-42",
        "registration_ip": "127.0.0.1",
        "registration_port": "20000"
    })");

    configuration config = loadConfiguration(file.path());

    ASSERT_TRUE(std::holds_alternative<RegistrationConfiguration>(config));
    const auto& regConfig = std::get<RegistrationConfiguration>(config);
    EXPECT_EQ(regConfig.m_kernelId, "kernel-42");
    EXPECT_EQ(regConfig.m_registrationIp, "127.0.0.1");
    EXPECT_EQ(regConfig.m_registrationPort, "20000");
    EXPECT_EQ(regConfig.m_key, "shared-secret");
}

TEST(KernelConfigurationTest, MissingSignatureSchemeMeansEmptyKey)
{
    // loadCommonConfiguration() only reads "key" when "signature_scheme" is
    // non-empty -- an unauthenticated/unset scheme should leave m_key empty
    // rather than erroring on a missing "key" field.
    TempConnectionFile file(R"({
        "transport": "tcp",
        "ip": "127.0.0.1",
        "control_port": 1,
        "shell_port": 2,
        "stdin_port": 3,
        "iopub_port": 4,
        "hb_port": 5
    })");

    configuration config = loadConfiguration(file.path());

    ASSERT_TRUE(std::holds_alternative<KernelConfiguration>(config));
    EXPECT_EQ(std::get<KernelConfiguration>(config).m_key, "");
}
