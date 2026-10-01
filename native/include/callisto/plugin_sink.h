#ifndef CALLISTO_PLUGIN_SINK_H
#define CALLISTO_PLUGIN_SINK_H

/*
 * What Callisto's Stata plugin (callisto_stata.plugin, native/src/callisto/plugin/) hands the values of the dataset
 * to. Stata runs inside Callisto's own process, so the kernel passes the plugin the address of one of these (as a
 * decimal number, the plugin call's argument) and the plugin calls it for each value, without anything being printed
 * or written to a file. Plain C, so the plugin and the kernel need not share a compiler.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CALLISTO_SINK_VERSION 1

typedef struct CallistoSink
{
    int version; /* CALLISTO_SINK_VERSION */
    void* context;
    /* a numeric value that is not missing */
    void (*number)(void* context, double value);
    /* a missing value: 0 for ".", 1 to 26 for ".a" to ".z" */
    void (*missing)(void* context, int code);
    /* a string value (UTF-8 as Stata stores it, not 0-terminated) */
    void (*text)(void* context, const char* bytes, size_t length);
    /* the end of an observation's values */
    void (*row_end)(void* context);
} CallistoSink;

#ifdef __cplusplus
}
#endif

#endif
