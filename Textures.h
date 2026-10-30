#pragma once
// ---------------------------------------------------------------------------
// Textures.h / Textures.cpp (owner: R3)
//
// There is no image-library dependency. A texture "spec" in the scene file is
//     proc:<name>      generated in code (concrete, brick, metal, wood, ...)
//     path/to/file.bmp 24/32-bit uncompressed BMP, or P6 .ppm
// A missing or unreadable file is REPORTED and replaced with a magenta checker
// so the game still runs and the fault is obvious on screen (never silent black).
// ---------------------------------------------------------------------------
#include "CommonValues.h"
#include <string>
#include <vector>
#include <map>

bool LoadImageFile(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba, std::string& err);
bool GenerateProcedural(const std::string& name, int& w, int& h, std::vector<unsigned char>& rgba);
GLuint CreateTexture2D(const unsigned char* rgba, int w, int h, bool mipmaps, bool nearest);

// faceFiles: 6 paths in +X,-X,+Y,-Y,+Z,-Z order, or empty for the procedural night sky
GLuint CreateSkyCube(const std::vector<std::string>& faceFiles, int& missingCount);

class TextureManager
{
public:
    GLuint Get(const std::string& spec);        // never returns 0
    int    MissingCount() const { return missing; }
private:
    std::map<std::string, GLuint> cache;
    int missing = 0;
};
