#include "Mesh.h"
#include <cmath>

void Mesh::Create(const std::vector<float>& vertices, const std::vector<unsigned int>& indices)
{
    Clear();
    indexCount  = (GLsizei)indices.size();
    vertexCount = (int)(vertices.size() / 8);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    const GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Mesh::Render() const
{
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::Clear()
{
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    ibo = vbo = vao = 0;
    indexCount = 0;
    vertexCount = 0;
}

static void PushVertex(std::vector<float>& v, const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv)
{
    v.push_back(p.x);  v.push_back(p.y);  v.push_back(p.z);
    v.push_back(n.x);  v.push_back(n.y);  v.push_back(n.z);
    v.push_back(uv.x); v.push_back(uv.y);
}

std::shared_ptr<Mesh> MakeBox(float sx, float sy, float sz, float tileSize)
{
    const glm::vec3 h(sx * 0.5f, sy * 0.5f, sz * 0.5f);
    struct Face { glm::vec3 n, u, v; };
    // u x v == n for every face, so (-u-v, +u-v, +u+v, -u+v) is counter-clockwise from outside
    const Face faces[6] = {
        { { 1, 0, 0}, { 0, 0,-1}, { 0, 1, 0} },
        { {-1, 0, 0}, { 0, 0, 1}, { 0, 1, 0} },
        { { 0, 1, 0}, { 1, 0, 0}, { 0, 0,-1} },
        { { 0,-1, 0}, { 1, 0, 0}, { 0, 0, 1} },
        { { 0, 0, 1}, { 1, 0, 0}, { 0, 1, 0} },
        { { 0, 0,-1}, {-1, 0, 0}, { 0, 1, 0} },
    };
    std::vector<float> verts;
    std::vector<unsigned int> idx;
    for (int f = 0; f < 6; ++f)
    {
        const Face& F = faces[f];
        float hn = std::fabs(F.n.x) * h.x + std::fabs(F.n.y) * h.y + std::fabs(F.n.z) * h.z;
        float hu = std::fabs(F.u.x) * h.x + std::fabs(F.u.y) * h.y + std::fabs(F.u.z) * h.z;
        float hv = std::fabs(F.v.x) * h.x + std::fabs(F.v.y) * h.y + std::fabs(F.v.z) * h.z;
        float su = 2.0f * hu / tileSize, sv = 2.0f * hv / tileSize;   // texture repeats every tileSize world units
        glm::vec3 c = F.n * hn;
        unsigned int base = (unsigned int)(verts.size() / 8);
        PushVertex(verts, c - F.u * hu - F.v * hv, F.n, { 0,  0 });
        PushVertex(verts, c + F.u * hu - F.v * hv, F.n, { su, 0 });
        PushVertex(verts, c + F.u * hu + F.v * hv, F.n, { su, sv });
        PushVertex(verts, c - F.u * hu + F.v * hv, F.n, { 0,  sv });
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
    auto m = std::make_shared<Mesh>();
    m->Create(verts, idx);
    return m;
}

std::shared_ptr<Mesh> MakeSphere(float radius, int slices, int stacks)
{
    std::vector<float> verts;
    std::vector<unsigned int> idx;
    for (int i = 0; i <= stacks; ++i)
    {
        float phi = PI_F * (float)i / (float)stacks;
        for (int j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * PI_F * (float)j / (float)slices;
            glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            PushVertex(verts, n * radius, n, { (float)j / slices, (float)i / stacks });
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j)
        {
            unsigned int a = i * (slices + 1) + j;
            unsigned int b = a + slices + 1;
            idx.insert(idx.end(), { a, a + 1, b, a + 1, b + 1, b });
        }
    auto m = std::make_shared<Mesh>();
    m->Create(verts, idx);
    return m;
}

std::shared_ptr<Mesh> MakeCylinder(float radius, float height, int slices)
{
    std::vector<float> verts;
    std::vector<unsigned int> idx;

    // side
    for (int j = 0; j <= slices; ++j)
    {
        float t = 2.0f * PI_F * (float)j / (float)slices;
        glm::vec3 n(std::cos(t), 0, std::sin(t));
        PushVertex(verts, n * radius, n, { (float)j / slices, 0 });
        PushVertex(verts, n * radius + glm::vec3(0, height, 0), n, { (float)j / slices, 1 });
    }
    for (int j = 0; j < slices; ++j)
    {
        unsigned int a = 2 * j, b = a + 1, a2 = a + 2, b2 = a + 3;
        idx.insert(idx.end(), { a, b, a2, a2, b, b2 });
    }
    // caps
    for (int cap = 0; cap < 2; ++cap)
    {
        float y = cap ? height : 0.0f;
        glm::vec3 n(0, cap ? 1.0f : -1.0f, 0);
        unsigned int centre = (unsigned int)(verts.size() / 8);
        PushVertex(verts, { 0, y, 0 }, n, { 0.5f, 0.5f });
        for (int j = 0; j <= slices; ++j)
        {
            float t = 2.0f * PI_F * (float)j / (float)slices;
            PushVertex(verts, { radius * std::cos(t), y, radius * std::sin(t) }, n,
                       { 0.5f + 0.5f * std::cos(t), 0.5f + 0.5f * std::sin(t) });
        }
        for (int j = 0; j < slices; ++j)
        {
            unsigned int p0 = centre + 1 + j, p1 = p0 + 1;
            if (cap) idx.insert(idx.end(), { centre, p1, p0 });
            else     idx.insert(idx.end(), { centre, p0, p1 });
        }
    }
    auto m = std::make_shared<Mesh>();
    m->Create(verts, idx);
    return m;
}

std::shared_ptr<Mesh> MakeSkyCube()
{
    std::vector<float> verts;
    for (int i = 0; i < 8; ++i)
    {
        glm::vec3 p((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
        PushVertex(verts, p, { 0, 0, 0 }, { 0, 0 });
    }
    // 12 triangles (index buffer re-uses the 8 corners); culling is disabled when drawing the sky
    std::vector<unsigned int> idx = {
        0,1,3, 0,3,2,   4,6,7, 4,7,5,   0,2,6, 0,6,4,
        1,5,7, 1,7,3,   0,4,5, 0,5,1,   2,3,7, 2,7,6 };
    auto m = std::make_shared<Mesh>();
    m->Create(verts, idx);
    return m;
}
