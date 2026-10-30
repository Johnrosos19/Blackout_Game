#include "Shader.h"
#include "Paths.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string Shader::versionOverride;

static bool ReadWholeFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool Shader::CompileStage(GLuint& shader, GLenum type, const std::string& path)
{
    std::string src;
    if (!ReadWholeFile(ResolveAsset(path), src))
    {
        std::cerr << "[SHADER] cannot open file: " << path << "\n";
        return false;
    }
    if (!versionOverride.empty() && src.compare(0, 8, "#version") == 0)
    {
        size_t nl = src.find('\n');
        src = versionOverride + (nl == std::string::npos ? std::string() : src.substr(nl));
    }

    shader = glCreateShader(type);
    const char* s = src.c_str();
    glShaderSource(shader, 1, &s, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[SHADER] compile failed: " << path << "\n" << log << "\n";
        glDeleteShader(shader);
        shader = 0;
        return false;
    }
    return true;
}

bool Shader::CreateFromFiles(const std::string& vert, const std::string& frag, const std::string& geom)
{
    Clear();
    GLuint vs = 0, fs = 0, gs = 0;
    bool ok = CompileStage(vs, GL_VERTEX_SHADER, vert) && CompileStage(fs, GL_FRAGMENT_SHADER, frag);
    if (ok && !geom.empty()) ok = CompileStage(gs, GL_GEOMETRY_SHADER, geom);
    if (!ok)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (gs) glDeleteShader(gs);
        return false;
    }

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    if (gs) glAttachShader(program, gs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (gs) glDeleteShader(gs);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[4096];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[SHADER] link failed (" << vert << "): " << log << "\n";
        glDeleteProgram(program);
        program = 0;
        return false;
    }
    return true;
}

void Shader::Clear()
{
    if (program) glDeleteProgram(program);
    program = 0;
    cache.clear();
}

GLint Shader::Loc(const std::string& name)
{
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    GLint l = glGetUniformLocation(program, name.c_str());
    cache[name] = l;
    return l;
}

void Shader::SetInt(const std::string& n, int v)                   { glProgramUniform1i(program, Loc(n), v); }
void Shader::SetFloat(const std::string& n, float v)               { glProgramUniform1f(program, Loc(n), v); }
void Shader::SetVec3(const std::string& n, const glm::vec3& v)     { glProgramUniform3f(program, Loc(n), v.x, v.y, v.z); }
void Shader::SetVec4(const std::string& n, const glm::vec4& v)     { glProgramUniform4f(program, Loc(n), v.x, v.y, v.z, v.w); }
void Shader::SetMat4(const std::string& n, const glm::mat4& m)     { glProgramUniformMatrix4fv(program, Loc(n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::SetMat4Array(const std::string& n, const glm::mat4* m, int count)
{
    glProgramUniformMatrix4fv(program, Loc(n), count, GL_FALSE, glm::value_ptr(m[0]));
}
