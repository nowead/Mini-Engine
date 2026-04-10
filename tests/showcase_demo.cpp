/**
 * @file showcase_demo.cpp
 * @brief Mini-Engine Portfolio Showcase — Phase 1~4 Unified Demo
 *
 * 세 가지 체감 가능한 순간:
 *   1. Point Light 슬라이더 0→24
 *      - Deferred는 O(lights × pixels), Forward는 O(lights × geometry)
 *      - 슬라이더를 올릴수록 비용 비교 텍스트가 강조됨
 *
 *   2. CSM Cascade 색상 시각화 토글 (Phase 3)
 *      - Red / Green / Blue / Yellow 로 cascade 0~3 경계를 색칠
 *      - "이 줄이 50m 경계입니다"를 눈으로 확인
 *
 *   3. Post-process 개별 On/Off 체크박스 (Phase 1)
 *      - Bloom, SSAO, ACES Tonemap 각각 끄면 즉시 차이가 보임
 *
 * 씬: 8×8 PBR 구체 그리드 (roughness × metallic 그라데이션)
 *     + 24개 궤도 컬러 포인트 라이트
 *
 * 조작: 좌드래그=오비트, 우드래그=팬, 스크롤=줌, Q/Esc=종료
 */

#include "src/rendering/Renderer.hpp"
#include "src/ui/ImGuiManager.hpp"
#include "src/scene/Camera.hpp"
#include "src/scene/Mesh.hpp"
#include "src/rendering/InstancedRenderData.hpp"
#include "src/utils/Vertex.hpp"

#include <imgui.h>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <vulkan/vulkan.h>
#endif

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <vector>
#include <memory>
#include <iostream>
#include <cmath>
#include <random>

// =============================================================================
// 씬 상수
// =============================================================================

static constexpr int   WIN_W         = 1280;
static constexpr int   WIN_H         = 720;
static constexpr int   GRID          = 8;       // GRID × GRID 구체
static constexpr float SPACING       = 2.8f;    // 구체 간격 (m)
static constexpr int   NUM_LIGHTS    = 24;
static constexpr float LIGHT_RADIUS  = 14.0f;
static constexpr float SPHERE_R      = 1.0f;

// =============================================================================
// UV 구체 메시 생성
// =============================================================================

static void buildSphere(float r, int sectors, int stacks,
                        std::vector<Vertex>& verts, std::vector<uint32_t>& idxs)
{
    verts.clear(); idxs.clear();
    for (int i = 0; i <= stacks; ++i) {
        float phi = glm::pi<float>() * i / stacks;   // 0 → π
        for (int j = 0; j <= sectors; ++j) {
            float theta = glm::two_pi<float>() * j / sectors;
            Vertex v;
            v.normal   = { std::sin(phi)*std::cos(theta),
                           std::cos(phi),
                           std::sin(phi)*std::sin(theta) };
            v.pos      = v.normal * r;
            v.texCoord = { float(j)/sectors, float(i)/stacks };
            verts.push_back(v);
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < sectors; ++j) {
            uint32_t a = i*(sectors+1)+j, b = a+sectors+1;
            idxs.insert(idxs.end(), {a, b, a+1, b, b+1, a+1});
        }
    }
}

// =============================================================================
// 포인트 라이트 애니메이션
// =============================================================================

struct AnimLight {
    float orbit, height, speed, phase;
    glm::vec3 color;
    float intensity;
};

static std::vector<AnimLight> makeLights()
{
    const glm::vec3 pal[] = {
        {1.0f,0.25f,0.15f},{0.15f,0.55f,1.0f},{0.2f,1.0f,0.4f},
        {1.0f,0.85f,0.1f},{0.9f,0.15f,1.0f},{0.1f,1.0f,0.9f},
        {1.0f,0.5f,0.1f},{0.5f,0.2f,1.0f},
    };
    float half = GRID * SPACING * 0.5f;
    std::mt19937 rng(42);
    auto fr = [&](float lo, float hi){ return lo+(hi-lo)*(rng()/float(rng.max())); };

    std::vector<AnimLight> out;
    for (int i = 0; i < NUM_LIGHTS; ++i)
        out.push_back({ fr(half*0.3f, half*0.9f), fr(1.5f, 9.0f),
                        fr(0.15f,0.4f)*(i%2?-1.f:1.f), fr(0,glm::two_pi<float>()),
                        pal[i%8], fr(5.f,10.f) });
    return out;
}

