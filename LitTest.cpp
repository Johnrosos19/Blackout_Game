#include "LitTest.h"
#include <cmath>
#include <algorithm>

static const glm::vec3 LUMA(0.2126f, 0.7152f, 0.0722f);

float BulbAttenuation(const Scene& sc, float d)
{
    return sc.attE * d * d + sc.attL * d + sc.attC;
}

// slab test of the segment a->b against an axis-aligned box
static bool SegmentHitsBox(const glm::vec3& a, const glm::vec3& b, const glm::vec3& mn, const glm::vec3& mx)
{
    glm::vec3 d = b - a;
    float t0 = 0.0f, t1 = 1.0f;
    for (int k = 0; k < 3; ++k)
    {
        if (std::fabs(d[k]) < 1e-9f)
        {
            if (a[k] < mn[k] || a[k] > mx[k]) return false;
        }
        else
        {
            float inv = 1.0f / d[k];
            float ta = (mn[k] - a[k]) * inv, tb = (mx[k] - a[k]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
    }
    return true;
}

bool SegmentBlocked(const Scene& sc, const glm::vec3& a, const glm::vec3& b)
{
    // the blockers are EXACTLY the objects that are drawn into the cube maps
    for (const auto& o : sc.objects)
        if (o.castShadow && SegmentHitsBox(a, b, o.minB, o.maxB)) return true;
    return false;
}

LitResult ComputeIllumination(const Scene& sc, const glm::vec3& p, bool lineOfSight)
{
    LitResult r;
    for (size_t i = 0; i < sc.bulbs.size(); ++i)
    {
        const Bulb& b = sc.bulbs[i];
        float d = glm::length(p - b.position);
        if (d > sc.bulbFar) continue;
        if (lineOfSight && SegmentBlocked(sc, p, b.position)) continue;
        r.visMask |= (1u << i);
        r.illum += glm::dot(b.colour, LUMA) * b.intensity / BulbAttenuation(sc, d);
    }
    return r;
}
