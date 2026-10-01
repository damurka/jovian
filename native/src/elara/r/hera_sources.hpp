#ifndef ELARA_R_HERA_SOURCES_HPP
#define ELARA_R_HERA_SOURCES_HPP

// hera's R code, built into the kernel: packages/hera's R/*.R, NAMESPACE and DESCRIPTION, written into a generated
// source file by cmake/EmbedHera.cmake. The kernel loads it at start-up as the namespace 'hera' (see
// RInterpreter's loadHera()), the way Ark carries its own R code: nothing is installed, so a session needs no
// package -- and no Rscript run to install one -- and loads only R's base packages besides.

#include <string>
#include <vector>

namespace elara
{
    namespace hera
    {
        struct SourceFile
        {
            const char *path; // relative to packages/hera: "R/execute.R", "NAMESPACE", "DESCRIPTION"
            std::string text;
        };

        const std::vector<SourceFile> &sourceFiles();
    }
}

#endif // ELARA_R_HERA_SOURCES_HPP
