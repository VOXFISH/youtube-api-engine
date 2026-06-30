#pragma once

#include "Rice/Application.h"
#include "Rice/Debug.h"
#include "Rice/Time.h"
#include "Rice/Layer.h"

#include "Input/Input.h"
#include "Event/KeyCode.h"
#include "Event/MouseCode.h"
#include "ImGui/ImGuiLayer.h"

#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"

#include "Renderer/Buffer.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/VertexArray.h"
 
#include "Renderer/Camera.h"
#include "Renderer/OrthCamera.h"
#include "Renderer/PerspectiveCamera.h"

#ifdef RICE_ENABLE_ENTRY_POINT
    #include <iostream>
    #include "Rice/EntryPoint.h"
#endif
