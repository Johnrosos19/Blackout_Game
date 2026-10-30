#include "Textures.h"
#include "Paths.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <iterator>

// ----------------------------------------------------------------------------
// tiny deterministic noise
// ----------------------------------------------------------------------------
static float Hash2(int x, int y, int seed = 0)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return (float)(h & 0xFFFFu) / 65535.0f;
}

// tileable value noise: lattice coordinates wrap at 'period'
static float VNoise(float x, float y, int period, int seed = 0)
{
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto wrap = [&](int v) { v %= period; return v < 0 ? v + period : v; };
    float a = Hash2(wrap(xi),     wrap(yi),     seed);
    float b = Hash2(wrap(xi + 1), wrap(yi),     seed);
    float c = Hash2(wrap(xi),     wrap(yi + 1), seed);
    float d = Hash2(wrap(xi + 1), wrap(yi + 1), seed);
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

static float FBm(float u, float v, int basePeriod, int seed = 0)   // u,v in 0..1
{
    float sum = 0, amp = 0.5f, norm = 0;
    int period = basePeriod;
    for (int o = 0; o < 4; ++o)
    {
        sum += amp * VNoise(u * period, v * period, period, seed + o);
        norm += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / norm;
}

static void Put(std::vector<unsigned char>& px, int w, int x, int y, float r, float g, float b, float a = 1.0f)
{
    auto cl = [](float v) { return (unsigned char)(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f); };
    size_t i = ((size_t)y * w + x) * 4;
    px[i] = cl(r); px[i + 1] = cl(g); px[i + 2] = cl(b); px[i + 3] = cl(a);
}

// ----------------------------------------------------------------------------
// procedural textures (all 256x256, all tileable)
// ----------------------------------------------------------------------------
bool GenerateProcedural(const std::string& name, int& w, int& h, std::vector<unsigned char>& px)
{
    w = h = 256;
    px.assign((size_t)w * h * 4, 255);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float u = (float)x / w, v = (float)y / h;

            if (name == "concrete")
            {
                float n = FBm(u, v, 6, 1);
                float speck = Hash2(x, y, 7) * 0.08f;
                float g = 0.50f + 0.28f * (n - 0.5f) + speck;
                Put(px, w, x, y, g, g * 0.99f, g * 0.96f);
            }
            else if (name == "asphalt")
            {
                float n = FBm(u, v, 8, 3);
                float g = 0.20f + 0.14f * (n - 0.5f) + Hash2(x, y, 5) * 0.10f;
                Put(px, w, x, y, g, g, g * 1.05f);
            }
            else if (name == "brick")
            {
                const int rowH = 32, brickW = 64, mortar = 4;
                int row = y / rowH;
                int xo = (x + (row & 1) * (brickW / 2)) % w;
                int col = xo / brickW;
                bool isMortar = ((y % rowH) < mortar) || ((xo % brickW) < mortar);
                if (isMortar)
                {
                    float g = 0.62f + 0.10f * Hash2(x, y, 2);
                    Put(px, w, x, y, g, g * 0.97f, g * 0.92f);
                }
                else
                {
                    float tint = Hash2(col, row, 11);
                    float n = FBm(u, v, 16, 4);
                    float r = 0.52f + 0.20f * tint + 0.10f * (n - 0.5f);
                    Put(px, w, x, y, r, r * (0.38f + 0.1f * tint), r * 0.30f);
                }
            }
            else if (name == "metal")
            {
                // corrugated container: vertical ridges + brushed streaks + dark panel seams
                float ridge = 0.5f + 0.5f * std::sin(u * 2.0f * PI_F * 16.0f);
                float streak = Hash2(0, y, 9) * 0.08f + FBm(u * 0.1f, v, 8, 6) * 0.06f;
                float g = 0.30f + 0.22f * ridge + streak;
                bool seam = (y % 128) < 3 || (y % 128) > 124;
                if (seam) g *= 0.35f;
                Put(px, w, x, y, g * 0.55f, g * 0.80f, g * 1.15f);
            }
            else if (name == "wood")
            {
                int plank = x / 64;
                bool gap = (x % 64) < 2;
                float wob = FBm(u, v, 4, 20 + plank);
                float grain = 0.5f + 0.5f * std::sin((v * 14.0f + wob * 6.0f + plank * 1.7f) * PI_F);
                float g = 0.65f + 0.35f * grain;
                float r = 0.58f * g, gg = 0.38f * g, b = 0.20f * g;
                if (gap) { r *= 0.3f; gg *= 0.3f; b *= 0.3f; }
                Put(px, w, x, y, r, gg, b);
            }
            else if (name == "hazard")
            {
                bool yellow = (((x + y) / 32) & 1) == 0;
                float n = Hash2(x, y, 4) * 0.05f;
                if (yellow) Put(px, w, x, y, 0.95f + n, 0.78f + n, 0.05f);
                else        Put(px, w, x, y, 0.06f + n, 0.06f + n, 0.06f + n);
            }
            else if (name == "exitpad")
            {
                bool line = (x % 64) < 3 || (y % 64) < 3;
                float n = FBm(u, v, 8, 8);
                if (line) Put(px, w, x, y, 0.75f, 1.0f, 0.80f);
                else      Put(px, w, x, y, 0.05f, 0.45f + 0.25f * n, 0.12f);
            }
            else if (name == "white")
            {
                Put(px, w, x, y, 1, 1, 1);
            }
            else if (name == "checker")       // the "something is missing" texture
            {
                bool c = (((x / 32) + (y / 32)) & 1) == 0;
                if (c) Put(px, w, x, y, 1.0f, 0.0f, 1.0f);
                else   Put(px, w, x, y, 0.1f, 0.0f, 0.1f);
            }
            else
            {
                return false;
            }
        }
    return true;
}

GLuint CreateTexture2D(const unsigned char* rgba, int w, int h, bool mipmaps, bool nearest)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (nearest)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

// ----------------------------------------------------------------------------
// BMP (24/32-bit, uncompressed) and PPM (P6) loaders
// ----------------------------------------------------------------------------
static bool LoadBMP(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open file"; return false; }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') { err = "not a BMP file"; return false; }
    auto rd32 = [&](size_t o) { return (int32_t)(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24)); };
    auto rd16 = [&](size_t o) { return (int)(d[o] | (d[o + 1] << 8)); };
    int dataOff = rd32(10), width = rd32(18), height = rd32(22), bpp = rd16(28), comp = rd32(30);
    if (comp != 0 || (bpp != 24 && bpp != 32)) { err = "BMP must be uncompressed 24 or 32 bit"; return false; }
    bool topDown = height < 0;
    if (topDown) height = -height;
    if (width <= 0 || height <= 0) { err = "bad BMP size"; return false; }
    int bytes = bpp / 8;
    size_t rowSize = (((size_t)width * bpp + 31) / 32) * 4;
    if ((size_t)dataOff + rowSize * height > d.size()) { err = "BMP data truncated"; return false; }
    w = width; h = height;
    rgba.assign((size_t)w * h * 4, 255);
    for (int y = 0; y < h; ++y)
    {
        int srcRow = topDown ? y : (h - 1 - y);
        const unsigned char* src = &d[dataOff + rowSize * srcRow];
        for (int x = 0; x < w; ++x)
        {
            size_t o = ((size_t)y * w + x) * 4;
            rgba[o] = src[x * bytes + 2]; rgba[o + 1] = src[x * bytes + 1]; rgba[o + 2] = src[x * bytes];
            rgba[o + 3] = (bytes == 4) ? src[x * bytes + 3] : 255;
        }
    }
    return true;
}

