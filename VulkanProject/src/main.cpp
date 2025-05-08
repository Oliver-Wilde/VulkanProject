#include "Engine/Core/Application.h"
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        Application app;

#ifdef BENCHMARK_MODE
        /* Parse --scenario / --seed / --seconds before init() */
        app.parseCommandLine(argc, argv);
#endif

        app.init();
        app.runLoop();     // runLoop calls cleanup() internally on exit
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
