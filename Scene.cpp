#include "Scene.h"
#include "Paths.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ----------------------------------------------------------------------------
// Bulb
// ----------------------------------------------------------------------------
void Bulb::Finalise()
{
    pathLen = 0.0f;
    if (path.size() >= 2)
        for (size_t i = 0; i < path.size(); ++i)
            pathLen += glm::length(path[(i + 1) % path.size()] - path[i]);
    s = 0.0f;
    Place();
}

void Bulb::Place()
{
    if (path.empty()) return;
    if (IsStatic() || pathLen <= 0.0f)
    {
        position = glm::vec3(path[0].x, height, path[0].y);
        return;
    }
    float t = std::fmod(s, pathLen);
    if (t < 0.0f) t += pathLen;
    size_t n = path.size();
    for (size_t i = 0; i < n; ++i)
    {
        glm::vec2 a = path[i], b = path[(i + 1) % n];
        float len = glm::length(b - a);
        if (t <= len || i == n - 1)
        {
            float f = (len > 0.0f) ? glm::clamp(t / len, 0.0f, 1.0f) : 0.0f;
            glm::vec2 p = a + (b - a) * f;
            position = glm::vec3(p.x, height, p.y);
            return;
        }
        t -= len;
    }
}

// ----------------------------------------------------------------------------
// Loader helpers
// ----------------------------------------------------------------------------
void Scene::Err(int line, const std::string& msg)
{
    std::ostringstream ss;
    ss << filePath << ":" << line << ": " << msg;
    errors.push_back(ss.str());
    std::cerr << "[SCENE] " << ss.str() << "\n";
}

bool Scene::Floats(const std::vector<std::string>& t, size_t from, size_t count, float* out, int line)
{
    for (size_t i = 0; i < count; ++i)
    {
        const std::string& s = t[from + i];
        char* end = nullptr;
        float v = std::strtof(s.c_str(), &end);
        if (s.empty() || end == s.c_str() || *end != '\0')
        {
            Err(line, "'" + t[0] + "': argument " + std::to_string(from + i) + " should be a number but is '" + s + "'");
            return false;
        }
        out[i] = v;
    }
    return true;
}

int Scene::FindMaterial(const std::string& name) const
{
    for (size_t i = 0; i < materials.size(); ++i)
        if (materials[i].name == name) return (int)i;
    return -1;
}

