#pragma once
// ---------------------------------------------------------------------------
// LitTest.h / LitTest.cpp - THE CPU ANSWER TO "IS THE PLAYER LIT?"   (R4 + R2)
//
// For every bulb i, with d = |p - bulb_i|:
//
//   visible_i = (d <= farPlane) AND (segment p -> bulb_i is not blocked by any
//               shadow-casting box)
//   att_i     = exponent*d*d + linear*d + constant        (same as shader.frag)
//   illum     = sum_i visible_i * luma(colour_i) * intensity_i / att_i
//   lit       = illum >= litThreshold
//
// There is deliberately NO normal (N.L) term: the player is a capsule, not a flat
// face, and the gameplay question is "can the bulb see me", not "how much does
// this surface point toward it".  shader.frag computes the identical 'illum'
// in debugMode 2 so the two answers can be compared pixel by pixel (see P key).
// ---------------------------------------------------------------------------
#include "Scene.h"

struct LitResult
{
    float illum = 0.0f;          // summed illumination
    unsigned int visMask = 0;    // bit i set = bulb i can see the point
};

float BulbAttenuation(const Scene& sc, float d);
bool  SegmentBlocked(const Scene& sc, const glm::vec3& a, const glm::vec3& b);
LitResult ComputeIllumination(const Scene& sc, const glm::vec3& p, bool lineOfSight = true);
