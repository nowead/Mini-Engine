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
static void js_setTAAEnabled(bool on)     { if (g_app) g_app->wasm_setTAAEnabled(on); }
static void js_setDebugCascades(bool on)  { if (g_app) g_app->wasm_setDebugCascades(on); }
static void js_setVolumeEnabled(bool on)     { if (g_app) g_app->wasm_setVolumeEnabled(on); }
static void js_setVolumeDensity(float v)     { if (g_app) g_app->wasm_setVolumeDensity(v); }
static void js_setVolumeExtinction(float v)  { if (g_app) g_app->wasm_setVolumeExtinction(v); }
static void js_setVolumeThreshold(float v)   { if (g_app) g_app->wasm_setVolumeThreshold(v); }
static void js_setVolumeColorMix(float v)    { if (g_app) g_app->wasm_setVolumeColorMix(v); }
static void js_setVolumeWinCenter(float v)   { if (g_app) g_app->wasm_setVolumeWinCenter(v); }
static void js_setVolumeWinWidth(float v)    { if (g_app) g_app->wasm_setVolumeWinWidth(v); }
static void js_setVolumePreset(int p)        { if (g_app) g_app->wasm_setVolumePreset(p); }
static void js_setVolumeLowColor(int rgb)  { if (g_app) g_app->wasm_setVolumeLowColor(rgb); }
static void js_setVolumeHighColor(int rgb) { if (g_app) g_app->wasm_setVolumeHighColor(rgb); }
static void js_setSunIntensity(float i)   { if (g_app) g_app->wasm_setSunIntensity(i); }
static void js_setExposure(float e)       { if (g_app) g_app->wasm_setExposure(e); }
static void js_setPointLightCount(int n)  { if (g_app) g_app->wasm_setPointLightCount(n); }
static void js_setABSplitX(float x)        { if (g_app) g_app->wasm_setABSplitX(x); }

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
    emscripten::function("setTAAEnabled",      &wasm::js_setTAAEnabled);
    emscripten::function("setDebugCascades",   &wasm::js_setDebugCascades);
    emscripten::function("setVolumeEnabled",   &wasm::js_setVolumeEnabled);
    emscripten::function("setVolumeDensity",   &wasm::js_setVolumeDensity);
    emscripten::function("setVolumeExtinction",&wasm::js_setVolumeExtinction);
    emscripten::function("setVolumeThreshold", &wasm::js_setVolumeThreshold);
    emscripten::function("setVolumeColorMix",  &wasm::js_setVolumeColorMix);
    emscripten::function("setVolumeWinCenter", &wasm::js_setVolumeWinCenter);
    emscripten::function("setVolumeWinWidth",  &wasm::js_setVolumeWinWidth);
    emscripten::function("setVolumePreset",    &wasm::js_setVolumePreset);
    emscripten::function("setVolumeLowColor",  &wasm::js_setVolumeLowColor);
    emscripten::function("setVolumeHighColor", &wasm::js_setVolumeHighColor);
    emscripten::function("setSunIntensity",    &wasm::js_setSunIntensity);
    emscripten::function("setExposure",        &wasm::js_setExposure);
    emscripten::function("setPointLightCount", &wasm::js_setPointLightCount);
    emscripten::function("setABSplitX",         &wasm::js_setABSplitX);

    emscripten::function("getPassTimeGBuffer",     &wasm::js_getPassTimeGBuffer);
    emscripten::function("getPassTimeDeferred",    &wasm::js_getPassTimeDeferred);
    emscripten::function("getPassTimeSSAO",        &wasm::js_getPassTimeSSAO);
    emscripten::function("getPassTimeBloom",       &wasm::js_getPassTimeBloom);
    emscripten::function("getPassTimePostProcess", &wasm::js_getPassTimePostProcess);
    emscripten::function("getPassTimeTotal",       &wasm::js_getPassTimeTotal);
    emscripten::function("isGPUTimingAvailable",   &wasm::js_isGPUTimingAvailable);
}

#endif // __EMSCRIPTEN__
