#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imnodes.h"
#include <cstdio>
#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app_state.h"
#include "DroidSans.cpp"

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "invalid params, expected format: <cmd> json_filename sounds_dir_path");
        return 1;
    }

    const char* json_filename = argv[1];
    const char* sounds_path = argv[2];

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);            // 3.0+ only
#endif

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1800, 1500, "Miniaudio HLE editor", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // glfwMaximizeWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImNodes::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    // ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();
    ImGui::StyleColorsLight();
    ImNodes::StyleColorsLight();
    // ImGui::GetStyle().Colors[ImGuiCol_Button].w = 1.0f;

    ImNodes::GetStyle().Flags |= ImNodesStyleFlags_GridSnapping;
    ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    float xscale, yscale;
    glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &xscale, &yscale);

    // Load Fonts
    float SCALE = xscale;
    float FONT_SCALE = SCALE;
#if defined(__APPLE__)
    SCALE = 1.0f;
    io.FontGlobalScale = 1.0f / FONT_SCALE;
#endif
    // SCALE = 1.0f;
    // SCALE = 2.5f;
    // io.Fonts->AddFontFromFileTTF("res/fonts/DroidSans.ttf", floorf(13.0f * SCALE));
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(DroidSans_compressed_data_base85, floorf(13.0f * FONT_SCALE));
    ImGui::GetStyle().ScaleAllSizes(SCALE);
    ImNodes::GetStyle().GridSpacing *= SCALE;
    ImNodes::GetStyle().PinTriangleSideLength = 15 * SCALE;
    ImNodes::GetStyle().NodePadding.x = 3.0f * SCALE;
    ImNodes::GetStyle().NodePadding.y = 3.0f * SCALE;
    ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);

    auto app_state = hle_audio::editor::create_app_state(SCALE);
    hle_audio::editor::init_with_data(app_state, json_filename, sounds_path);

    // Main loop
    bool running = true;
    while (running) {
        bool should_close = glfwWindowShouldClose(window);
        if (should_close) {
            glfwSetWindowShouldClose(window, GLFW_FALSE);

            if (hle_audio::editor::request_exit(app_state))
                break; // running loop
        }

        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const bool use_work_area = true;
            ImGui::SetNextWindowPos(use_work_area ? viewport->WorkPos : viewport->Pos);
            ImGui::SetNextWindowSize(use_work_area ? viewport->WorkSize : viewport->Size);

            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                    ImGuiWindowFlags_NoMove | 
                    ImGuiWindowFlags_NoResize | 
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoBringToFrontOnFocus;
            // ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus
            ImGui::Begin("main window", nullptr, flags);

            // running = 
            running = hle_audio::editor::process_frame(app_state);

            ImGui::End();
        }


        // Rendering
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    hle_audio::editor::destroy(app_state);

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImNodes::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
