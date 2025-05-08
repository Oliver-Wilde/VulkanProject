#ifdef BENCHMARK_MODE
#include "ScenarioRunner.h"
#include "Engine/Scene/Camera.h"
#include "Engine/Voxels/VoxelWorld.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdlib>

namespace {

    /* convert radians → degrees without the experimental GLM header */
    constexpr float RAD2DEG = 57.2957795f;

    /* helper to aim camera at target (keeps roll = 0) */
    inline void lookAt(Camera* c, const glm::vec3& target)
    {
        glm::vec3 dir = glm::normalize(target - c->position);
        c->pitch = std::asin(glm::clamp(dir.y, -1.f, 1.f)) * RAD2DEG;
        c->yaw = std::atan2(dir.z, dir.x) * RAD2DEG;
    }

} // anonymous namespace
/* ───────────────────────────────────────────────────────────────────────── */

ScenarioRunner::ScenarioRunner(Type t,
    uint32_t seed,
    Camera* cam,
    VoxelWorld* world)
    : _type(t)
    , _seed(seed)
    , _cam(cam)
    , _world(world)
{
    std::srand(seed);
}

void ScenarioRunner::update(float dt)
{
    if (!_cam) return;
    _t += dt;

    switch (_type)
    {
    case Type::Static:
        /* leave camera as-is */
        break;

    case Type::Fly:
    {
        /* ─── warm-up + smooth speed ramp ───────────────────────────── */
        constexpr float WARMUP_SEC = 10.f;     // stand still first 5 s
        constexpr float RAMP_SEC = 5.f;     // reach full speed after +5 s
        constexpr float FULL_SPEED = 0.25f;   // rad/s once ramped
        constexpr float ORBIT_RADIUS = 64.f;

        float tMove = std::max(0.f, _t - WARMUP_SEC);              // time since moving
        float speed = FULL_SPEED * glm::clamp(tMove / RAMP_SEC,    // 0→1 ramp
            0.f, 1.f);
        float angle = speed * tMove;                               // θ(t)
        _cam->position = { ORBIT_RADIUS * std::cos(angle),
                           32.f,
                           ORBIT_RADIUS * std::sin(angle) };
        lookAt(_cam, { 0.f, 16.f, 0.f });
    } break;

    case Type::Edit:
    {
        /* every second mark all chunks dirty to trigger rebuilds */
        static float acc = 0.f;  acc += dt;
        if (acc > 1.f && _world) {
            acc = 0.f;
            _world->forceRebuildAllChunks();
        }
    } break;

    case Type::Lod:
    {
        const float d = 16.f + _t * 4.f;      // zoom out over time
        _cam->position = { d, d * 0.4f, d };
        lookAt(_cam, { 0.f, 0.f, 0.f });
    } break;
    }
}
#endif /* BENCHMARK_MODE */
