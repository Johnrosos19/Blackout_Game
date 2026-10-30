#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// ---------------------------------------------------------------------------
// CommonValues.h  -  shared includes and constants (owner: R1)
// ---------------------------------------------------------------------------
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

constexpr int   MAX_BULBS     = 4;        // must match MAX_POINT_LIGHTS in shader.frag
constexpr float PI_F          = 3.14159265358979f;
constexpr float PLAYER_RADIUS = 0.35f;    // collision circle on the ground plane
constexpr float SAMPLE_HEIGHT = 0.01f;    // height above the floor of the CPU lit-test point
