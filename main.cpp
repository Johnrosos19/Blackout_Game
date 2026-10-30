// ===========================================================================
//  BLACKOUT  -  "Light is the enemy"                       Group 1 / main.cpp
//  OpenGL 4.6, GLFW + GLEW + GLM, Visual Studio.
//
//  One frame:
//    1. input + game update (CPU lit test drives the alarm)
//    2. directional shadow map            (directional_shadow_map.*)
//    3. one omni cube map per bulb        (omni_shadow_map.vert/.geom/.frag)
//         -> ONE draw pass per bulb, the geometry shader fans out to 6 faces
//    4. skybox                            (skybox.*)
//    5. scene + bulbs + player            (shader.vert / shader.frag)
//    6. HUD with glm::ortho               (hud.*)
// ===========================================================================
#include "CommonValues.h"
#include "Shader.h"
#include "Mesh.h"
#include "Textures.h"
#include "Scene.h"
#include "Game.h"
#include "LitTest.h"
#include "Hud.h"
#include "Paths.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <fstream>

// ---------------------------------------------------------------------------
// window / camera state
// ---------------------------------------------------------------------------
static int    g_fbW = 1280, g_fbH = 720;
static float  g_camYaw = -0.785f, g_camPitch = 0.95f, g_camDist = 9.0f;
static double g_lastX = 0, g_lastY = 0;
static bool   g_firstMouse = true;
static bool   g_prevKey[GLFW_KEY_LAST + 1] = {};

static void FramebufferSizeCallback(GLFWwindow*, int w, int h)
{
    g_fbW = std::max(w, 1);
    g_fbH = std::max(h, 1);
}

static void CursorCallback(GLFWwindow*, double x, double y)
{
    if (g_firstMouse) { g_lastX = x; g_lastY = y; g_firstMouse = false; }
    float dx = (float)(x - g_lastX), dy = (float)(y - g_lastY);
    g_lastX = x; g_lastY = y;
    g_camYaw   += dx * 0.0035f;
    g_camPitch  = glm::clamp(g_camPitch + dy * 0.0035f, 0.60f, 1.45f);   // never lower than the tallest wall
}

static bool Pressed(GLFWwindow* w, int key)
{
    bool now = glfwGetKey(w, key) == GLFW_PRESS;
    bool res = now && !g_prevKey[key];
    g_prevKey[key] = now;
    return res;
}

// ---------------------------------------------------------------------------
// GPU resources
// ---------------------------------------------------------------------------
struct OmniMap
{
    GLuint fbo = 0, tex = 0;
    glm::vec3 lastPos{ 0.0f };
    bool valid = false;                 // E1: has this cube map ever been rendered?
};

class Renderer
{
public:
    Shader mainShader, dirShadow, omniShadow, skyShader;
    Hud hud;
    TextureManager textures;

    std::vector<OmniMap> omni;
    GLuint dirFbo = 0, dirTex = 0;
    bool dirValid = false;
    GLuint skyTex = 0, whiteTex = 0;
    GLuint probeFbo = 0, probeTex = 0, probeDepth = 0;
    GLuint queries[2] = { 0, 0 };
    int queryIdx = 0;
    bool queryIssued[2] = { false, false };

    std::shared_ptr<Mesh> skyMesh, sphereMesh, bodyMesh, headMesh, beaconMesh, padMesh, torchMesh;

    // debugging / "break it on purpose" / extension switches
    bool  cacheEnabled = false;         // E1
    int   breakMode = 0;                // 0 ok, 1 GPU shadows off, 2 CPU ignores walls, 3 huge bias
    int   debugView = 0;                // V
    bool  liveProbe = false;            // E2: show the GPU pixel value next to the CPU number
    float omniBias = 0.1f;

    glm::mat4 dirLightTransform{ 1.0f };
    float shadowGpuMs = 0.0f;
    int   mapsRebuilt = 0;
    float liveGpuIllum = 0.0f;

    bool Init(Scene& sc);
    void UpdateShadowMaps(const Scene& sc, const Game& game);
    void DrawFrame(const Scene& sc, const Game& game, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye);
    bool ProbeGPU(const Scene& sc, const Game& game, float x, float z, float& illum, unsigned& mask);
    void Shutdown();

private:
    bool CreateOmni(OmniMap& m, int res);
    bool CreateDirMap(int res);
    bool CreateProbe();
    void SetSceneUniforms(const Scene& sc, const Game& game, const glm::vec3& eye, int debugMode);
    void DrawCasters(Shader& sh, const Scene& sc);
    void DrawWorld(const Scene& sc, const Game& game, bool drawExtras);
    void DrawMesh(const Mesh& m, const glm::mat4& model, GLuint tex, const glm::vec3& colour,
                  float spec, float shin, float emissive);
    int  dirRes = 2048, omniRes = 1024;
};

// ---------------------------------------------------------------------------
bool Renderer::CreateOmni(OmniMap& m, int res)
{
    glGenTextures(1, &m.tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m.tex);
    glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, GL_DEPTH_COMPONENT32F, res, res);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m.fbo);
    // glFramebufferTexture (NOT ...Texture2D): attaches the WHOLE cube map so gl_Layer works
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m.tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) { std::cerr << "[GL] omni shadow framebuffer incomplete\n"; return false; }
    return true;
}

