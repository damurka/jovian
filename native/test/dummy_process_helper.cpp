// Standalone helper spawned by kernel_process_test.cpp -- deliberately not
// datasuite-r.exe, so KernelProcess's spawn/isAlive/kill lifecycle can be
// tested in milliseconds without embedding a real R interpreter.
//
// KernelProcess::start() (native/src/supervisor/kernel_process.cpp) always
// builds its child's command line from a fixed set of datasuite-r-shaped
// flags (--r-home, --registration-port, --key, ...); there's no way to pass
// this helper a custom flag through that code path. Reusing --key as a
// signal instead: "--key quick-exit" means exit immediately (for testing
// isAlive() becoming false after a natural exit), anything else means run
// until killed (for testing isAlive()==true while running and kill()
// actually terminating it).
#include <chrono>
#include <cstring>
#include <thread>

int main(int argc, char** argv)
{
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