// =============================================================================
// ShowcaseDemo
// =============================================================================

class ShowcaseDemo {
public:
    ShowcaseDemo() { initWindow(); initRenderer(); initScene(); }
    ~ShowcaseDemo() {
        if (m_renderer) m_renderer->waitIdle();
        m_objBuf.reset(); m_sphere.reset();
        m_renderer.reset();
        if (m_win) glfwDestroyWindow(m_win);
        glfwTerminate();
    }

    void run() {
        while (!glfwWindowShouldClose(m_win)) {
            glfwPollEvents();
            if (glfwGetKey(m_win, GLFW_KEY_Q)     == GLFW_PRESS ||
                glfwGetKey(m_win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(m_win, GLFW_TRUE);

            double now = glfwGetTime();
            m_dt   = float(now - m_lastT);
            m_lastT = now;
            m_time += m_dt;

            updateCamera();
            updateLights();
            renderFrame();
        }
    }

private:
    // -------------------------------------------------------------------------
    // 초기화
    // -------------------------------------------------------------------------

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_win = glfwCreateWindow(WIN_W, WIN_H,
            "Mini-Engine Showcase — Phase 1~4 | Deferred + CSM + Bindless", nullptr, nullptr);
        glfwSetWindowUserPointer(m_win, this);

        glfwSetFramebufferSizeCallback(m_win, [](GLFWwindow* w,int,int){
            static_cast<ShowcaseDemo*>(glfwGetWindowUserPointer(w))
                ->m_renderer->handleFramebufferResize();
        });
        glfwSetMouseButtonCallback(m_win, [](GLFWwindow* w, int btn, int act, int){
            auto* d = static_cast<ShowcaseDemo*>(glfwGetWindowUserPointer(w));
            if (btn==GLFW_MOUSE_BUTTON_LEFT)  d->m_lDrag = (act==GLFW_PRESS);
            if (btn==GLFW_MOUSE_BUTTON_RIGHT) d->m_rDrag = (act==GLFW_PRESS);
            if (act==GLFW_PRESS) glfwGetCursorPos(w, &d->m_mx, &d->m_my);
        });
        glfwSetCursorPosCallback(m_win, [](GLFWwindow* w, double x, double y){
            auto* d = static_cast<ShowcaseDemo*>(glfwGetWindowUserPointer(w));
            float dx=float(x-d->m_mx), dy=float(y-d->m_my);
            d->m_mx=x; d->m_my=y;
            if (d->m_lDrag) d->m_camera->rotate(dx*0.35f, dy*0.35f);
            if (d->m_rDrag) d->m_camera->translate(dx*0.02f, dy*0.02f);
        });
        glfwSetScrollCallback(m_win, [](GLFWwindow* w, double, double dy){
            static_cast<ShowcaseDemo*>(glfwGetWindowUserPointer(w))
                ->m_camera->zoom(float(-dy)*1.5f);
        });
    }

    void initRenderer() {
        float aspect = float(WIN_W) / float(WIN_H);
        m_camera = std::make_unique<Camera>(aspect);
        m_camera->setDistance(28.0f);

        m_renderer = std::make_unique<Renderer>(m_win,
            std::vector<const char*>{"VK_LAYER_KHRONOS_validation"}, false);
        m_renderer->initImGui(m_win);

        // IBL 로드 시도 (lavapipe에서는 메모리 부족으로 실패 → 야경 컨셉으로 대체)
        for (auto* p : {"textures/ferndale_studio_12_4k.hdr",
                         "textures/rogland_clear_night_4k.hdr"}) {
            if (m_renderer->loadEnvironmentMap(p)) { m_envLoaded=true; break; }
        }

        // 달빛 방향광 — 구체 곡면에 그림자가 잘 보이도록 낮은 각도
        m_renderer->setSunDirection(glm::normalize(glm::vec3(0.4f, 0.6f, 0.3f)));
        m_renderer->setSunIntensity(m_envLoaded ? 1.0f : 2.0f);
        m_renderer->setSunColor(glm::vec3(0.88f, 0.92f, 1.0f));
        m_renderer->setAmbientIntensity(m_envLoaded ? 0.25f : 0.45f);

        m_renderer->setExposure(1.0f);
        m_renderer->setBloomStrength(1.2f);   // 구체 하이라이트에 Bloom이 극적
        m_renderer->setAOStrength(1.0f);
        m_renderer->setShadowBias(1.2f);
        m_renderer->setShadowStrength(0.65f);
        m_renderer->setShadowSceneRadius(GRID * SPACING);
    }

    void initScene() {
        auto* dev = m_renderer->getRHIDevice();
        auto* q   = m_renderer->getGraphicsQueue();

        // UV 구체 (32×16 — 면이 많을수록 Bloom/조명이 부드럽게)
        std::vector<Vertex>   sverts;
        std::vector<uint32_t> sidxs;
        buildSphere(SPHERE_R, 32, 16, sverts, sidxs);
        m_sphere = std::make_unique<Mesh>(dev, q, sverts, sidxs);

        // 8×8 PBR 그리드 — roughness(X축) × metallic(Y축) 변화
        float off = -(GRID-1)*SPACING*0.5f;
        m_objs.resize(GRID*GRID);
        for (int row=0; row<GRID; ++row) {
            float metallic  = float(row) / (GRID-1);          // 0→1
            for (int col=0; col<GRID; ++col) {
                float roughness = float(col) / (GRID-1);      // 0→1
                float cx = off + col*SPACING;
                float cz = off + row*SPACING;
                int idx  = row*GRID+col;

                auto& o = m_objs[idx];
                o.worldMatrix = glm::translate(glm::mat4(1.f), {cx, 0.f, cz});
                o.boundingBoxMin = glm::vec4(cx-SPHERE_R, -SPHERE_R, cz-SPHERE_R, 0);
                o.boundingBoxMax = glm::vec4(cx+SPHERE_R,  SPHERE_R, cz+SPHERE_R, 0);

                // 금속은 철 색(회청), 비금속은 흰도자기 색
                glm::vec3 alb = glm::mix(
                    glm::vec3(0.95f,0.95f,0.95f),   // dielectric: white
                    glm::vec3(0.55f,0.60f,0.65f),   // metallic:   steel
                    metallic);
                o.colorAndMetallic = glm::vec4(alb, metallic);
                o.roughnessAOPad   = glm::vec4(glm::clamp(roughness,0.04f,1.f),
                                               1.0f,
                                               glm::uintBitsToFloat(0xFFFFFFFFu), 0.f);
            }
        }

        // 지면 — 넓은 평면 (구체 그림자가 맺히도록)
        rendering::ObjectData ground{};
        float gs = GRID*SPACING + 8.f;
        ground.worldMatrix = glm::translate(glm::mat4(1.f), {0,-SPHERE_R-0.05f,0})
            * glm::scale(glm::mat4(1.f), {gs, 0.15f, gs});
        ground.boundingBoxMin = {-gs*.5f,-SPHERE_R-.1f,-gs*.5f,0};
        ground.boundingBoxMax = { gs*.5f,-SPHERE_R,     gs*.5f,0};
        ground.colorAndMetallic = {0.55f,0.52f,0.50f, 0.0f};
        ground.roughnessAOPad   = {0.92f,1.f, glm::uintBitsToFloat(0xFFFFFFFFu),0.f};
        m_objs.push_back(ground);

        rhi::BufferDesc bd{};
        bd.size  = sizeof(rendering::ObjectData) * m_objs.size();
        bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        m_objBuf = dev->createBuffer(bd);
        m_objBuf->write(m_objs.data(), bd.size);

        m_lightDescs = makeLights();
    }

    // -------------------------------------------------------------------------
    // 프레임 업데이트
    // -------------------------------------------------------------------------

    void updateCamera() {
        int w,h; glfwGetFramebufferSize(m_win,&w,&h);
        if (h>0) m_camera->setAspectRatio(float(w)/float(h));
        m_renderer->updateCamera(m_camera->getViewMatrix(),
                                 m_camera->getProjectionMatrix(),
                                 m_camera->getPosition());
    }

    void updateLights() {
        std::vector<PointLight> lights;
        for (int i=0; i<m_numLights && i<(int)m_lightDescs.size(); ++i) {
            const auto& d = m_lightDescs[i];
            float a = d.phase + d.speed * m_time;
            PointLight pl{};
            pl.position  = {std::cos(a)*d.orbit, d.height, std::sin(a)*d.orbit};
            pl.radius    = LIGHT_RADIUS;
            pl.color     = d.color;
            pl.intensity = d.intensity;
            lights.push_back(pl);
        }
        m_renderer->setPointLights(lights);
    }

    // -------------------------------------------------------------------------
    // ImGui — 세 개의 인터랙티브 섹션
    // -------------------------------------------------------------------------

    void buildUI() {
        ImGui::SetNextWindowPos({10,10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({330,0}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::Begin("Mini-Engine Showcase", nullptr,
            ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);

        // ── 기술 스택 체크리스트 ──────────────────────────────────────────────
        ImGui::TextColored({0.4f,0.85f,1.f,1.f}, "Phase 1~4 기술 스택");
        ImGui::Separator();
        auto ok = [](const char* s){ ImGui::TextColored({0.3f,1.f,0.35f,1.f},"[v]"); ImGui::SameLine(); ImGui::TextUnformatted(s); };
        ok("Phase 1 : HDR + Bloom + SSAO + FXAA");
        ok("Phase 2 : Render Graph (sync2 배리어 자동)");
        ok("Phase 3 : Deferred G-Buffer + 4-Cascade CSM");
        ok("Phase 4 : VMA 커스텀 풀 + Bindless 텍스처");
        if (!m_envLoaded)
            ImGui::TextColored({1.f,0.75f,0.2f,1.f}," ※ IBL: 야경 모드 (lavapipe)");

        ImGui::Spacing();

        // ── 1. Dynamic Point Lights ──────────────────────────────────────────
        ImGui::TextColored({0.4f,0.85f,1.f,1.f}, "1. Dynamic Point Lights  (Phase 3)");
        ImGui::Separator();

        ImGui::Text("Active lights : %d / %d", m_numLights, NUM_LIGHTS);
        ImGui::SliderInt("##lights", &m_numLights, 0, NUM_LIGHTS);

        // Forward vs Deferred 비용 비교 — 라이트 수에 따라 강조
        int tris = GRID*GRID * 32*16*2;   // 구체당 삼각형 수
        ImGui::Spacing();
        ImGui::TextColored({1.f,0.4f,0.4f,1.f},
            "Forward:  %d lights x ~%dk tri = 비례 비용", m_numLights, tris/1000);
        ImGui::TextColored({0.3f,1.f,0.4f,1.f},
            "Deferred: %d lights x %dx%d px  <- 이 데모", m_numLights, WIN_W, WIN_H);
        if (m_numLights >= 16)
            ImGui::TextColored({1.f,0.9f,0.2f,1.f}, "  Forward라면 이 숫자는 불가합니다!");

        ImGui::Spacing();

        // ── 2. CSM Cascade 시각화 (Phase 3) ─────────────────────────────────
        ImGui::TextColored({0.4f,0.85f,1.f,1.f}, "2. CSM Cascade 시각화  (Phase 3)");
        ImGui::Separator();

        bool dbg = m_renderer->getDebugCascades();
        if (ImGui::Checkbox("Cascade 색상 표시", &dbg))
            m_renderer->setDebugCascades(dbg);

        if (dbg) {
            ImGui::TextColored({1.f,0.3f,0.3f,1.f}, "  Red    = Cascade 0  (0 ~ 10m)");
            ImGui::TextColored({0.3f,1.f,0.3f,1.f}, "  Green  = Cascade 1  (10 ~ 50m)");
            ImGui::TextColored({0.4f,0.5f,1.f,1.f}, "  Blue   = Cascade 2  (50 ~ 200m)");
            ImGui::TextColored({1.f,0.9f,0.2f,1.f}, "  Yellow = Cascade 3  (200~1000m)");
        } else {
            ImGui::TextDisabled("  체크하면 그림자 cascade 경계가 보입니다");
        }

        ImGui::Spacing();

        // ── 3. Post-process 개별 On/Off ──────────────────────────────────────
        ImGui::TextColored({0.4f,0.85f,1.f,1.f}, "3. Post-process 토글  (Phase 1)");
        ImGui::Separator();

        // Bloom
        bool bloomOn = (m_renderer->getBloomStrength() > 0.01f);
        if (ImGui::Checkbox("Bloom (Kawase Dual Blur)", &bloomOn))
            m_renderer->setBloomStrength(bloomOn ? m_savedBloom : 0.0f);
        if (bloomOn) {
            float bs = m_renderer->getBloomStrength();
            if (ImGui::SliderFloat("  강도##bloom", &bs, 0.05f, 3.0f))
            { m_renderer->setBloomStrength(bs); m_savedBloom = bs; }
        }

        // SSAO
        bool aoOn = (m_renderer->getAOStrength() > 0.01f);
        if (ImGui::Checkbox("SSAO (Bilateral Blur)", &aoOn))
            m_renderer->setAOStrength(aoOn ? m_savedAO : 0.0f);
        if (aoOn) {
            float as = m_renderer->getAOStrength();
            if (ImGui::SliderFloat("  강도##ao", &as, 0.05f, 2.0f))
            { m_renderer->setAOStrength(as); m_savedAO = as; }
        }

        // Tonemap
        bool tmOn = m_renderer->getTonemapEnabled();
        if (ImGui::Checkbox("ACES Filmic Tonemap", &tmOn))
            m_renderer->setTonemapEnabled(tmOn);
        if (!tmOn)
            ImGui::TextColored({1.f,0.5f,0.2f,1.f}, "  [!] Raw HDR — 하이라이트 날아감");

        ImGui::Spacing();

        // ── 조명 튜닝 ────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("방향광 / 노출")) {
            float si = m_renderer->getSunIntensity();
            if (ImGui::SliderFloat("태양 강도", &si, 0.f, 4.f)) m_renderer->setSunIntensity(si);
            float ai = m_renderer->getAmbientIntensity();
            if (ImGui::SliderFloat("Ambient",   &ai, 0.f, 1.f)) m_renderer->setAmbientIntensity(ai);
            float ev = m_renderer->getExposure();
            if (ImGui::SliderFloat("Exposure",  &ev, 0.1f,4.f)) m_renderer->setExposure(ev);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("좌드래그: 오비트  우드래그: 팬  스크롤: 줌");
        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // 렌더 프레임
    // -------------------------------------------------------------------------

    void renderFrame() {
        rendering::InstancedRenderData rd{};
        rd.mesh         = m_sphere.get();
        rd.objectBuffer = m_objBuf.get();
        rd.instanceCount = uint32_t(m_objs.size());
        m_renderer->submitInstancedRenderData(rd);

        auto* imgui = m_renderer->getImGuiManager();
        if (imgui) { imgui->newFrame(); buildUI(); }

        m_renderer->drawFrame();
    }

    // -------------------------------------------------------------------------
    // 멤버
    // -------------------------------------------------------------------------

    GLFWwindow*                         m_win     = nullptr;
    std::unique_ptr<Camera>             m_camera;
    std::unique_ptr<Renderer>           m_renderer;

    std::unique_ptr<Mesh>               m_sphere;
    std::unique_ptr<rhi::RHIBuffer>     m_objBuf;
    std::vector<rendering::ObjectData>  m_objs;

    std::vector<AnimLight>              m_lightDescs;
    int                                 m_numLights = 16;   // 기본 16개
    float                               m_time  = 0.f;
    double                              m_lastT = 0.0;
    float                               m_dt    = 0.f;

    bool   m_envLoaded = false;
    float  m_savedBloom = 1.2f;
    float  m_savedAO    = 1.0f;

    bool   m_lDrag = false, m_rDrag = false;
    double m_mx=0, m_my=0;
};

// =============================================================================

int main() {
    try { ShowcaseDemo d; d.run(); }
    catch (const std::exception& e) {
        std::cerr << "[showcase] Fatal: " << e.what() << "\n";
        return 1;
    }
}
