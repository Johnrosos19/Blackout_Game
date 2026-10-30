#include "Game.h"
#include <cmath>
#include <algorithm>

static const float PLAYER_SPEED = 4.5f;

void Game::Init(Scene& sc)
{
    flashOn = sc.flashOn;
    level = 1;
    speedScale = 1.0f;
    Reset(sc);
    state = GameState::Title;
}

void Game::Reset(Scene& sc)
{
    playerPos = glm::vec3(sc.playerStart.x, 0.0f, sc.playerStart.y);
    facing = glm::normalize(glm::vec2(sc.exitPos.x - sc.playerStart.x, sc.exitPos.y - sc.playerStart.y));
    alarm = 0.0f;
    elapsed = 0.0f;
    for (auto& b : sc.bulbs) b.Reset();
    EvaluateLighting(sc);
}

void Game::EvaluateLighting(const Scene& sc)
{
    LitResult r = ComputeIllumination(sc, SamplePoint(), lineOfSightEnabled);
    illum = r.illum;
    visMask = r.visMask;
    lit = illum >= sc.litThreshold;
}

// circle (player) against axis-aligned boxes taken from the scene file
static void ResolveCollisions(glm::vec3& p, const Scene& sc)
{
    for (int iter = 0; iter < 3; ++iter)
    {
        for (const auto& o : sc.objects)
        {
            if (!o.solid) continue;
            float cx = glm::clamp(p.x, o.minB.x, o.maxB.x);
            float cz = glm::clamp(p.z, o.minB.z, o.maxB.z);
            float dx = p.x - cx, dz = p.z - cz;
            float d2 = dx * dx + dz * dz;
            if (d2 >= PLAYER_RADIUS * PLAYER_RADIUS) continue;

            if (d2 > 1e-10f)
            {
                float d = std::sqrt(d2);
                float push = (PLAYER_RADIUS - d) / d;
                p.x += dx * push;
                p.z += dz * push;
            }
            else
            {
                // centre is inside the box: leave through the nearest side
                float l = p.x - o.minB.x, r = o.maxB.x - p.x, t = p.z - o.minB.z, b = o.maxB.z - p.z;
                float m = std::min(std::min(l, r), std::min(t, b));
                if (m == l)      p.x = o.minB.x - PLAYER_RADIUS;
                else if (m == r) p.x = o.maxB.x + PLAYER_RADIUS;
                else if (m == t) p.z = o.minB.z - PLAYER_RADIUS;
                else             p.z = o.maxB.z + PLAYER_RADIUS;
            }
        }
    }
    // fail-safe so the player can never leave the level even if the walls are edited away
    p.x = glm::clamp(p.x, sc.bounds.x + PLAYER_RADIUS, sc.bounds.z - PLAYER_RADIUS);
    p.z = glm::clamp(p.z, sc.bounds.y + PLAYER_RADIUS, sc.bounds.w - PLAYER_RADIUS);
}

void Game::Update(float dt, Scene& sc, const Input& in, float camYaw)
{
    switch (state)
    {
    case GameState::Title:
        for (auto& b : sc.bulbs) b.Advance(b.speed * speedScale * dt);    // attract mode
        if (in.enter) { Reset(sc); state = GameState::Playing; }
        return;

    case GameState::Paused:
        if (in.pause) state = GameState::Playing;
        return;

    case GameState::Won:
    case GameState::Lost:
        if (in.restart || in.enter)
        {
            if (state == GameState::Won) { ++level; speedScale *= sc.speedStep; }
            Reset(sc);
            state = GameState::Playing;
        }
        return;

    case GameState::Playing:
        break;
    }

    // ---------------------------- PLAYING ----------------------------
    if (in.restart) { Reset(sc); return; }
    if (in.pause)   { state = GameState::Paused; return; }

    elapsed += dt;
    for (auto& b : sc.bulbs) b.Advance(b.speed * speedScale * dt);

    // movement relative to the camera, scaled by dt (frame-rate independent, B4)
    glm::vec3 camFwd(std::cos(camYaw), 0.0f, std::sin(camYaw));
    glm::vec3 camRight(-std::sin(camYaw), 0.0f, std::cos(camYaw));
    glm::vec3 wish = camFwd * in.forward + camRight * in.right;
    if (glm::length(wish) > 1e-4f)
    {
        wish = glm::normalize(wish);
        playerPos += wish * PLAYER_SPEED * dt;
        glm::vec2 want(wish.x, wish.z);
        glm::vec2 f = glm::mix(facing, want, 0.3f);
        if (glm::length(f) > 1e-4f) facing = glm::normalize(f);
    }
    ResolveCollisions(playerPos, sc);

    // THE MECHANIC: the alarm is driven by the same illumination test the renderer must agree with
    EvaluateLighting(sc);
    float fillScale = 1.0f + 0.15f * (float)(level - 1);
    if (lit) alarm += sc.alarmFill * fillScale * (0.6f + illum) * dt;
    else     alarm -= sc.alarmDrain * dt;
    alarm = glm::clamp(alarm, 0.0f, 1.0f);

    float dx = playerPos.x - sc.exitPos.x, dz = playerPos.z - sc.exitPos.y;
    if (std::sqrt(dx * dx + dz * dz) < sc.exitRadius)
    {
        finalTime = elapsed;
        state = GameState::Won;
    }
    else if (alarm >= 1.0f)
    {
        finalTime = elapsed;
        state = GameState::Lost;
    }
}