static bool LoadPPM(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open file"; return false; }
    std::string magic;
    f >> magic;
    if (magic != "P6") { err = "only binary P6 PPM is supported"; return false; }
    auto skipComments = [&]() { while (f.peek() == '#') { std::string junk; std::getline(f, junk); } };
    int maxv = 0;
    skipComments(); f >> w;
    skipComments(); f >> h;
    skipComments(); f >> maxv;
    f.get();
    if (w <= 0 || h <= 0 || maxv != 255) { err = "bad PPM header"; return false; }
    std::vector<unsigned char> rgb((size_t)w * h * 3);
    f.read((char*)rgb.data(), rgb.size());
    if ((size_t)f.gcount() != rgb.size()) { err = "PPM data truncated"; return false; }
    rgba.assign((size_t)w * h * 4, 255);
    for (size_t i = 0; i < (size_t)w * h; ++i)
    {
        rgba[i * 4] = rgb[i * 3]; rgba[i * 4 + 1] = rgb[i * 3 + 1]; rgba[i * 4 + 2] = rgb[i * 3 + 2];
    }
    return true;
}

bool LoadImageFile(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba, std::string& err)
{
    std::string p = ResolveAsset(path);
    std::string ext = p.size() > 4 ? p.substr(p.size() - 4) : "";
    for (auto& c : ext) c = (char)tolower(c);
    if (ext == ".bmp") return LoadBMP(p, w, h, rgba, err);
    if (ext == ".ppm") return LoadPPM(p, w, h, rgba, err);
    err = "unsupported image type (use .bmp or .ppm, or proc:<name>)";
    return false;
}

