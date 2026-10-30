#pragma once
// ---------------------------------------------------------------------------
// Shader.h / Shader.cpp - compile + link a program from files (owner: R2)
// ---------------------------------------------------------------------------
#include "CommonValues.h"
#include <string>
#include <unordered_map>

class Shader
{
public:
    Shader() = default;
    ~Shader() { Clear(); }
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // geom is optional (pass "" for none)
    bool CreateFromFiles(const std::string& vert, const std::string& frag, const std::string& geom = "");
    void Use() const { glUseProgram(program); }
    void Clear();
    GLuint Id() const { return program; }

    // These use glProgramUniform*, so the program does not have to be bound.
    void SetInt  (const std::string& name, int v);
    void SetFloat(const std::string& name, float v);
    void SetVec3 (const std::string& name, const glm::vec3& v);
    void SetVec4 (const std::string& name, const glm::vec4& v);
    void SetMat4 (const std::string& name, const glm::mat4& m);
    void SetMat4Array(const std::string& name, const glm::mat4* m, int count);

    // Used when the driver only offers e.g. GL 4.5: rewrites the first line of
    // every shader ("#version 460 core" -> "#version 450 core").
    static void SetVersionLine(const std::string& line) { versionOverride = line; }

private:
    GLint Loc(const std::string& name);
    static bool CompileStage(GLuint& out, GLenum type, const std::string& path);

    GLuint program = 0;
    std::unordered_map<std::string, GLint> cache;
    static std::string versionOverride;
};
