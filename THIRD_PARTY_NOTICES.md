# Third-party notices

Jovian is released under the MIT licence (`LICENSE`). Parts of it derive from, or ship, work under other licences,
listed here with the notices those licences require.

## Adrastea (`native/src/adrastea`, `native/include/adrastea`)

Adrastea, Jovian's Jupyter transport and kernel core, began as a fork of **xeus** and **xeus-zmq** by QuantStack
and has since been rewritten and extended in place. The parts that remain derived from them are used under the
BSD 3-Clause licence:

```
Copyright (c) 2016, Johan Mabille, Sylvain Corlay, Martin Renou and QuantStack
Copyright (c) 2016, QuantStack
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Stata plugin interface (`native/third_party/stata/stplugin.h`)

StataCorp's C plugin interface header, redistributed as StataCorp publishes it for plugin authors; see the `README.md`
beside it.

## Dependencies built in

The native binaries link libraries from vcpkg (`vcpkg.json`): ZeroMQ and cppzmq (MPL 2.0), nlohmann/json (MIT),
cpp-httplib (MIT), IXWebSocket (BSD 3-Clause), OpenSSL (Apache 2.0), zlib (zlib licence), Brotli (MIT) and
GoogleTest (BSD 3-Clause, tests only). Their licence texts are in each package under `vcpkg_installed/*/share/`.