GLuint TextureManager::Get(const std::string& spec)
{
    auto it = cache.find(spec);
    if (it != cache.end()) return it->second;

    int w = 0, h = 0;
    std::vector<unsigned char> px;
    bool ok = false;
    std::string err;
    if (spec.compare(0, 5, "proc:") == 0)
    {
        ok = GenerateProcedural(spec.substr(5), w, h, px);
        if (!ok) err = "unknown procedural texture '" + spec.substr(5) + "'";
    }
    else
    {
        ok = LoadImageFile(spec, w, h, px, err);
    }

    if (!ok)
    {
        std::cerr << "[ASSET MISSING] texture '" << spec << "': " << err << "  -> using magenta checker\n";
        ++missing;
        GenerateProcedural("checker", w, h, px);
    }
    GLuint t = CreateTexture2D(px.data(), w, h, true, false);
    cache[spec] = t;
    return t;
}

// ----------------------------------------------------------------------------
// Sky cube map. The procedural sky is computed from the DIRECTION of each texel
// using the OpenGL cube-map face table, so the six faces always line up.
// Face order: +X, -X, +Y, -Y, +Z, -Z  (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i)
// ----------------------------------------------------------------------------
static glm::vec3 FaceDirection(int face, float u, float v)   // u,v in -1..1, v grows with the row index
{
    switch (face)
    {
    case 0:  return glm::vec3( 1, -v, -u);
    case 1:  return glm::vec3(-1, -v,  u);
    case 2:  return glm::vec3( u,  1,  v);
    case 3:  return glm::vec3( u, -1, -v);
    case 4:  return glm::vec3( u, -v,  1);
    default: return glm::vec3(-u, -v, -1);
    }
}

static void NightSkyFace(int face, int S, std::vector<unsigned char>& px)
{
    px.assign((size_t)S * S * 4, 255);
    const glm::vec3 moonDir = glm::normalize(glm::vec3(0.4f, 0.7f, 0.5f));    // opposite of the dirlight direction in level1.txt
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            float u = 2.0f * (x + 0.5f) / S - 1.0f, v = 2.0f * (y + 0.5f) / S - 1.0f;
            glm::vec3 d = glm::normalize(FaceDirection(face, u, v));

            float t = glm::clamp(d.y * 0.5f + 0.5f, 0.0f, 1.0f);
            glm::vec3 horizon(0.10f, 0.11f, 0.19f), zenith(0.01f, 0.02f, 0.07f);
            glm::vec3 c = glm::mix(horizon, zenith, std::pow(t, 0.6f));

            float star = Hash2(face * 1000 + x, y, 99);
            if (star > 0.9965f) { float b = 0.5f + 0.5f * Hash2(x, y + face, 3); c += glm::vec3(b); }

            float m = glm::dot(d, moonDir);
            if (m > 0.9985f)      c = glm::vec3(0.95f, 0.95f, 0.85f);
            else if (m > 0.97f)   c += glm::vec3(0.10f, 0.11f, 0.15f) * ((m - 0.97f) / 0.0285f);

            Put(px, S, x, y, c.r, c.g, c.b);
        }
}

GLuint CreateSkyCube(const std::vector<std::string>& faceFiles, int& missingCount)
{
    std::vector<std::vector<unsigned char>> faces(6);
    int S = 256;
    bool fromFiles = (faceFiles.size() == 6);
    if (fromFiles)
    {
        int w0 = 0, h0 = 0;
        for (int i = 0; i < 6 && fromFiles; ++i)
        {
            int w, h; std::string err;
            if (!LoadImageFile(faceFiles[i], w, h, faces[i], err))
            {
                std::cerr << "[ASSET MISSING] sky face " << i << " '" << faceFiles[i] << "': " << err
                          << "  -> using the procedural night sky\n";
                ++missingCount; fromFiles = false;
            }
            else if (i == 0) { w0 = w; h0 = h; }
            else if (w != w0 || h != h0 || w != h)
            {
                std::cerr << "[ASSET] sky faces must all be the same square size -> using the procedural night sky\n";
                ++missingCount; fromFiles = false;
            }
        }
        if (fromFiles) S = w0;
    }
    if (!fromFiles)
    {
        S = 256;
        for (int i = 0; i < 6; ++i) NightSkyFace(i, S, faces[i]);
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, faces[i].data());
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return tex;
}
