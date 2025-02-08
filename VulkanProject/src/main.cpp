#include "Engine/Core/Application.h"
#include <iostream>

int main()
{
    try {
        // Create the application
        Application app;
        // Initialize
        app.init();
        // Run main loop
        app.runLoop();
        // Cleanup
        app.cleanup();
    }
    catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}