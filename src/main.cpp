#include "Application.h"
#include <iostream>

int main() {
    try {
        Application app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "An unknown exception occurred." << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
