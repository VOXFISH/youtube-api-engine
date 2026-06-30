#include <Rice.h>

#include "../Assets/SandboxCube.h"

#include <imgui.h>

#include <utility>
#include <vector>

class ExampleLayer : public Rice::Layer
{
    public:
    ExampleLayer() : Layer("Example"){}

    void OnUpdate(Rice::Time ts) override
    {
        (void)ts;

    }
    virtual void OnImGuiRender() override
    {

    }
    void OnEvent(Rice::Event& event) override
    {
        (void)event;

    }
};

class SandboxApp : public Rice::Application
{
public:
    SandboxApp()
    {
        m_ImGuiLayer = new Rice::ImGuiLayer();
        m_ImGuiLayer->SetRenderCallback([this]()
        {
            ImGui::Begin("Center Cube");
            if (ImGui::ColorPicker4("Color", &m_CenterCubeColor.x))
            {
                if (m_CenterCube)
                    m_CenterCube->SetColor(m_CenterCubeColor);
            }
            ImGui::End();
        });
        PushOverlay(m_ImGuiLayer);
    }

    ~SandboxApp()
    {
    }

    void OnInit() override
    {
        const Rice::Window& window = GetWindow();
        const float aspectRatio = window.GetHeight() == 0
            ? 16.0f / 9.0f
            : static_cast<float>(window.GetWidth()) / static_cast<float>(window.GetHeight());

        Rice::RenderCommand::SetDepthTestEnabled(true);

        m_Camera = Rice::CreateScope<Rice::PerspectiveCamera>(45.0f, aspectRatio, 0.1f, 100.0f);
        m_Camera->SetPosition({ 0.0f, 3.0f, 6.0f });
        m_Camera->LookAtObj({ 0.0f, -0.15f, -1.5f });

        constexpr int screenColumns = 4;
        constexpr int floorDepthRows = 4;
        constexpr float tileSize = 1.0f;
        constexpr float tileHeight = 0.18f;
        constexpr float floorY = -0.5f;

        m_GrassTiles.reserve(screenColumns * floorDepthRows);

        for (int depth = 0; depth < floorDepthRows; ++depth)
        {
            for (int column = 0; column < screenColumns; ++column)
            {
                const float x = (static_cast<float>(column) - static_cast<float>(screenColumns - 1) * 0.5f) * tileSize;
                const float z = -static_cast<float>(depth) * tileSize;

                Rice::Scope<Sandbox::SandboxCube> tile = Rice::CreateScope<Sandbox::SandboxCube>(
                    Rice::Vector3(tileSize, tileHeight, tileSize),
                    Rice::Vector3(x, floorY, z),
                    Rice::Vector3(0.0f, 1.0f, 0.0f),
                    Rice::Color(1.0f, 1.0f, 1.0f, 1.0f));
                tile->ApplyGrassTexture();

                m_GrassTiles.push_back(std::move(tile));
            }
        }

        const float floorCenterZ = -static_cast<float>(floorDepthRows - 1) * tileSize * 0.5f;
        const float floorTopY = floorY + tileHeight * 0.5f;
        const float cubeSize = 1.0f;
        m_CenterCubePosition = Rice::Vector3(0.0f, floorTopY + cubeSize * 0.5f, floorCenterZ);

        m_CenterCube = Rice::CreateScope<Sandbox::SandboxCube>(
            Rice::Vector3(cubeSize, cubeSize, cubeSize),
            m_CenterCubePosition,
            Rice::Vector3(0.0f, 1.0f, 0.0f),
            m_CenterCubeColor);
    }

    void OnUpdate(Rice::Time t) override
    {
        constexpr float fixedDeltaTime = Rice::Time::fixedDeltaTime;
        m_FixedTimeAccumulator += t.GetSeconds();

        while (m_FixedTimeAccumulator >= fixedDeltaTime)
        {
            UpdateCenterCubeMovement(fixedDeltaTime);
            m_FixedTimeAccumulator -= fixedDeltaTime;
        }
    }

    void OnRender() override
    {
        Rice::Renderer::BeginScene(*m_Camera);
        for (const auto& tile : m_GrassTiles)
            tile->Render();
        m_CenterCube->Render();
        Rice::Renderer::EndScene();
    }

    void OnShutDown() override
    {
    }

private:
    void UpdateCenterCubeMovement(float fixedDeltaTime)
    {
        Rice::Vector3 movement(0.0f);

        if (Rice::Input::IsKeyPressed(Rice::Key::W))
            movement.z -= 1.0f;
        if (Rice::Input::IsKeyPressed(Rice::Key::S))
            movement.z += 1.0f;
        if (Rice::Input::IsKeyPressed(Rice::Key::A))
            movement.x -= 1.0f;
        if (Rice::Input::IsKeyPressed(Rice::Key::D))
            movement.x += 1.0f;

        if (glm::length(movement) == 0.0f)
            return;

        constexpr float tilesPerSecond = 2.0f;
        movement = glm::normalize(movement);
        m_CenterCubePosition += movement * tilesPerSecond * fixedDeltaTime;
        m_CenterCube->SetPosition(m_CenterCubePosition);
    }

private:
    Rice::ImGuiLayer* m_ImGuiLayer = nullptr;
    Rice::Scope<Rice::PerspectiveCamera> m_Camera;
    std::vector<Rice::Scope<Sandbox::SandboxCube>> m_GrassTiles;
    Rice::Scope<Sandbox::SandboxCube> m_CenterCube;
    Rice::Vector3 m_CenterCubePosition = Rice::Vector3(0.0f);
    Rice::Color m_CenterCubeColor = Rice::Color(0.9f, 0.25f, 0.2f, 1.0f);
    float m_FixedTimeAccumulator = 0.0f;
};

Rice::Application* Rice::CreateApplication()
{
    return new SandboxApp();
}
