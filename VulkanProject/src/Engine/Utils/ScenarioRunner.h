#pragma once
#ifdef BENCHMARK_MODE
/* ─────────────────────────────────────────────────────────────────────────
   ScenarioRunner – deterministic camera/world scripts for head-less runs
   ------------------------------------------------------------------------ */
#include <cstdint>

   /* forward decls to avoid including the big headers */
class Camera;
class VoxelWorld;

class ScenarioRunner
{
public:
    enum class Type { Static, Fly, Edit, Lod };

    ScenarioRunner(Type type,
        uint32_t seed,
        Camera* cam,
        VoxelWorld* world);

    /** call once per rendered frame */
    void update(float dt);

private:
    Type        _type;
    uint32_t    _seed;
    Camera* _cam;      /* not owned – live for entire app */
    VoxelWorld* _world;    /* not owned */
    float       _t = 0.f;  /* accumulated time */
};
#endif /* BENCHMARK_MODE */
