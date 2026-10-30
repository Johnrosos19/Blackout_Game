#pragma once
// ---------------------------------------------------------------------------
// Scene.h / Scene.cpp - the level comes from a text file (B11)       (owner: R1)
// See Scenes/level1.txt for the format, and README.md for the one-page spec.
// ---------------------------------------------------------------------------
#include "CommonValues.h"
#include "Mesh.h"
#include "Textures.h"
#include <string>
#include <vector>
#include <memory>

struct Material
{
    std::string name;
    std::string texSpec;
    GLuint tex = 0;
    float specular  = 0.3f;
    float shininess = 16.0f;
    float emissive  = 0.0f;
    glm::vec3 colour{ 1.0f };
    float tile = 2.0f;               // world units per texture repeat
};

struct SceneObject
{
    std::string name;
    int   material = 0;
    glm::vec3 pos{ 0.0f }, size{ 1.0f };
    bool  solid = true;              // blocks the player (collision uses THIS data - no second copy of the level)
    bool  castShadow = true;         // drawn into the shadow maps AND used as a blocker by the CPU lit test
    glm::vec3 minB{ 0.0f }, maxB{ 0.0f };
    std::shared_ptr<Mesh> mesh;
};

struct Bulb
{
    std::string name;
    glm::vec3 colour{ 1.0f };
    float height = 4.0f, intensity = 1.0f, speed = 2.0f;
    std::vector<glm::vec2> path;     // closed loop in (x, z). One point = a static lamp.
    float pathLen = 0.0f;
    float s = 0.0f;                  // distance travelled along the loop
    glm::vec3 position{ 0.0f };

    bool IsStatic() const { return path.size() < 2; }
    void Finalise();
    void Place();
    void Advance(float ds) { s += ds; Place(); }
    void Reset() { s = 0.0f; Place(); }
};

class Scene
{
public:
    // Returns false only when the level cannot be played at all. Malformed lines
    // are skipped, reported with file:line, and collected in 'errors'.
    bool Load(const std::string& path, TextureManager& textures);

    std::string filePath;
    std::vector<std::string> errors;

    int winW = 1280, winH = 720;
    std::string skySpec = "proc:night";
    std::vector<std::string> skyFiles;

    std::vector<Material>    materials;
    std::vector<SceneObject> objects;
    std::vector<Bulb>        bulbs;

    // directional (moon) light
    glm::vec3 dirDirection{ -0.4f, -0.7f, -0.5f };
    glm::vec3 dirColour{ 0.55f, 0.65f, 1.0f };
    float dirAmbient = 0.10f, dirDiffuse = 0.25f;

    // point-light attenuation: att = exponent*d*d + linear*d + constant
    float attC = 1.0f, attL = 0.12f, attE = 0.08f;
    float bulbFar = 25.0f;
    float omniBias = 0.10f;          // world units
    int   omniRes = 1024, dirRes = 2048;

    // flashlight (spot light)
    glm::vec3 flashColour{ 1.0f, 0.95f, 0.8f };
    float flashCutoffDeg = 22.0f, flashIntensity = 1.2f;
    bool  flashOn = true;

    // gameplay
    glm::vec2 playerStart{ 0.0f }, exitPos{ 0.0f };
    float exitRadius = 1.3f;
    glm::vec4 bounds{ -15.0f, -15.0f, 15.0f, 15.0f };      // minX, minZ, maxX, maxZ
    float alarmFill = 0.35f, alarmDrain = 0.25f, litThreshold = 0.15f;
    float speedStep = 1.2f;

private:
    void Err(int line, const std::string& msg);
    bool Floats(const std::vector<std::string>& t, size_t from, size_t count, float* out, int line);
    int  FindMaterial(const std::string& name) const;
};

bool PointInsideSolid(const Scene& sc, float x, float z, float margin);