bool PointInsideSolid(const Scene& sc, float x, float z, float margin)
{
    for (const auto& o : sc.objects)
    {
        if (!o.solid) continue;
        if (x > o.minB.x - margin && x < o.maxB.x + margin && z > o.minB.z - margin && z < o.maxB.z + margin)
            return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Load
// ----------------------------------------------------------------------------
bool Scene::Load(const std::string& path, TextureManager& tm)
{
    filePath = ResolveAsset(path);
    std::ifstream f(filePath);
    if (!f)
    {
        Err(0, "cannot open scene file '" + path + "'");
        return false;
    }

    // material 0 is always present so a bad material name is survivable
    Material def;
    def.name = "default"; def.texSpec = "proc:concrete"; def.tex = tm.Get(def.texSpec);
    materials.push_back(def);

    bool havePlayer = false, haveExit = false;
    std::string line;
    int ln = 0;
    while (std::getline(f, line))
    {
        ++ln;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ss(line);
        std::vector<std::string> t;
        std::string tok;
        while (ss >> tok) t.push_back(tok);
        if (t.empty()) continue;

        const std::string& cmd = t[0];
        float v[10];
        auto need = [&](size_t count, const char* usage) -> bool {
            if (t.size() != count)
            {
                Err(ln, "'" + cmd + "' expects " + std::to_string(count - 1) + " argument(s), found " +
                        std::to_string(t.size() - 1) + ".  Usage: " + usage);
                return false;
            }
            return true;
        };

        if (cmd == "window")
        {
            if (!need(3, "window <width> <height>") || !Floats(t, 1, 2, v, ln)) continue;
            if (v[0] < 320 || v[1] < 240) { Err(ln, "window size must be at least 320x240"); continue; }
            winW = (int)v[0]; winH = (int)v[1];
        }
        else if (cmd == "sky")
        {
            if (t.size() == 2) { skySpec = t[1]; skyFiles.clear(); }
            else if (t.size() == 7) { skyFiles.assign(t.begin() + 1, t.end()); skySpec = "files"; }
            else Err(ln, "'sky' expects either 'sky proc:night' or six image paths (+X -X +Y -Y +Z -Z)");
        }
        else if (cmd == "dirlight")
        {
            if (!need(9, "dirlight <dx> <dy> <dz> <r> <g> <b> <ambient> <diffuse>") || !Floats(t, 1, 8, v, ln)) continue;
            glm::vec3 d(v[0], v[1], v[2]);
            if (glm::length(d) < 1e-4f) { Err(ln, "dirlight direction cannot be (0,0,0)"); continue; }
            dirDirection = d; dirColour = glm::vec3(v[3], v[4], v[5]); dirAmbient = v[6]; dirDiffuse = v[7];
        }
        else if (cmd == "attenuation")
        {
            if (!need(4, "attenuation <constant> <linear> <exponent>") || !Floats(t, 1, 3, v, ln)) continue;
            if (v[0] <= 0.0f) { Err(ln, "attenuation constant must be > 0 (it is the divisor at d = 0)"); continue; }
            attC = v[0]; attL = v[1]; attE = v[2];
        }
        else if (cmd == "bulbrange")
        {
            if (!need(2, "bulbrange <farPlane>") || !Floats(t, 1, 1, v, ln)) continue;
            if (v[0] < 2.0f) { Err(ln, "bulbrange must be at least 2"); continue; }
            bulbFar = v[0];
        }
        else if (cmd == "omnibias")
        {
            if (!need(2, "omnibias <worldUnits>") || !Floats(t, 1, 1, v, ln)) continue;
            omniBias = v[0];
        }
        else if (cmd == "shadowres")
        {
            if (!need(3, "shadowres <omniResolution> <directionalResolution>") || !Floats(t, 1, 2, v, ln)) continue;
            if (v[0] < 64 || v[0] > 4096 || v[1] < 64 || v[1] > 8192) { Err(ln, "shadow resolutions out of range (64..4096 / 64..8192)"); continue; }
            omniRes = (int)v[0]; dirRes = (int)v[1];
        }
        else if (cmd == "alarm")
        {
            if (!need(4, "alarm <fillPerSecond> <drainPerSecond> <litThreshold>") || !Floats(t, 1, 3, v, ln)) continue;
            alarmFill = v[0]; alarmDrain = v[1]; litThreshold = v[2];
        }
        else if (cmd == "speedstep")
        {
            if (!need(2, "speedstep <multiplierPerLevel>") || !Floats(t, 1, 1, v, ln)) continue;
            speedStep = v[0];
        }
        else if (cmd == "flashlight")
        {
            if (!need(7, "flashlight <r> <g> <b> <cutoffDegrees> <intensity> <on 0/1>") || !Floats(t, 1, 6, v, ln)) continue;
            if (v[3] < 1.0f || v[3] > 80.0f) { Err(ln, "flashlight cutoff must be between 1 and 80 degrees"); continue; }
            flashColour = glm::vec3(v[0], v[1], v[2]); flashCutoffDeg = v[3]; flashIntensity = v[4]; flashOn = v[5] != 0.0f;
        }
        else if (cmd == "material")
        {
            if (!need(10, "material <name> <texture> <specular> <shininess> <r> <g> <b> <emissive> <tileSize>")) continue;
            if (!Floats(t, 3, 7, v, ln)) continue;
            if (FindMaterial(t[1]) >= 0) { Err(ln, "material '" + t[1] + "' is defined twice"); continue; }
            if (v[6] <= 0.0f) { Err(ln, "material tile size must be > 0"); continue; }
            Material m;
            m.name = t[1]; m.texSpec = t[2];
            m.tex = tm.Get(t[2]);                      // reports + survives a missing file
            m.specular = v[0]; m.shininess = v[1]; m.colour = glm::vec3(v[2], v[3], v[4]); m.emissive = v[5]; m.tile = v[6];
            materials.push_back(m);
        }
        else if (cmd == "box")
        {
            if (!need(11, "box <name> <material> <cx> <cy> <cz> <sx> <sy> <sz> <solid 0/1> <castShadow 0/1>")) continue;
            if (!Floats(t, 3, 8, v, ln)) continue;
            int mi = FindMaterial(t[2]);
            if (mi < 0) { Err(ln, "box '" + t[1] + "': unknown material '" + t[2] + "' (using 'default')"); mi = 0; }
            if (v[3] <= 0 || v[4] <= 0 || v[5] <= 0) { Err(ln, "box '" + t[1] + "': sizes must be > 0"); continue; }
            SceneObject o;
            o.name = t[1]; o.material = mi;
            o.pos = glm::vec3(v[0], v[1], v[2]); o.size = glm::vec3(v[3], v[4], v[5]);
            o.solid = v[6] != 0.0f; o.castShadow = v[7] != 0.0f;
            o.minB = o.pos - o.size * 0.5f; o.maxB = o.pos + o.size * 0.5f;
            o.mesh = MakeBox(o.size.x, o.size.y, o.size.z, materials[mi].tile);
            objects.push_back(o);
        }
        else if (cmd == "bulb")
        {
            // bulb <name> <r> <g> <b> <height> <intensity> <speed> <x z> [<x z> ...]
            if (t.size() < 10 || ((t.size() - 8) % 2) != 0)
            {
                Err(ln, "'bulb' needs: name r g b height intensity speed, then one or more x z pairs");
                continue;
            }
            if ((int)bulbs.size() >= MAX_BULBS) { Err(ln, "too many bulbs (the shader supports " + std::to_string(MAX_BULBS) + ")"); continue; }
            if (!Floats(t, 2, 6, v, ln)) continue;
            Bulb b;
            b.name = t[1]; b.colour = glm::vec3(v[0], v[1], v[2]); b.height = v[3]; b.intensity = v[4]; b.speed = v[5];
            bool okPts = true;
            for (size_t i = 8; i + 1 < t.size(); i += 2)
            {
                float xz[2];
                if (!Floats(t, i, 2, xz, ln)) { okPts = false; break; }
                b.path.push_back(glm::vec2(xz[0], xz[1]));
            }
            if (!okPts) continue;
            b.Finalise();
            bulbs.push_back(b);
        }
        else if (cmd == "player")
        {
            if (!need(3, "player <x> <z>") || !Floats(t, 1, 2, v, ln)) continue;
            playerStart = glm::vec2(v[0], v[1]); havePlayer = true;
        }
        else if (cmd == "exit")
        {
            if (!need(4, "exit <x> <z> <radius>") || !Floats(t, 1, 3, v, ln)) continue;
            if (v[2] <= 0.0f) { Err(ln, "exit radius must be > 0"); continue; }
            exitPos = glm::vec2(v[0], v[1]); exitRadius = v[2]; haveExit = true;
        }
        else if (cmd == "bounds")
        {
            if (!need(5, "bounds <minX> <minZ> <maxX> <maxZ>") || !Floats(t, 1, 4, v, ln)) continue;
            if (v[2] <= v[0] || v[3] <= v[1]) { Err(ln, "bounds max must be larger than min"); continue; }
            bounds = glm::vec4(v[0], v[1], v[2], v[3]);
        }
        else
        {
            Err(ln, "unknown directive '" + cmd + "'");
        }
    }

    // ---- is the level playable at all? ----
    bool playable = true;
    if (!havePlayer)      { Err(ln, "no 'player' line - the level needs a start position"); playable = false; }
    if (!haveExit)        { Err(ln, "no 'exit' line - the level needs an exit"); playable = false; }
    if (bulbs.empty())    { Err(ln, "no 'bulb' lines - the level needs at least one light"); playable = false; }
    if (objects.empty())  { Err(ln, "no 'box' lines - the level is empty"); playable = false; }
    int patrolling = 0;
    for (auto& b : bulbs) if (!b.IsStatic()) ++patrolling;
    if (playable && patrolling < 2)
        std::cerr << "[SCENE] warning: BLACKOUT asks for at least two PATROLLING bulbs, this level has " << patrolling << "\n";
    return playable;
}
