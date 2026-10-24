// ============================================================================
// Standalone correctness test for scene::AABB and scene::Frustum.
//
// CPU-only (GLM, no Vulkan / GLFW). Frustum::intersectsAABB drives scene-graph
// culling (SceneGraph::cullFrustum -> BatchRenderer), so a wrong plane sign
// silently drops visible objects. Validates:
//   1. AABB construction, containment, intersection (inclusive edges), merge
//   2. Frustum classification of boxes in front, behind, past far, beside,
//      and straddling each plane -- with and without the Vulkan Y flip
//   3. intersectsAABB / containsAABB agree with testFrustumAABB
//   4. point and sphere tests, translated and rotated cameras
//
// Exit code 0 = all checks passed; non-zero = failure (printed to stderr).
// ============================================================================

#include "src/scene/AABB.hpp"
#include "src/scene/Frustum.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

namespace {

using scene::AABB;
using scene::CullResult;
using scene::Frustum;

int g_failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::printf("  [ok] %s\n", what);
    } else {
        std::fprintf(stderr, "  [FAIL] %s\n", what);
        ++g_failures;
    }
}

constexpr float kFov    = glm::radians(60.0f);
constexpr float kAspect = 16.0f / 9.0f;
constexpr float kNear   = 0.1f;
constexpr float kFar    = 100.0f;

// Same construction as Camera::getProjectionMatrix (flipY = native Vulkan path).
Frustum makeFrustum(const glm::vec3& eye, const glm::vec3& target, bool flipY) {
    glm::mat4 proj = glm::perspective(kFov, kAspect, kNear, kFar);
    if (flipY) proj[1][1] *= -1.0f;
    glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    return Frustum(proj * view);
}

// Half extents of the view volume at distance d in front of the camera.
float halfHeightAt(float d) { return d * std::tan(kFov * 0.5f); }
float halfWidthAt(float d)  { return halfHeightAt(d) * kAspect; }

AABB cube(const glm::vec3& center, float half) {
    return AABB::fromCenterExtents(center, glm::vec3(half));
}

// All three AABB queries must give the expected, mutually consistent answer.
void expectCull(const Frustum& f, const AABB& box, CullResult expected, const char* what) {
    CullResult r = scene::testFrustumAABB(f, box);
    bool consistent = f.intersectsAABB(box) == (r != CullResult::Outside) &&
                      f.containsAABB(box) == (r == CullResult::Inside);
    check(r == expected && consistent, what);
}

// 1. AABB basics.
void testAABB() {
    std::printf("test: AABB\n");
    AABB a = AABB::fromCenterSize(glm::vec3(0.0f), glm::vec3(2.0f));
    check(a.min == glm::vec3(-1.0f) && a.max == glm::vec3(1.0f), "fromCenterSize builds min/max");
    check(a.getCenter() == glm::vec3(0.0f) && a.getHalfExtents() == glm::vec3(1.0f), "center and half extents");
    check(a.contains(glm::vec3(1.0f, 0.0f, -1.0f)), "contains point on the boundary");
    check(!a.contains(glm::vec3(1.01f, 0.0f, 0.0f)), "rejects point just outside");
    check(a.contains(cube(glm::vec3(0.5f), 0.5f)), "contains a box touching its corner");
    check(!a.contains(cube(glm::vec3(0.5f), 0.6f)), "does not contain an overhanging box");

    AABB touching(glm::vec3(1.0f, -1.0f, -1.0f), glm::vec3(3.0f, 1.0f, 1.0f));
    AABB apart(glm::vec3(1.1f, -1.0f, -1.0f), glm::vec3(3.0f, 1.0f, 1.0f));
    check(a.intersects(touching) && touching.intersects(a), "faces touching count as intersecting");
    check(!a.intersects(apart), "separated boxes do not intersect");

    AABB grown = a;
    grown.expand(glm::vec3(5.0f, -3.0f, 0.0f));
    check(grown.min == glm::vec3(-1.0f, -3.0f, -1.0f) && grown.max == glm::vec3(5.0f, 1.0f, 1.0f),
          "expand(point) grows only the needed axes");
    AABB merged = AABB::merge(a, apart);
    check(merged.min == glm::vec3(-1.0f) && merged.max == glm::vec3(3.0f, 1.0f, 1.0f), "merge spans both boxes");
}

