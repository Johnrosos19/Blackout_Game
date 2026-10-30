#pragma once
// ---------------------------------------------------------------------------
// Game.h / Game.cpp - state machine, player, collision, alarm   (owner: R4)
//
//   TITLE   --Enter-->                      PLAYING
//   PLAYING --P-->  PAUSED  --P-->          PLAYING
//   PLAYING --reach exit-->                 WON
//   PLAYING --alarm reaches 1.0-->          LOST
//   PLAYING --R-->                          PLAYING (restart, same level)
//   WON     --R or Enter-->                 PLAYING (next level: bulbs faster)
//   LOST    --R or Enter-->                 PLAYING (same level)
// ---------------------------------------------------------------------------
#include "Scene.h"
#include "LitTest.h"

enum class GameState { Title, Playing, Paused, Won, Lost };

struct Input
{
    float forward = 0.0f, right = 0.0f;       // -1..1
    bool enter = false, restart = false, pause = false;
};

class Game
{
public:
    GameState state = GameState::Title;

    glm::vec3 playerPos{ 0.0f };
    glm::vec2 facing{ 1.0f, 0.0f };

    float alarm = 0.0f;                       // 0..1, 1 = caught
    float illum = 0.0f;                       // what the CPU lit test says at the player's feet
    unsigned int visMask = 0;
    bool  lit = false;
    bool  flashOn = true;

    int   level = 1;
    float speedScale = 1.0f;
    float elapsed = 0.0f;                     // seconds in the current attempt
    float finalTime = 0.0f;

    bool  lineOfSightEnabled = true;          // false = "break it on purpose" (CPU ignores walls)

    void Init(Scene& sc);
    void Reset(Scene& sc);
    void Update(float dt, Scene& sc, const Input& in, float camYaw);

    // re-evaluate the lit test at the player's current position (also used by the probe tools)
    void EvaluateLighting(const Scene& sc);

    glm::vec3 SamplePoint() const { return glm::vec3(playerPos.x, SAMPLE_HEIGHT, playerPos.z); }
};
