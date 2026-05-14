#pragma once
#ifdef __EMSCRIPTEN__

class Application;

namespace wasm {
    void setApp(Application* app);
}

#endif // __EMSCRIPTEN__
