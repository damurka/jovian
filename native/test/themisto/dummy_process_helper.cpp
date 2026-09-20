// Standalone helper spawned by kernel_process_test.cpp -- deliberately not
// elara.exe, so KernelProcess's spawn/isAlive/kill lifecycle can be
// tested in milliseconds without embedding a real R interpreter.
//
// KernelProcess::start() (native/src/themisto/kernel_process.cpp) always
// builds its child's command line from a fixed set of elara-shaped
// flags (--r-home, --registration-port, --key, ...); there's no way to pass
// this helper a custom flag through that code path. Reusing --key as a
// signal instead: "--key quick-exit" means exit immediately (for testing
// isAlive() becoming false after a natural exit), anything else means run
// until killed (for testing isAlive()==true while running and kill()
// actually terminating it).
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    // No trailing '\n' on purpose: exercises KernelProcess::startOutputPump()'s
    // (native/src/themisto/kernel_process.cpp) leftover-partial-line flush,
    // which only fires once the pipe closes (this process exiting/being
    // killed) with unflushed data still in `carry` -- real R kernel output
    // always ends in a newline, so that branch had zero coverage before this.
    std::cout << "no-trailing-newline";
    std::cout.flush();

    for (int i = 0; i + 1 < argc; ++i)
    {
        if (std::strcmp(argv[i], "--key") == 0 && std::strcmp(argv[i + 1], "quick-exit") == 0)
        {
            return 0;
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(60));
    return 0;
}