// 2 + 3. Frustum classification for both projection conventions.
void testFrustumClassification(bool flipY) {
    std::printf("test: frustum classification (%s)\n", flipY ? "Vulkan Y flip" : "no Y flip");
    Frustum f = makeFrustum(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), flipY);
    const float d = 10.0f;

    expectCull(f, cube({0.0f, 0.0f, -d}, 0.5f), CullResult::Inside, "box in front is inside");
    expectCull(f, cube({0.0f, 0.0f, d}, 0.5f), CullResult::Outside, "box behind the camera is outside");
    expectCull(f, cube({0.0f, 0.0f, -2.0f * kFar}, 0.5f), CullResult::Outside, "box past the far plane is outside");
    expectCull(f, cube({0.0f, 0.0f, -kFar}, 1.0f), CullResult::Intersect, "box on the far plane intersects");
    expectCull(f, cube({0.0f, 0.0f, 0.0f}, 0.5f), CullResult::Intersect, "box around the eye intersects (near plane)");

    expectCull(f, cube({-10.0f * halfWidthAt(d), 0.0f, -d}, 0.5f), CullResult::Outside, "box far left is outside");
    expectCull(f, cube({ 10.0f * halfWidthAt(d), 0.0f, -d}, 0.5f), CullResult::Outside, "box far right is outside");
    expectCull(f, cube({-halfWidthAt(d), 0.0f, -d}, 1.0f), CullResult::Intersect, "box on the left plane intersects");
    expectCull(f, cube({ halfWidthAt(d), 0.0f, -d}, 1.0f), CullResult::Intersect, "box on the right plane intersects");

    expectCull(f, cube({0.0f,  10.0f * halfHeightAt(d), -d}, 0.5f), CullResult::Outside, "box far above is outside");
    expectCull(f, cube({0.0f, -10.0f * halfHeightAt(d), -d}, 0.5f), CullResult::Outside, "box far below is outside");
    expectCull(f, cube({0.0f,  halfHeightAt(d), -d}, 1.0f), CullResult::Intersect, "box on the top plane intersects");
    expectCull(f, cube({0.0f, -halfHeightAt(d), -d}, 1.0f), CullResult::Intersect, "box on the bottom plane intersects");

    // Wider than tall: a box just past the vertical half-height but within the
    // horizontal half-width is outside only if aspect is applied to the right axis.
    float h = halfHeightAt(d);
    expectCull(f, cube({h * 1.5f, 0.0f, -d}, 0.1f), CullResult::Inside, "aspect widens the horizontal extent");
    expectCull(f, cube({0.0f, h * 1.5f, -d}, 0.1f), CullResult::Outside, "vertical extent is not widened");
}

// 4. Point / sphere tests and non-default camera poses.
void testPointsSpheresAndPoses() {
    std::printf("test: points, spheres, camera poses\n");
    Frustum f = makeFrustum(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), true);
    check(f.containsPoint({0.0f, 0.0f, -50.0f}), "point in the middle is inside");
    check(!f.containsPoint({0.0f, 0.0f, -0.05f}), "point between eye and near plane is outside");
    check(!f.containsPoint({0.0f, 0.0f, -kFar - 1.0f}), "point past far is outside");
    check(!f.intersectsSphere({0.0f, 0.0f, 10.0f}, 9.9f), "sphere behind, short of the near plane, is outside");
    check(f.intersectsSphere({0.0f, 0.0f, 10.0f}, 10.5f), "sphere behind, reaching past the near plane, intersects");

    Frustum moved = makeFrustum(glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(100.0f, 0.0f, -1.0f), true);
    expectCull(moved, cube({100.0f, 0.0f, -10.0f}, 0.5f), CullResult::Inside, "translated camera sees box ahead of it");
    expectCull(moved, cube({0.0f, 0.0f, -10.0f}, 0.5f), CullResult::Outside, "translated camera culls box at the origin");

    Frustum turned = makeFrustum(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), true);
    expectCull(turned, cube({10.0f, 0.0f, 0.0f}, 0.5f), CullResult::Inside, "camera facing +X sees box on +X");
    expectCull(turned, cube({0.0f, 0.0f, -10.0f}, 0.5f), CullResult::Outside, "camera facing +X culls box on -Z");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    testAABB();
    testFrustumClassification(true);
    testFrustumClassification(false);
    testPointsSpheresAndPoses();

    if (g_failures == 0) {
        std::printf("\nALL SCENE CULLING TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d SCENE CULLING TEST(S) FAILED\n", g_failures);
    return 1;
}
