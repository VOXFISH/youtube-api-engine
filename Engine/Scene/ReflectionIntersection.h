#pragma once

namespace Rice {
inline const char* ReflectionIntersectionGLSL = R"(
uniform samplerBuffer reflectionGeometry;
uniform int reflectionGroupCount;
uniform int reflectionReceiverIndex;
uniform int reflectionProbeIndex;
uniform vec3 reflectionCapturePosition;
uniform int reflectionGroundEnabled;

const float reflectionMinDistance = 0.0001;
const float reflectionMaxDistance = 1000.0;

bool reflectionPositiveRoot(float distance) {
    return distance > reflectionMinDistance && distance < reflectionMaxDistance;
}

void reflectionKeepHit(float candidate, inout float nearest) {
    if (reflectionPositiveRoot(candidate)) nearest = min(nearest, candidate);
}

// Local directions retain their scale so every distance remains a world-ray parameter.
float reflectionBoxHit(vec3 origin, vec3 direction) {
    float entry = -reflectionMaxDistance;
    float exit = reflectionMaxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        if (abs(direction[axis]) < 0.00000001) {
            if (abs(origin[axis]) > 0.5) return reflectionMaxDistance;
        } else {
            float first = (-0.5 - origin[axis]) / direction[axis];
            float second = (0.5 - origin[axis]) / direction[axis];
            entry = max(entry, min(first, second));
            exit = min(exit, max(first, second));
            if (entry > exit) return reflectionMaxDistance;
        }
    }
    if (reflectionPositiveRoot(entry)) return entry;
    if (reflectionPositiveRoot(exit)) return exit;
    return reflectionMaxDistance;
}

// Return both roots, including the linear case for a cone's generator direction.
bool reflectionQuadratic(float a, float b, float c, out vec2 roots) {
    float scale = max(abs(a), max(abs(b), abs(c)));
    if (scale == 0.0) return false;
    a /= scale;
    b /= scale;
    c /= scale;
    if (abs(a) < 0.00000001) {
        if (abs(b) < 0.00000001) return false;
        roots = vec2(-c / b);
        return true;
    }
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return false;
    float radical = sqrt(discriminant);
    float q = -0.5 * (b + (b >= 0.0 ? radical : -radical));
    if (abs(q) < 0.00000001) {
        roots = vec2(-b / (2.0 * a));
    } else {
        roots = vec2(q / a, c / q);
    }
    return true;
}

float reflectionPlaneHit(vec3 origin, vec3 direction, bool circular) {
    if (abs(direction.z) < 0.00000001) return reflectionMaxDistance;
    float distance = -origin.z / direction.z;
    vec2 point = origin.xy + direction.xy * distance;
    bool inside = circular ? dot(point, point) <= 0.25 : max(abs(point.x), abs(point.y)) <= 0.5;
    if (inside && reflectionPositiveRoot(distance)) return distance;
    return reflectionMaxDistance;
}

float reflectionSphereHit(vec3 origin, vec3 direction) {
    vec2 roots;
    if (!reflectionQuadratic(dot(direction, direction), 2.0 * dot(origin, direction),
                             dot(origin, origin) - 0.25, roots)) return reflectionMaxDistance;
    float nearest = reflectionMaxDistance;
    reflectionKeepHit(roots.x, nearest);
    reflectionKeepHit(roots.y, nearest);
    return nearest;
}

void reflectionKeepSideHit(vec3 origin, vec3 direction, float candidate, inout float nearest) {
    float height = origin.y + direction.y * candidate;
    if (height >= -0.5 && height <= 0.5) reflectionKeepHit(candidate, nearest);
}

void reflectionKeepCapHit(vec3 origin, vec3 direction, float height, inout float nearest) {
    if (abs(direction.y) < 0.00000001) return;
    float candidate = (height - origin.y) / direction.y;
    vec2 point = origin.xz + direction.xz * candidate;
    if (dot(point, point) <= 0.25) reflectionKeepHit(candidate, nearest);
}

float reflectionRoundSolidHit(vec3 origin, vec3 direction, bool conical) {
    float a = dot(direction.xz, direction.xz);
    float b = 2.0 * dot(origin.xz, direction.xz);
    float c = dot(origin.xz, origin.xz) - 0.25;
    if (conical) {
        float radius = 0.25 - 0.5 * origin.y;
        a -= 0.25 * direction.y * direction.y;
        b += radius * direction.y;
        c = dot(origin.xz, origin.xz) - radius * radius;
    }
    float nearest = reflectionMaxDistance;
    vec2 roots;
    if (reflectionQuadratic(a, b, c, roots)) {
        reflectionKeepSideHit(origin, direction, roots.x, nearest);
        reflectionKeepSideHit(origin, direction, roots.y, nearest);
    }
    reflectionKeepCapHit(origin, direction, -0.5, nearest);
    if (!conical) reflectionKeepCapHit(origin, direction, 0.5, nearest);
    return nearest;
}

