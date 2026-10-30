#include "Hud.h"
#include <cctype>
#include <cstring>
#include <iostream>
#include <vector>

// Characters in strip order. Anything else is drawn as a space.
static const char* CHARSET = " 0123456789.:-%ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const int   GLYPH_COUNT = 41;
static const int   CELL_W = 6, CELL_H = 8;      // 5x7 glyph + 1 column / 1 row of blank

static const char* GLYPHS[GLYPH_COUNT][7] = {
    {"00000","00000","00000","00000","00000","00000","00000"},   // space
    {"01110","10001","10011","10101","11001","10001","01110"},   // 0
    {"00100","01100","00100","00100","00100","00100","01110"},   // 1
    {"01110","10001","00001","00010","00100","01000","11111"},   // 2
    {"11110","00001","00001","01110","00001","00001","11110"},   // 3
    {"00010","00110","01010","10010","11111","00010","00010"},   // 4
    {"11111","10000","11110","00001","00001","10001","01110"},   // 5
    {"00110","01000","10000","11110","10001","10001","01110"},   // 6
    {"11111","00001","00010","00100","01000","01000","01000"},   // 7
    {"01110","10001","10001","01110","10001","10001","01110"},   // 8
    {"01110","10001","10001","01111","00001","00010","01100"},   // 9
    {"00000","00000","00000","00000","00000","01100","01100"},   // .
    {"00000","01100","01100","00000","01100","01100","00000"},   // :
    {"00000","00000","00000","11111","00000","00000","00000"},   // -
    {"11000","11001","00010","00100","01000","10011","00011"},   // %
    {"01110","10001","10001","11111","10001","10001","10001"},   // A
    {"11110","10001","10001","11110","10001","10001","11110"},   // B
    {"01110","10001","10000","10000","10000","10001","01110"},   // C
    {"11110","10001","10001","10001","10001","10001","11110"},   // D
    {"11111","10000","10000","11110","10000","10000","11111"},   // E
    {"11111","10000","10000","11110","10000","10000","10000"},   // F
    {"01110","10001","10000","10111","10001","10001","01111"},   // G
    {"10001","10001","10001","11111","10001","10001","10001"},   // H
    {"01110","00100","00100","00100","00100","00100","01110"},   // I
    {"00111","00010","00010","00010","00010","10010","01100"},   // J
    {"10001","10010","10100","11000","10100","10010","10001"},   // K
    {"10000","10000","10000","10000","10000","10000","11111"},   // L
    {"10001","11011","10101","10101","10001","10001","10001"},   // M
    {"10001","11001","10101","10011","10001","10001","10001"},   // N
    {"01110","10001","10001","10001","10001","10001","01110"},   // O
    {"11110","10001","10001","11110","10000","10000","10000"},   // P
    {"01110","10001","10001","10001","10101","10010","01101"},   // Q
    {"11110","10001","10001","11110","10100","10010","10001"},   // R
    {"01111","10000","10000","01110","00001","00001","11110"},   // S
    {"11111","00100","00100","00100","00100","00100","00100"},   // T
    {"10001","10001","10001","10001","10001","10001","01110"},   // U
    {"10001","10001","10001","10001","10001","01010","00100"},   // V
    {"10001","10001","10001","10101","10101","10101","01010"},   // W
    {"10001","10001","01010","00100","01010","10001","10001"},   // X
    {"10001","10001","01010","00100","00100","00100","00100"},   // Y
    {"11111","00001","00010","00100","01000","10000","11111"},   // Z
};

bool Hud::Init()
{
    if (!shader.CreateFromFiles("Shaders/hud.vert", "Shaders/hud.frag"))
        return false;

    // ---- build the digit/letter strip: GLYPH_COUNT cells side by side ----
    int W = GLYPH_COUNT * CELL_W, H = CELL_H;
    std::vector<unsigned char> px((size_t)W * H * 4, 0);
    for (int g = 0; g < GLYPH_COUNT; ++g)
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (GLYPHS[g][row][col] == '1')
                {
                    size_t o = ((size_t)row * W + (g * CELL_W + col)) * 4;
                    px[o] = px[o + 1] = px[o + 2] = px[o + 3] = 255;
                }
    glGenTextures(1, &strip);
    glBindTexture(GL_TEXTURE_2D, strip);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ---- unit quad, indexed ----
    float verts[] = {          // pos.xy     uv   (v flipped so row 0 of the strip is the TOP)
        0.0f, 0.0f,   0.0f, 1.0f,
        1.0f, 0.0f,   1.0f, 1.0f,
        1.0f, 1.0f,   1.0f, 0.0f,
        0.0f, 1.0f,   0.0f, 0.0f };
    unsigned int idx[] = { 0, 1, 2, 0, 2, 3 };
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return true;
}

void Hud::Begin(int width, int height)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader.Use();
    glm::mat4 ortho = glm::ortho(0.0f, (GLfloat)width, 0.0f, (GLfloat)height, -1.0f, 1.0f);
    shader.SetMat4("projection", ortho);
    shader.SetInt("hudTexture", 9);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, strip);
    glBindVertexArray(vao);
}

void Hud::Rect(float x, float y, float w, float h, const glm::vec4& colour)
{
    shader.SetVec4("rect", glm::vec4(x, y, w, h));
    shader.SetVec4("uvRect", glm::vec4(0, 0, 1, 1));
    shader.SetVec4("tint", colour);
    shader.SetInt("useTexture", 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

float Hud::TextWidth(const std::string& s, int scale) const
{
    return (float)(s.size() * CELL_W * scale);
}

void Hud::Text(const std::string& s, float x, float y, int scale, const glm::vec4& colour)
{
    shader.SetVec4("tint", colour);
    shader.SetInt("useTexture", 1);
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = (char)std::toupper((unsigned char)s[i]);
        const char* p = std::strchr(CHARSET, c);
        int index = (p && c != '\0') ? (int)(p - CHARSET) : 0;
        shader.SetVec4("rect", glm::vec4(x + (float)(i * CELL_W * scale), y, (float)(CELL_W * scale), (float)(CELL_H * scale)));
        shader.SetVec4("uvRect", glm::vec4((float)index / GLYPH_COUNT, 0.0f, 1.0f / GLYPH_COUNT, 1.0f));
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }
}

void Hud::End()
{
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
