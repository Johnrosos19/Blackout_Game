#pragma once
// ---------------------------------------------------------------------------
// Mesh.h / Mesh.cpp - indexed geometry written by hand (no model loading).
// Every shape is drawn with glDrawElements (B2).
// Vertex layout: position(3) normal(3) uv(2)
//
// A lit, textured box has 24 vertices, not 8: each corner is shared by three
// faces that need three different normals, so it is split three ways.
// The skybox cube really does use 8 (position is the texture coordinate).
// ---------------------------------------------------------------------------
#include "CommonValues.h"
#include <vector>
#include <memory>

class Mesh
{
public:
    Mesh() = default;
    ~Mesh() { Clear(); }
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void Create(const std::vector<float>& vertices, const std::vector<unsigned int>& indices);
    void Render() const;
    void Clear();

    int VertexCount()   const { return vertexCount; }
    int TriangleCount() const { return indexCount / 3; }

private:
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei indexCount = 0;
    int vertexCount = 0;
};

std::shared_ptr<Mesh> MakeBox(float sx, float sy, float sz, float tileSize);   // centred on origin
std::shared_ptr<Mesh> MakeSphere(float radius, int slices, int stacks);
std::shared_ptr<Mesh> MakeCylinder(float radius, float height, int slices);    // base on y = 0
std::shared_ptr<Mesh> MakeSkyCube();                                            // 8 vertices