bool reflectionIntersectsBounds(vec3 origin, vec3 direction, vec3 low, vec3 high, float nearest) {
    float entry = 0.0, exit = nearest;
    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0.0) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) return false;
        } else {
            float a = (low[axis] - origin[axis]) / direction[axis];
            float b = (high[axis] - origin[axis]) / direction[axis];
            entry = max(entry, min(a, b));
            exit = min(exit, max(a, b));
            if (entry > exit) return false;
        }
    }
    return true;
}

void reflectionTestTriangle(int base, vec3 origin, vec3 direction, inout float nearest) {
    vec3 a = texelFetch(reflectionGeometry, base).xyz;
    vec3 e1 = texelFetch(reflectionGeometry, base + 1).xyz - a;
    vec3 e2 = texelFetch(reflectionGeometry, base + 2).xyz - a;
    vec3 p = cross(direction, e2);
    float determinant = dot(e1, p);
    if (abs(determinant) < 0.00000001) return;
    vec3 offset = origin - a;
    float u = dot(offset, p) / determinant;
    vec3 q = cross(offset, e1);
    float v = dot(direction, q) / determinant;
    float distance = dot(e2, q) / determinant;
    if (u >= 0.0 && v >= 0.0 && u + v <= 1.0 && distance > reflectionMinDistance)
        nearest = min(nearest, distance);
}

void reflectionTraceMesh(int cursor, int end, vec3 origin, vec3 direction, inout float nearest) {
    while (cursor < end) {
        vec4 low = texelFetch(reflectionGeometry, cursor);
        vec4 high = texelFetch(reflectionGeometry, cursor + 1);
        if (!reflectionIntersectsBounds(origin, direction, low.xyz, high.xyz, nearest)) {
            cursor += int(low.w);
            continue;
        }
        // Branch children immediately follow their parent. Leaves contain triangle vertices.
        cursor += 2;
        int leafEnd = cursor + 3 * int(high.w);
        for (; cursor < leafEnd; cursor += 3) reflectionTestTriangle(cursor, origin, direction, nearest);
    }
}

vec3 correctedReflectionDirection(vec3 origin, vec3 direction) {
    float lengthSquared = dot(direction, direction);
    if (lengthSquared < 0.00000001) return vec3(0.0, 0.0, 1.0);
    direction *= inversesqrt(lengthSquared);
    float nearest = reflectionMaxDistance;
    int nextPayload = reflectionGroupCount * 3;
    for (int group = 0; group < reflectionGroupCount; ++group) {
        int header = group * 3;
        vec3 metadata = texelFetch(reflectionGeometry, header).xyz;
        int base = nextPayload;
        nextPayload += int(metadata.z);
        if (int(metadata.x) == reflectionReceiverIndex || int(metadata.x) == reflectionProbeIndex) continue;
        vec3 low = texelFetch(reflectionGeometry, header + 1).xyz;
        vec3 high = texelFetch(reflectionGeometry, header + 2).xyz;
        if (!reflectionIntersectsBounds(origin, direction, low, high, nearest)) continue;
        int shape = int(metadata.y);
        if (shape == 6) {
            for (int i = base; i < nextPayload; i += 3) reflectionTestTriangle(i, origin, direction, nearest);
            continue;
        }
        if (shape == 7) {
            reflectionTraceMesh(base, nextPayload, origin, direction, nearest);
            continue;
        }
        mat4 inverseWorld = mat4(texelFetch(reflectionGeometry, base),
                                 texelFetch(reflectionGeometry, base + 1),
                                 texelFetch(reflectionGeometry, base + 2),
                                 texelFetch(reflectionGeometry, base + 3));
        vec3 localOrigin = (inverseWorld * vec4(origin, 1.0)).xyz;
        vec3 localDirection = (inverseWorld * vec4(direction, 0.0)).xyz;
        float distance = reflectionMaxDistance;
        if (shape == 0) distance = reflectionBoxHit(localOrigin, localDirection);
        else if (shape == 1 || shape == 2) distance = reflectionPlaneHit(localOrigin, localDirection, shape == 1);
        else if (shape == 3) distance = reflectionSphereHit(localOrigin, localDirection);
        else if (shape == 4 || shape == 5) distance = reflectionRoundSolidHit(localOrigin, localDirection, shape == 5);
        nearest = min(nearest, distance);
    }
    if(reflectionGroundEnabled==1 && abs(direction.y)>.000001){
        float groundDistance=-origin.y/direction.y;
        if(reflectionPositiveRoot(groundDistance))nearest=min(nearest,groundDistance);
    }
    if (nearest == reflectionMaxDistance) return vec3(0.0);
    vec3 captureDirection = origin + direction * nearest - reflectionCapturePosition;
    float captureDistance = length(captureDirection);
    return captureDistance > 0.000001 ? captureDirection / captureDistance : direction;
}
)";
}
