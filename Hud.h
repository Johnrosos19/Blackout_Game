#pragma once
// ---------------------------------------------------------------------------
// Hud.h / Hud.cpp - flat on-screen display (owner: R4; digit strip made by R3)
//
// Projection:  glm::ortho(0, width, 0, height, -1, 1)  -> 1 unit = 1 pixel, origin bottom-left.
// A HUD has no depth and must not shrink with distance, so perspective is wrong here.
//
// Text: there is no font library. The "strip" is ONE texture holding every
// character in a row (6x8 pixel cells); a character is one textured quad whose
// texture coordinates are shifted to pick its cell (u = index/N).
// ---------------------------------------------------------------------------
#include "CommonValues.h"
#include "Shader.h"
#include <string>

class Hud
{
public:
    bool Init();
    void Begin(int width, int height);
    void Rect(float x, float y, float w, float h, const glm::vec4& colour);
    void Text(const std::string& s, float x, float y, int scale, const glm::vec4& colour);
    float TextWidth(const std::string& s, int scale) const;
    void End();

    GLuint StripTexture() const { return strip; }

private:
    Shader shader;
    GLuint vao = 0, vbo = 0, ibo = 0, strip = 0;
};
