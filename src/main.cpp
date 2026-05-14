#include "Application.hpp"

#ifdef __EMSCRIPTEN__
#include "src/wasm/WASMBindings.hpp"
#endif

#include <iostream>
#include <stdexcept>
#include <cstdlib>

int main() {
    try {
        Application app;
#ifdef __EMSCRIPTEN__
        wasm::setApp(&app);
#endif
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
