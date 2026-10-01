// Callisto's Stata plugin (callisto_stata.plugin): reads the values of the dataset for the kernel through Stata's
// plugin interface (stplugin.h), the one way C code reaches Stata's data. The kernel loads it as a Stata program and
// calls it with the variables and the observations it wants and the address of a CallistoSink of its own (Stata runs
// inside the kernel's process):
//
//     program _callisto_plugin, plugin using("<dir>/callisto_stata.plugin")
//     plugin call _callisto_plugin price make in 1/100, "<sink address>"
//
// Each value goes to the sink as it is read: a number, a missing value (which of the 27), or a string's bytes.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "stplugin.h"
#include "callisto/plugin_sink.h"

ST_plugin* _stata_;

// What Stata calls when it loads the plugin (stplugin.c's)
STDLL pginit(ST_plugin* p)
{
    _stata_ = p;
    return SD_PLUGINVER;
}

namespace
{
    // Which missing value: Stata's 27 are 2^1023 times 1 + k/4096, k = 0 for "." and 1 to 26 for ".a" to ".z"
    int missingCode(double value)
    {
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        int code = static_cast<int>((bits >> 40) & 0xfff);
        return code <= 26 ? code : 0;
    }
}

STDLL stata_call(int argc, char* argv[])
{
    if (argc < 1)
    {
        SF_error(const_cast<char*>("callisto plugin: no sink\n"));
        return 198;
    }
    auto* sink = reinterpret_cast<CallistoSink*>(static_cast<std::uintptr_t>(std::strtoull(argv[0], nullptr, 10)));
    if (!sink || sink->version != CALLISTO_SINK_VERSION)
    {
        SF_error(const_cast<char*>("callisto plugin: not called by Callisto\n"));
        return 198;
    }

    const ST_int variables = SF_nvars();
    std::vector<char> text(2048);
    for (ST_int obs = SF_in1(); obs <= SF_in2(); ++obs)
    {
        if (!SF_ifobs(obs)) continue;
        for (ST_int var = 1; var <= variables; ++var)
        {
            if (SF_var_is_string(var))
            {
                ST_int length;
                if (SF_var_is_strl(var))
                {
                    if (SF_var_is_binary(var, obs))
                    {
                        sink->text(sink->context, "<binary>", 8);
                        continue;
                    }
                    length = SF_sdatalen(var, obs);
                    if (length < 0) length = 0;
                    if (static_cast<std::size_t>(length) + 1 > text.size()) text.resize(static_cast<std::size_t>(length) + 1);
                    if (SF_strldata(var, obs, text.data(), length + 1) < 0) length = 0;
                }
                else
                {
                    text[0] = '\0';
                    if (SF_sdata(var, obs, text.data()) != 0) text[0] = '\0';
                    length = static_cast<ST_int>(std::strlen(text.data()));
                }
                sink->text(sink->context, text.data(), static_cast<std::size_t>(length));
                continue;
            }
            ST_double value = 0;
            if (SF_vdata(var, obs, &value) != 0 || SF_is_missing(value))
            {
                sink->missing(sink->context, missingCode(value));
            }
            else
            {
                sink->number(sink->context, value);
            }
        }
        sink->row_end(sink->context);
    }
    return 0;
}
