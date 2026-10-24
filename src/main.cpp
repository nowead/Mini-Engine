#include "Application.hpp"

#ifdef __EMSCRIPTEN__
#include "src/wasm/WASMBindings.hpp"
#endif

#include <rhi/RHIDevice.hpp>

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    // --frames N: render N frames, exit, and fail if the validation layer
    // reported any error. Used by `make smoke` for unattended checks.
    uint32_t maxFrames = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        }
    }

    try {
        Application app;
#ifdef __EMSCRIPTEN__
        wasm::setApp(&app);
#endif
        app.setMaxFrames(maxFrames);
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    if (maxFrames != 0) {
        uint32_t errors = rhi::validationErrorCount();
        std::cout << "Smoke run: " << maxFrames << " frames, "
                  << errors << " validation error(s)" << std::endl;
        if (errors != 0) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
