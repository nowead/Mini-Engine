#ifdef __EMSCRIPTEN__
#include "WASMBindings.hpp"
#include "src/Application.hpp"
#include <emscripten/bind.h>

namespace wasm {

static Application* g_app = nullptr;

void setApp(Application* app) { g_app = app; }

static void js_setDebugView(int v)       { if (g_app) g_app->wasm_setDebugView(v); }
static void js_setBloomStrength(float s)  { if (g_app) g_app->wasm_setBloomStrength(s); }
static void js_setAOStrength(float s)     { if (g_app) g_app->wasm_setAOStrength(s); }
static void js_setTonemapEnabled(bool on) { if (g_app) g_app->wasm_setTonemapEnabled(on); }
static void js_setFXAAEnabled(bool on)    { if (g_app) g_app->wasm_setFXAAEnabled(on); }
static void js_setDebugCascades(bool on)  { if (g_app) g_app->wasm_setDebugCascades(on); }
static void js_setSunIntensity(float i)   { if (g_app) g_app->wasm_setSunIntensity(i); }
static void js_setExposure(float e)       { if (g_app) g_app->wasm_setExposure(e); }
static void js_setPointLightCount(int n)  { if (g_app) g_app->wasm_setPointLightCount(n); }

static float js_getPassTimeGBuffer()     { return g_app ? g_app->wasm_getPassTimeGBuffer()     : 0.f; }
static float js_getPassTimeDeferred()    { return g_app ? g_app->wasm_getPassTimeDeferred()    : 0.f; }
static float js_getPassTimeSSAO()        { return g_app ? g_app->wasm_getPassTimeSSAO()        : 0.f; }
static float js_getPassTimeBloom()       { return g_app ? g_app->wasm_getPassTimeBloom()       : 0.f; }
static float js_getPassTimePostProcess() { return g_app ? g_app->wasm_getPassTimePostProcess() : 0.f; }
static float js_getPassTimeTotal()       { return g_app ? g_app->wasm_getPassTimeTotal()       : 0.f; }
static bool  js_isGPUTimingAvailable()   { return g_app ? g_app->wasm_isGPUTimingAvailable()   : false; }

} // namespace wasm

EMSCRIPTEN_BINDINGS(mini_engine) {
    emscripten::function("setDebugView",      &wasm::js_setDebugView);
    emscripten::function("setBloomStrength",   &wasm::js_setBloomStrength);
    emscripten::function("setAOStrength",      &wasm::js_setAOStrength);
    emscripten::function("setTonemapEnabled",  &wasm::js_setTonemapEnabled);
    emscripten::function("setFXAAEnabled",     &wasm::js_setFXAAEnabled);
    emscripten::function("setDebugCascades",   &wasm::js_setDebugCascades);
    emscripten::function("setSunIntensity",    &wasm::js_setSunIntensity);
    emscripten::function("setExposure",        &wasm::js_setExposure);
    emscripten::function("setPointLightCount", &wasm::js_setPointLightCount);

    emscripten::function("getPassTimeGBuffer",     &wasm::js_getPassTimeGBuffer);
    emscripten::function("getPassTimeDeferred",    &wasm::js_getPassTimeDeferred);
    emscripten::function("getPassTimeSSAO",        &wasm::js_getPassTimeSSAO);
    emscripten::function("getPassTimeBloom",       &wasm::js_getPassTimeBloom);
    emscripten::function("getPassTimePostProcess", &wasm::js_getPassTimePostProcess);
    emscripten::function("getPassTimeTotal",       &wasm::js_getPassTimeTotal);
    emscripten::function("isGPUTimingAvailable",   &wasm::js_isGPUTimingAvailable);
}

#endif // __EMSCRIPTEN__
