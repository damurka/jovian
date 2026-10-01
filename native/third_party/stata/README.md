# Stata's plugin interface

`stplugin.h` (version 3.0.0) is StataCorp's header for writing Stata plugins, copyright (c) 2003-2015 StataCorp LP,
as published at <https://www.stata.com/plugins/> for plugin authors. It is used unchanged by Callisto's plugin
(`native/src/callisto/plugin/`); `pginit()`, which StataCorp's `stplugin.c` defines, is in the plugin's own source.
