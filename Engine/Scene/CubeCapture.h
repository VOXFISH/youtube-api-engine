#pragma once
#include "Rice/Core.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace Rice {
// OpenGL cubemap order, shared by reflection and point-shadow capture.
inline const std::array<Vector3,6> CubeCaptureDirections{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
inline const std::array<Vector3,6> CubeCaptureUps{{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}}};
inline Matrix4 CubeCaptureView(Vector3 position,int face){
    return glm::lookAt(position,position+CubeCaptureDirections[face],CubeCaptureUps[face]);
}
}