bool Renderer::CreateDirMap(int res)
{
    glGenTextures(1, &dirTex);
    glBindTexture(GL_TEXTURE_2D, dirTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, res, res);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &dirFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, dirFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dirTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) { std::cerr << "[GL] directional shadow framebuffer incomplete\n"; return false; }
    return true;
}

bool Renderer::CreateProbe()
{
    const int S = 15;       // odd size: the centre pixel is exactly at the probe position
    glGenTextures(1, &probeTex);
    glBindTexture(GL_TEXTURE_2D, probeTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, S, S);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &probeDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, probeDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, S, S);
    glGenFramebuffers(1, &probeFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, probeFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, probeDepth);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return st == GL_FRAMEBUFFER_COMPLETE;
}

bool Renderer::Init(Scene& sc)
{
    omniRes = sc.omniRes;
    dirRes = sc.dirRes;
    omniBias = sc.omniBias;

    if (!mainShader.CreateFromFiles("Shaders/shader.vert", "Shaders/shader.frag")) return false;
    if (!dirShadow.CreateFromFiles("Shaders/directional_shadow_map.vert", "Shaders/directional_shadow_map.frag")) return false;
    if (!omniShadow.CreateFromFiles("Shaders/omni_shadow_map.vert", "Shaders/omni_shadow_map.frag", "Shaders/omni_shadow_map.geom")) return false;
    if (!skyShader.CreateFromFiles("Shaders/skybox.vert", "Shaders/skybox.frag")) return false;
    if (!hud.Init()) return false;

    // texture units (README: texture unit list)
    mainShader.SetInt("theTexture", 0);
    mainShader.SetInt("directionalShadowMap", 1);
    for (int i = 0; i < MAX_BULBS; ++i)
        mainShader.SetInt("omniShadowMaps[" + std::to_string(i) + "]", 3 + i);
    skyShader.SetInt("skybox", 8);

    omni.resize(sc.bulbs.size());
    for (auto& m : omni) if (!CreateOmni(m, omniRes)) return false;
    if (!CreateDirMap(dirRes)) return false;
    if (!CreateProbe()) return false;
    glGenQueries(2, queries);

    int missing = 0;
    skyTex = CreateSkyCube(sc.skySpec == "files" ? sc.skyFiles : std::vector<std::string>(), missing);
    whiteTex = textures.Get("proc:white");

    skyMesh    = MakeSkyCube();
    sphereMesh = MakeSphere(0.28f, 24, 16);
    bodyMesh   = MakeCylinder(0.30f, 0.95f, 20);
    headMesh   = MakeSphere(0.26f, 20, 14);
    beaconMesh = MakeCylinder(0.07f, 4.0f, 12);
    padMesh    = MakeBox(2.4f, 0.06f, 2.4f, 1.2f);
    torchMesh  = MakeBox(0.10f, 0.10f, 0.30f, 1.0f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return true;
}

void Renderer::Shutdown()
{
    for (auto& m : omni) { glDeleteFramebuffers(1, &m.fbo); glDeleteTextures(1, &m.tex); }
    glDeleteFramebuffers(1, &dirFbo);  glDeleteTextures(1, &dirTex);
    glDeleteFramebuffers(1, &probeFbo); glDeleteTextures(1, &probeTex); glDeleteRenderbuffers(1, &probeDepth);
    glDeleteQueries(2, queries);
    glDeleteTextures(1, &skyTex);
}

void Renderer::DrawCasters(Shader& sh, const Scene& sc)
{
    for (const auto& o : sc.objects)
    {
        if (!o.castShadow) continue;
        sh.SetMat4("model", glm::translate(glm::mat4(1.0f), o.pos));
        o.mesh->Render();
    }
}

// ---------------------------------------------------------------------------
// shadow maps. E1: with 'cacheEnabled' a cube map is only re-rendered when its
// bulb has moved (the dirty flag). Everything else in the level is static, so
// the cached map is exact here; if something blocking could MOVE inside a
// bulb's range, that bulb's map would be stale until it was marked dirty.
// ---------------------------------------------------------------------------
void Renderer::UpdateShadowMaps(const Scene& sc, const Game&)
{
    mapsRebuilt = 0;
    glBeginQuery(GL_TIME_ELAPSED, queries[queryIdx]);

    glDisable(GL_CULL_FACE);                      // draw both faces into shadow maps

    // ---- directional ----
    if (!cacheEnabled || !dirValid)
    {
        glm::vec3 d = glm::normalize(sc.dirDirection);
        glm::vec3 lightPos = -d * 30.0f;
        glm::vec3 up = (std::fabs(d.y) > 0.95f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
        glm::mat4 lightProj = glm::ortho(-24.0f, 24.0f, -24.0f, 24.0f, 1.0f, 70.0f);
        dirLightTransform = lightProj * lightView;

        glViewport(0, 0, dirRes, dirRes);
        glBindFramebuffer(GL_FRAMEBUFFER, dirFbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        dirShadow.Use();
        dirShadow.SetMat4("directionalLightTransform", dirLightTransform);
        DrawCasters(dirShadow, sc);
        dirValid = true;
    }

    // ---- one omnidirectional cube map per bulb ----
    for (size_t i = 0; i < sc.bulbs.size(); ++i)
    {
        const Bulb& b = sc.bulbs[i];
        OmniMap& m = omni[i];
        bool dirty = !cacheEnabled || !m.valid || glm::distance(m.lastPos, b.position) > 1e-4f;
        if (!dirty) continue;

        const glm::vec3 p = b.position;
        glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, sc.bulbFar);
        glm::mat4 mats[6] = {
            proj * glm::lookAt(p, p + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
            proj * glm::lookAt(p, p + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
            proj * glm::lookAt(p, p + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
            proj * glm::lookAt(p, p + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
            proj * glm::lookAt(p, p + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
            proj * glm::lookAt(p, p + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)) };

        glViewport(0, 0, omniRes, omniRes);
        glBindFramebuffer(GL_FRAMEBUFFER, m.fbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        omniShadow.Use();
        omniShadow.SetMat4Array("lightMatrices", mats, 6);
        omniShadow.SetVec3("lightPos", p);
        omniShadow.SetFloat("farPlane", sc.bulbFar);
        DrawCasters(omniShadow, sc);             // ONE draw pass; the geometry shader makes 6 faces

        m.lastPos = p;
        m.valid = true;
        ++mapsRebuilt;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_CULL_FACE);

    glEndQuery(GL_TIME_ELAPSED);
    queryIssued[queryIdx] = true;
    int prev = 1 - queryIdx;
    if (queryIssued[prev])
    {
        GLuint64 ns = 0;
        glGetQueryObjectui64v(queries[prev], GL_QUERY_RESULT, &ns);
        shadowGpuMs = shadowGpuMs * 0.9f + (float)((double)ns / 1.0e6) * 0.1f;
    }
    queryIdx = prev;
}

// ---------------------------------------------------------------------------
void Renderer::SetSceneUniforms(const Scene& sc, const Game& game, const glm::vec3& eye, int debugMode)
{
    Shader& s = mainShader;
    s.SetVec3("directionalLight.colour", sc.dirColour);
    s.SetFloat("directionalLight.ambientIntensity", sc.dirAmbient);
    s.SetFloat("directionalLight.diffuseIntensity", sc.dirDiffuse);
    s.SetVec3("directionalLight.direction", glm::normalize(sc.dirDirection));
    s.SetMat4("directionalLightTransform", dirLightTransform);

    s.SetInt("pointLightCount", (int)sc.bulbs.size());
    for (size_t i = 0; i < sc.bulbs.size(); ++i)
    {
        const Bulb& b = sc.bulbs[i];
        std::string base = "pointLights[" + std::to_string(i) + "].";
        s.SetVec3(base + "colour", b.colour);
        s.SetFloat(base + "intensity", b.intensity);
        s.SetVec3(base + "position", b.position);
        s.SetFloat(base + "constant", sc.attC);
        s.SetFloat(base + "linear", sc.attL);
        s.SetFloat(base + "exponent", sc.attE);
        s.SetFloat(base + "farPlane", sc.bulbFar);
    }

    // torch: a spot light re-aimed from the player every frame
    glm::vec3 tp = game.playerPos + glm::vec3(game.facing.x * 0.35f, 0.95f, game.facing.y * 0.35f);
    glm::vec3 td = glm::normalize(glm::vec3(game.facing.x, -0.32f, game.facing.y));
    s.SetInt("spotOn", game.flashOn ? 1 : 0);
    s.SetVec3("spotLight.colour", sc.flashColour);
    s.SetFloat("spotLight.intensity", sc.flashIntensity);
    s.SetVec3("spotLight.position", tp);
    s.SetVec3("spotLight.direction", td);
    s.SetFloat("spotLight.edge", std::cos(glm::radians(sc.flashCutoffDeg)));
    s.SetFloat("spotLight.constant", 1.0f);
    s.SetFloat("spotLight.linear", 0.10f);
    s.SetFloat("spotLight.exponent", 0.03f);

    s.SetVec3("eyePosition", eye);
    s.SetInt("shadowsEnabled", breakMode == 1 ? 0 : 1);
    s.SetFloat("omniBias", breakMode == 3 ? 2.0f : omniBias);
    s.SetInt("debugMode", debugMode);
}

void Renderer::DrawMesh(const Mesh& m, const glm::mat4& model, GLuint tex, const glm::vec3& colour,
                        float spec, float shin, float emissive)
{
    mainShader.SetMat4("model", model);
    mainShader.SetFloat("specularIntensity", spec);
    mainShader.SetFloat("shininess", shin);
    mainShader.SetVec3("materialColour", colour);
    mainShader.SetFloat("emissive", emissive);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    m.Render();
}

void Renderer::DrawWorld(const Scene& sc, const Game& game, bool drawExtras)
{
    // bind the shadow maps to their fixed units
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, dirTex);
    for (size_t i = 0; i < omni.size(); ++i)
    {
        glActiveTexture(GL_TEXTURE3 + (GLenum)i);
        glBindTexture(GL_TEXTURE_CUBE_MAP, omni[i].tex);
    }

    for (const auto& o : sc.objects)
    {
        const Material& m = sc.materials[o.material];
        DrawMesh(*o.mesh, glm::translate(glm::mat4(1.0f), o.pos), m.tex, m.colour, m.specular, m.shininess, m.emissive);
    }

    if (!drawExtras) return;

    // exit pad + beacon
    GLuint padTex = textures.Get("proc:exitpad");
    glm::vec3 ep(sc.exitPos.x, 0.0f, sc.exitPos.y);
    DrawMesh(*padMesh, glm::translate(glm::mat4(1.0f), ep + glm::vec3(0, 0.03f, 0)), padTex, glm::vec3(1), 0.2f, 8.0f, 0.9f);
    DrawMesh(*beaconMesh, glm::translate(glm::mat4(1.0f), ep), whiteTex, glm::vec3(0.3f, 1.0f, 0.4f), 0.0f, 1.0f, 1.5f);

    // the bulbs themselves
    for (const auto& b : sc.bulbs)
        DrawMesh(*sphereMesh, glm::translate(glm::mat4(1.0f), b.position), whiteTex, b.colour, 0.0f, 1.0f, 2.0f);

    // the player (does not cast into the cube maps - see README, "player and shadows")
    if (game.state != GameState::Title)
    {
        glm::vec3 c(0.25f, 0.55f, 1.0f);
        DrawMesh(*bodyMesh, glm::translate(glm::mat4(1.0f), game.playerPos), whiteTex, c, 0.4f, 24.0f, 0.0f);
        DrawMesh(*headMesh, glm::translate(glm::mat4(1.0f), game.playerPos + glm::vec3(0, 1.15f, 0)), whiteTex,
                 glm::vec3(0.9f, 0.75f, 0.6f), 0.3f, 16.0f, 0.0f);
        float ang = std::atan2(game.facing.x, game.facing.y);
        glm::mat4 tm = glm::translate(glm::mat4(1.0f), game.playerPos + glm::vec3(game.facing.x * 0.35f, 0.95f, game.facing.y * 0.35f));
        tm = glm::rotate(tm, ang, glm::vec3(0, 1, 0));
        DrawMesh(*torchMesh, tm, whiteTex, glm::vec3(0.2f), 0.5f, 32.0f, game.flashOn ? 0.6f : 0.0f);
    }
}

void Renderer::DrawFrame(const Scene& sc, const Game& game, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_fbW, g_fbH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ---- skybox ----
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    skyShader.Use();
    skyShader.SetMat4("view", view);
    skyShader.SetMat4("projection", proj);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyTex);
    skyMesh->Render();
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    // ---- world ----
    mainShader.Use();
    mainShader.SetMat4("view", view);
    mainShader.SetMat4("projection", proj);
    SetSceneUniforms(sc, game, eye, debugView);
    DrawWorld(sc, game, true);
}

// ---------------------------------------------------------------------------
// GPU probe: render the floor point (x,0,z) from straight above through a tiny
// orthographic window into a float framebuffer with debugMode 2, read the
// centre pixel back. Returns exactly what the fragment shader computed there.
// ---------------------------------------------------------------------------
bool Renderer::ProbeGPU(const Scene& sc, const Game& game, float x, float z, float& illum, unsigned& mask)
{
    const int S = 15;
    glBindFramebuffer(GL_FRAMEBUFFER, probeFbo);
    glViewport(0, 0, S, S);
    glClearColor(-1.0f, -1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = glm::lookAt(glm::vec3(x, 6.0f, z), glm::vec3(x, 0.0f, z), glm::vec3(0, 0, -1));
    glm::mat4 proj = glm::ortho(-0.0375f, 0.0375f, -0.0375f, 0.0375f, 0.1f, 20.0f);   // 15 px * 0.005 = 0.075 wide

    mainShader.Use();
    mainShader.SetMat4("view", view);
    mainShader.SetMat4("projection", proj);
    SetSceneUniforms(sc, game, glm::vec3(x, 6.0f, z), 2);
    DrawWorld(sc, game, false);

    float px[4] = { -1, -1, 0, 0 };
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(S / 2, S / 2, 1, 1, GL_RGBA, GL_FLOAT, px);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_fbW, g_fbH);

    if (px[0] < 0.0f) return false;           // nothing drawn at that pixel
    illum = px[0];
    mask = (unsigned)(px[1] + 0.5f);
    return true;
}

// ---------------------------------------------------------------------------
// P3 study: 20 sample positions (10 within a hand's width of a shadow edge),
// CPU answer vs GPU answer, with the distance to the nearest shadow edge.
// Writes p3_results.csv next to the working directory.
// ---------------------------------------------------------------------------
struct Lcg
{
    unsigned s = 20260429u;
    float next() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * next(); }
};

static unsigned MaskAt(const Scene& sc, const glm::vec2& p, bool los)
{
    return ComputeIllumination(sc, glm::vec3(p.x, SAMPLE_HEIGHT, p.y), los).visMask;
}

static float NearestEdgeDistance(const Scene& sc, const glm::vec2& p, bool los, float maxR)
{
    unsigned m0 = MaskAt(sc, p, los);
    float best = maxR + 1.0f;
    for (int a = 0; a < 72; ++a)
    {
        float ang = (float)a * (2.0f * PI_F / 72.0f);
        glm::vec2 dir(std::cos(ang), std::sin(ang));
        for (float r = 0.0025f; r <= maxR; r += 0.0025f)
        {
            glm::vec2 q = p + dir * r;
            if (PointInsideSolid(sc, q.x, q.y, 0.0f)) break;      // object boundary is not a shadow edge
            if (MaskAt(sc, q, los) != m0) { best = std::min(best, r); break; }
        }
    }
    return best <= maxR ? best : -1.0f;
}

static void RunP3Study(Renderer& R, Scene& sc, Game& game)
{
    std::printf("\n========== P3 STUDY: CPU lit-test vs GPU shadow lookup ==========\n");
    std::vector<float> savedS;
    for (auto& b : sc.bulbs) { savedS.push_back(b.s); b.Reset(); }      // freeze bulbs at their start positions
    R.UpdateShadowMaps(sc, game);

    const bool los = true;
    Lcg rng;
    struct Sample { glm::vec2 p; bool edge; };
    std::vector<Sample> samples;
    const float offs[3] = { 0.02f, 0.05f, 0.09f };

    // --- 10 edge samples: find a CPU shadow boundary along a ray, then step either side of it ---
    int edges = 0, guard = 0;
    while (edges < 5 && guard++ < 5000)
    {
        glm::vec2 p0(rng.range(sc.bounds.x + 1, sc.bounds.z - 1), rng.range(sc.bounds.y + 1, sc.bounds.w - 1));
        if (PointInsideSolid(sc, p0.x, p0.y, 0.5f)) continue;
        float ang = rng.range(0.0f, 2.0f * PI_F);
        glm::vec2 dir(std::cos(ang), std::sin(ang));
        unsigned m0 = MaskAt(sc, p0, los);
        glm::vec2 prev = p0;
        for (float t = 0.05f; t <= 6.0f; t += 0.05f)
        {
            glm::vec2 q = p0 + dir * t;
            if (PointInsideSolid(sc, q.x, q.y, 0.3f) || q.x < sc.bounds.x + 0.5f || q.x > sc.bounds.z - 0.5f ||
                q.y < sc.bounds.y + 0.5f || q.y > sc.bounds.w - 0.5f) break;
            if (MaskAt(sc, q, los) != m0)
            {
                glm::vec2 a = prev, b = q;
                for (int k = 0; k < 30; ++k)
                {
                    glm::vec2 mid = (a + b) * 0.5f;
                    if (MaskAt(sc, mid, los) == m0) a = mid; else b = mid;
                }
                glm::vec2 e = (a + b) * 0.5f;
                float o = offs[edges % 3];
                samples.push_back({ e - dir * o, true });
                samples.push_back({ e + dir * o, true });
                ++edges;
                break;
            }
            prev = q;
        }
    }
    // --- 10 ordinary samples ---
    guard = 0;
    while (samples.size() < 20 && guard++ < 5000)
    {
        glm::vec2 p(rng.range(sc.bounds.x + 1, sc.bounds.z - 1), rng.range(sc.bounds.y + 1, sc.bounds.w - 1));
        if (PointInsideSolid(sc, p.x, p.y, 0.5f)) continue;
        samples.push_back({ p, false });
    }

    std::ofstream csv("p3_results.csv");
    csv << "n,x,z,edge_sample,cpu_mask,gpu_mask,cpu_illum,gpu_illum,cpu_lit,gpu_lit,dist_to_nearest_shadow_edge,agree\n";
    std::printf("%-3s %-17s %-6s %-22s %-22s %-10s %s\n", "n", "position (x,z)", "edge?", "CPU says", "GPU says", "edge dist", "agree");

    int disagree = 0, probed = 0;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const glm::vec2 p = samples[i].p;
        LitResult cpu = ComputeIllumination(sc, glm::vec3(p.x, SAMPLE_HEIGHT, p.y), los);
        float gi = 0.0f; unsigned gm = 0;
        bool ok = R.ProbeGPU(sc, game, p.x, p.y, gi, gm);
        if (!ok) { std::printf("%-3zu probe failed at (%.3f, %.3f)\n", i + 1, p.x, p.y); continue; }
        ++probed;
        bool cl = cpu.illum >= sc.litThreshold, gl = gi >= sc.litThreshold;
        bool agree = (cpu.visMask == gm) && (cl == gl);
        if (!agree) ++disagree;
        float ed = NearestEdgeDistance(sc, p, los, 0.6f);

        char c1[64], c2[64], pos[48], edb[24];
        std::snprintf(c1, sizeof c1, "%s i=%.3f m=%u%u%u%u", cl ? "LIT   " : "SHADOW", cpu.illum,
                      (cpu.visMask >> 3) & 1, (cpu.visMask >> 2) & 1, (cpu.visMask >> 1) & 1, cpu.visMask & 1);
        std::snprintf(c2, sizeof c2, "%s i=%.3f m=%u%u%u%u", gl ? "LIT   " : "SHADOW", gi,
                      (gm >> 3) & 1, (gm >> 2) & 1, (gm >> 1) & 1, gm & 1);
        std::snprintf(pos, sizeof pos, "(%7.3f,%7.3f)", p.x, p.y);
        if (ed >= 0) std::snprintf(edb, sizeof edb, "%.4f", ed); else std::snprintf(edb, sizeof edb, ">0.60");
        std::printf("%-3zu %-17s %-6s %-22s %-22s %-10s %s\n", i + 1, pos, samples[i].edge ? "yes" : "no", c1, c2, edb,
                    agree ? "ok" : "DISAGREE");
        csv << (i + 1) << "," << p.x << "," << p.y << "," << (samples[i].edge ? 1 : 0) << "," << cpu.visMask << "," << gm << ","
            << cpu.illum << "," << gi << "," << (cl ? 1 : 0) << "," << (gl ? 1 : 0) << "," << (ed >= 0 ? std::to_string(ed) : ">0.6")
            << "," << (agree ? 1 : 0) << "\n";
    }
    std::printf("---- %d samples probed, %d disagreement(s). Saved p3_results.csv (mask bits: bulb3 bulb2 bulb1 bulb0)\n", probed, disagree);
    std::printf("omni map %d x %d, bias %.3f world units, attenuation %.3f/%.3f/%.3f, threshold %.3f\n\n",
                sc.omniRes, sc.omniRes, R.omniBias, sc.attC, sc.attL, sc.attE, sc.litThreshold);

    for (size_t i = 0; i < sc.bulbs.size(); ++i) { sc.bulbs[i].s = savedS[i]; sc.bulbs[i].Place(); }
    R.UpdateShadowMaps(sc, game);           // maps must match the restored bulb positions again
}

// ---------------------------------------------------------------------------
// HUD (R4): alarm bar, light meter, state banners, performance read-out
// ---------------------------------------------------------------------------
static std::string Fmt(const char* f, double v)
{
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

static void DrawHud(Renderer& R, const Scene& sc, const Game& g, float frameMs, bool vsync)
{
    const int W = g_fbW, H = g_fbH;
    const int ts = std::max(2, H / 240);          // text scale (pixels per glyph pixel)
    const int big = ts * 3;
    Hud& hud = R.hud;
    hud.Begin(W, H);

    const glm::vec4 white(1, 1, 1, 1), grey(0.75f, 0.75f, 0.8f, 1), dim(0, 0, 0, 0.55f);
    auto centre = [&](const std::string& s, float y, int scale, const glm::vec4& c) {
        while (scale > 1 && hud.TextWidth(s, scale) > W * 0.96f) --scale;     // shrink to fit narrow windows
        hud.Text(s, (W - hud.TextWidth(s, scale)) * 0.5f, y, scale, c);
    };

    // red vignette that grows with the alarm
    if (g.state == GameState::Playing && g.alarm > 0.3f)
        hud.Rect(0, 0, (float)W, (float)H, glm::vec4(0.8f, 0.0f, 0.0f, 0.30f * (g.alarm - 0.3f) / 0.7f));

    // ---- ALARM METER (top centre) ----
    float barW = W * 0.40f, barH = 10.0f * ts, bx = (W - barW) * 0.5f, by = H - barH - 6.0f * ts - 8.0f * ts;
    hud.Text("ALARM", bx, by + barH + 2.0f * ts, ts, grey);
    hud.Rect(bx - 3, by - 3, barW + 6, barH + 6, glm::vec4(0, 0, 0, 0.7f));
    glm::vec3 col = (g.alarm < 0.5f) ? glm::mix(glm::vec3(0.1f, 0.8f, 0.2f), glm::vec3(1.0f, 0.85f, 0.1f), g.alarm * 2.0f)
                                     : glm::mix(glm::vec3(1.0f, 0.85f, 0.1f), glm::vec3(1.0f, 0.1f, 0.05f), (g.alarm - 0.5f) * 2.0f);
    hud.Rect(bx, by, barW * g.alarm, barH, glm::vec4(col, 1.0f));
    std::string pct = Fmt("%.0f", g.alarm * 100.0) + "%";
    hud.Text(pct, bx + barW - hud.TextWidth(pct, ts), by + barH + 2.0f * ts, ts, white);

    // ---- LIGHT METER (bottom left): the number the alarm is actually driven by (E2) ----
    float lx = 12.0f, ly = 12.0f;
    hud.Rect(lx - 6, ly - 6, 88.0f * ts + 12.0f, 40.0f * ts, dim);
    hud.Text(g.lit ? "LIT" : "SHADOW", lx, ly + 20.0f * ts, ts * 2, g.lit ? glm::vec4(1.0f, 0.35f, 0.25f, 1) : glm::vec4(0.3f, 0.9f, 0.5f, 1));
    hud.Text("LIGHT " + Fmt("%.3f", g.illum), lx, ly + 11.0f * ts, ts, white);
    if (R.liveProbe) hud.Text("GPU   " + Fmt("%.3f", R.liveGpuIllum), lx, ly + 2.0f * ts, ts, glm::vec4(0.5f, 0.9f, 1.0f, 1));
    else             hud.Text("THRESHOLD " + Fmt("%.2f", sc.litThreshold), lx, ly + 2.0f * ts, ts, grey);

    // ---- performance (top right) ----
    float rx = W - 12.0f;
    auto right = [&](const std::string& s, float y, const glm::vec4& c) { hud.Text(s, rx - hud.TextWidth(s, ts), y, ts, c); };
    right(Fmt("%.1f", frameMs) + " MS " + (vsync ? "VSYNC" : "NO VSYNC"), H - 10.0f * ts, grey);
    right("SHADOW GPU " + Fmt("%.2f", R.shadowGpuMs) + " MS", H - 19.0f * ts, grey);
    right(std::string("CACHE ") + (R.cacheEnabled ? "ON " : "OFF ") + std::to_string(R.mapsRebuilt) + "/" +
          std::to_string(sc.bulbs.size()) + " MAPS", H - 28.0f * ts, grey);

    // ---- level / time (bottom right) ----
    {
        std::string s = "LEVEL " + std::to_string(g.level);
        hud.Text(s, W - 12.0f - hud.TextWidth(s, ts), 12.0f + 9.0f * ts, ts, white);
        std::string t = "TIME " + Fmt("%.1f", g.elapsed);
        hud.Text(t, W - 12.0f - hud.TextWidth(t, ts), 12.0f, ts, white);
    }

    // "break it on purpose" banner
    const char* breakNames[] = { "", "BROKEN: GPU SHADOWS OFF", "BROKEN: CPU IGNORES WALLS", "BROKEN: HUGE SHADOW BIAS" };
    if (R.breakMode > 0) centre(breakNames[R.breakMode], H * 0.82f, ts * 2, glm::vec4(1.0f, 0.5f, 0.1f, 1));
    if (R.debugView == 1) centre("SHADOW FACTOR VIEW  R G B = BULB 0 1 2", H * 0.78f, ts, glm::vec4(0.6f, 1, 0.6f, 1));

    // ---- state screens ----
    if (g.state == GameState::Title)
    {
        hud.Rect(0, 0, (float)W, (float)H, glm::vec4(0, 0, 0, 0.62f));
        centre("BLACKOUT", H * 0.66f, big * 2, white);
        centre("LIGHT IS THE ENEMY", H * 0.58f, big, glm::vec4(1.0f, 0.8f, 0.3f, 1));
        centre("REACH THE GREEN EXIT", H * 0.46f, ts * 2, glm::vec4(0.4f, 1, 0.5f, 1));
        centre("STAND IN LIGHT AND THE ALARM FILLS", H * 0.40f, ts * 2, white);
        centre("HIDE BEHIND THINGS TO DRAIN IT", H * 0.34f, ts * 2, white);
        centre("WASD MOVE   MOUSE LOOK   F TORCH   P PAUSE   R RESTART", H * 0.26f, ts, grey);
        centre("PRESS ENTER TO START", H * 0.16f, big, white);
    }
    else if (g.state == GameState::Paused)
    {
        hud.Rect(0, 0, (float)W, (float)H, glm::vec4(0, 0, 0, 0.5f));
        centre("PAUSED", H * 0.52f, big * 2, white);
        centre("PRESS P TO CONTINUE", H * 0.42f, ts * 2, grey);
    }
    else if (g.state == GameState::Won)
    {
        hud.Rect(0, 0, (float)W, (float)H, glm::vec4(0, 0.25f, 0, 0.55f));
        centre("ESCAPED", H * 0.58f, big * 2, glm::vec4(0.4f, 1, 0.5f, 1));
        centre("TIME " + Fmt("%.1f", g.finalTime) + " SECONDS", H * 0.48f, ts * 2, white);
        centre("PRESS R FOR LEVEL " + std::to_string(g.level + 1) + "  BULBS MOVE FASTER", H * 0.38f, ts * 2, white);
    }
    else if (g.state == GameState::Lost)
    {
        hud.Rect(0, 0, (float)W, (float)H, glm::vec4(0.35f, 0, 0, 0.6f));
        centre("SPOTTED", H * 0.58f, big * 2, glm::vec4(1, 0.3f, 0.25f, 1));
        centre("SURVIVED " + Fmt("%.1f", g.finalTime) + " SECONDS", H * 0.48f, ts * 2, white);
        centre("PRESS R TO TRY AGAIN", H * 0.38f, ts * 2, white);
    }
    hud.End();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static int Fail(const char* msg)
{
    std::cerr << "\n" << msg << "\n\nPress Enter to close...";
    std::cin.get();
    return 1;
}

int main(int argc, char** argv)
{
    std::string scenePath = (argc > 1) ? argv[1] : "Scenes/level1.txt";

    if (!glfwInit()) return Fail("GLFW failed to start.");

    // OpenGL 4.6 (B1). If a lab machine refuses, step down and rewrite the #version lines on load.
    GLFWwindow* window = nullptr;
    const int versions[3][2] = { { 4, 6 }, { 4, 5 }, { 4, 3 } };
    int ctxMinor = 0;
    // read the scene first only for the window size (cheap, no GL needed for the numbers) - fall back to defaults
    int winW = 1280, winH = 720;
    {
        std::ifstream pre(ResolveAsset(scenePath));
        std::string l;
        while (std::getline(pre, l))
        {
            int a, b;
            if (std::sscanf(l.c_str(), " window %d %d", &a, &b) == 2 && a >= 320 && b >= 240) { winW = a; winH = b; }
        }
    }
    for (auto& v : versions)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, v[0]);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, v[1]);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        window = glfwCreateWindow(winW, winH, "BLACKOUT - Light is the enemy", nullptr, nullptr);
        if (window) { ctxMinor = v[1]; break; }
    }
    if (!window) return Fail("Could not create an OpenGL 4.3+ window. Update your graphics driver.");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return Fail("GLEW failed to start.");
    glGetError();   // GLEW can leave a harmless GL_INVALID_ENUM behind

    std::printf("GL_VERSION  : %s\nGL_RENDERER : %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    if (ctxMinor != 6)
    {
        std::string line = "#version 4" + std::to_string(ctxMinor) + "0 core";
        Shader::SetVersionLine(line);
        std::printf("NOTE: OpenGL 4.6 was refused; shaders rewritten to '%s' (write this in the decision log).\n", line.c_str());
    }

    glfwGetFramebufferSize(window, &g_fbW, &g_fbH);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetCursorPosCallback(window, CursorCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    Renderer R;
    Scene scene;
    bool playable = scene.Load(scenePath, R.textures);
    if (!scene.errors.empty())
        std::printf("\n%zu problem(s) found in the scene file (see above).\n", scene.errors.size());
    if (!playable) return Fail("The level cannot be played - fix the lines reported above and run again.");
    if (!R.Init(scene)) return Fail("Renderer failed to start (see messages above).");

    Game game;
    game.Init(scene);

    std::printf("\nKeys: WASD move | mouse look | F torch | P pause | R restart | Enter start | Esc quit\n"
                "Debug: V shadow view | 1 GPU shadows off | 2 CPU ignores walls | 3 huge bias | 0 fix\n"
                "       [ ] bias | C cache (E1) | Y vsync | L live GPU probe (E2) | G compare here | P3 = F9 | T timing\n\n");

    double last = glfwGetTime();
    float frameMs = 16.0f;
    bool vsync = true;
    int framesForAvg = 0; double accum = 0.0; bool printTiming = false;
    double timingStamp = last;

    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        float rawDt = (float)(now - last);
        last = now;
        float dt = std::min(rawDt, 0.05f);
        frameMs = frameMs * 0.95f + rawDt * 1000.0f * 0.05f;

        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        // ---------------- input ----------------
        Input in;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)    in.forward += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)  in.forward -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) in.right += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)  in.right -= 1.0f;
        in.enter   = Pressed(window, GLFW_KEY_ENTER) | Pressed(window, GLFW_KEY_KP_ENTER) | Pressed(window, GLFW_KEY_SPACE);
        in.restart = Pressed(window, GLFW_KEY_R);
        in.pause   = Pressed(window, GLFW_KEY_P);

        if (Pressed(window, GLFW_KEY_F)) game.flashOn = !game.flashOn;
        if (Pressed(window, GLFW_KEY_V)) R.debugView = 1 - R.debugView;
        if (Pressed(window, GLFW_KEY_0)) R.breakMode = 0;
        if (Pressed(window, GLFW_KEY_1)) R.breakMode = 1;
        if (Pressed(window, GLFW_KEY_2)) R.breakMode = 2;
        if (Pressed(window, GLFW_KEY_3)) R.breakMode = 3;
        game.lineOfSightEnabled = (R.breakMode != 2);
        if (Pressed(window, GLFW_KEY_LEFT_BRACKET))  { R.omniBias = std::max(0.0f, R.omniBias - 0.01f); std::printf("omni bias = %.3f\n", R.omniBias); }
        if (Pressed(window, GLFW_KEY_RIGHT_BRACKET)) { R.omniBias += 0.01f; std::printf("omni bias = %.3f\n", R.omniBias); }
        if (Pressed(window, GLFW_KEY_C)) { R.cacheEnabled = !R.cacheEnabled; std::printf("shadow-map cache (E1): %s\n", R.cacheEnabled ? "ON" : "OFF"); }
        if (Pressed(window, GLFW_KEY_Y)) { vsync = !vsync; glfwSwapInterval(vsync ? 1 : 0); std::printf("vsync: %s\n", vsync ? "ON" : "OFF"); }
        if (Pressed(window, GLFW_KEY_L)) { R.liveProbe = !R.liveProbe; std::printf("live GPU probe (E2): %s\n", R.liveProbe ? "ON" : "OFF"); }
        if (Pressed(window, GLFW_KEY_T)) { printTiming = !printTiming; framesForAvg = 0; accum = 0; timingStamp = now; }
        bool doStudy = Pressed(window, GLFW_KEY_F9);
        bool doCompare = Pressed(window, GLFW_KEY_G);

        // ---------------- update ----------------
        game.Update(dt, scene, in, g_camYaw);
        if (game.state == GameState::Playing) game.EvaluateLighting(scene);   // picks up break mode changes immediately

        // ---------------- shadow maps ----------------
        R.UpdateShadowMaps(scene, game);

        if (R.liveProbe || doCompare)
        {
            float gi = 0.0f; unsigned gm = 0;
            if (R.ProbeGPU(scene, game, game.playerPos.x, game.playerPos.z, gi, gm))
            {
                R.liveGpuIllum = gi;
                if (doCompare)
                    std::printf("COMPARE at (%.3f, %.3f): CPU illum %.6f mask %u | GPU illum %.6f mask %u | diff %.6f\n",
                                game.playerPos.x, game.playerPos.z, game.illum, game.visMask, gi, gm, std::fabs(game.illum - gi));
            }
        }
        if (doStudy) RunP3Study(R, scene, game);

        // ---------------- camera + draw ----------------
        glm::vec3 fwd(std::cos(g_camYaw), 0.0f, std::sin(g_camYaw));
        glm::vec3 target = game.playerPos + glm::vec3(0.0f, 0.9f, 0.0f);
        glm::vec3 eye = target - fwd * (g_camDist * std::cos(g_camPitch)) + glm::vec3(0.0f, g_camDist * std::sin(g_camPitch), 0.0f);
        glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)g_fbW / (float)g_fbH, 0.1f, 150.0f);   // B3: aspect follows the window

        R.DrawFrame(scene, game, view, proj, eye);
        DrawHud(R, scene, game, frameMs, vsync);
        glfwSwapBuffers(window);

        if (printTiming)
        {
            accum += rawDt; ++framesForAvg;
            if (now - timingStamp >= 1.0)
            {
                std::printf("[timing] %.2f ms/frame avg (%d frames) | shadow passes %.3f ms GPU | cache %s | maps rebuilt this frame %d/%zu | vsync %s\n",
                            accum / framesForAvg * 1000.0, framesForAvg, R.shadowGpuMs, R.cacheEnabled ? "ON" : "OFF",
                            R.mapsRebuilt, scene.bulbs.size(), vsync ? "ON" : "OFF");
                framesForAvg = 0; accum = 0; timingStamp = now;
            }
        }
    }

    R.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
