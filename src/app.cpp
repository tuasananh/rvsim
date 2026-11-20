#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <print>

#include "embedded_fonts.h"
#include "emscripten_mainloop_stub.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ui/design_tokens.h"

namespace rvsim {

void ErrorCallback(int error, const char* description) {
  if (error == 0) {
    // Informational notices (e.g. unsupported window hints in the
    // Emscripten GLFW port) are not errors.
    std::println("GLFW notice: {}", description);
    return;
  }
  std::println(stderr, "GLFW error {}: {}", error, description);
}

App::App(std::shared_ptr<Context> context) : context_(context) {
  glfwSetErrorCallback(ErrorCallback);
  if (!glfwInit()) {
    std::println(stderr, "Failed to initialize GLFW");
    std::exit(1);
  }

  // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100 (WebGL 1.0)
  const char* glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
  // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
  const char* glsl_version = "#version 300 es";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
  // GL 3.2 + GLSL 150
  const char* glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);  // Required on Mac
#else
  // GL 3.0 + GLSL 130
  const char* glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+
  // only glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // 3.0+ only
#endif

  float main_scale = 1.0f;
  if (GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor()) {
    main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(
        primary_monitor);  // Valid on GLFW 3.3+ only
  }
  window_ = glfwCreateWindow((int)(1280 * main_scale), (int)(800 * main_scale),
                             "rvsim | RISC-V Simulator", nullptr, nullptr);
  if (window_ == nullptr) {
    std::exit(1);
  }
  glfwMakeContextCurrent(window_);

#ifndef __EMSCRIPTEN__
  glfwSwapInterval(1);  // Enable vsync
#endif

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;             // Enable Gamepad Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // Enable Docking

  ImFontConfig font_cfg;
  font_cfg.FontDataOwnedByAtlas = false;
  ImFont* sans_font = io.Fonts->AddFontFromMemoryTTF(
      const_cast<void*>(static_cast<const void*>(assets::kNotoSansRegularData)),
      static_cast<int>(assets::kNotoSansRegularSize), 0.0f, &font_cfg);
  ImFont* mono_font = io.Fonts->AddFontFromMemoryTTF(
      const_cast<void*>(
          static_cast<const void*>(assets::kNotoSansMonoRegularData)),
      static_cast<int>(assets::kNotoSansMonoRegularSize), 0.0f, &font_cfg);
  if (sans_font == nullptr || mono_font == nullptr) {
    std::println(stderr, "Failed to load embedded fonts");
    std::exit(1);
  }
  context_->set_sans_font(sans_font);
  context_->set_monospace_font(mono_font);

  // Theme + fixed size scale (changing DPI requires re-applying the theme).
  ui::ApplyTheme(main_scale);

  ImGuiStyle& style = ImGui::GetStyle();
  style.FontScaleDpi =
      main_scale;  // Set initial font scale. (in docking branch: using
                   // io.ConfigDpiScaleFonts=true automatically overrides this
                   // for every window depending on the current monitor)

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window_, true);
#ifdef __EMSCRIPTEN__
  ImGui_ImplGlfw_InstallEmscriptenCallbacks(window_, "#canvas");
#endif
  ImGui_ImplOpenGL3_Init(glsl_version);

  workspace_ = std::make_unique<ui::Workspace>(context_);
}

App::~App() {
  windows_.clear();
  workspace_.reset();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window_);
  glfwTerminate();
}

void App::Loop() {
  // Main loop
#ifdef __EMSCRIPTEN__
  // For an Emscripten build we are disabling file-system access, so let's not
  // attempt to do a fopen() of the imgui.ini file. You may manually call
  // LoadIniSettingsFromMemory() to load settings from your own storage.
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  EMSCRIPTEN_MAINLOOP_BEGIN
#else
  while (!glfwWindowShouldClose(window_) && !context_->quit_requested)
#endif
  {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
    // tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
    // your main application, or clear/overwrite your copy of the mouse data.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
    // data to your main application, or clear/overwrite your copy of the
    // keyboard data. Generally you may always pass all inputs to dear imgui,
    // and hide them from your application based on those two flags.
    glfwPollEvents();
#ifndef __EMSCRIPTEN__
    if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED) != 0) {
      ImGui_ImplGlfw_Sleep(10);
      continue;
    }
#endif

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    context_->BeginFrame();
    workspace_->Render();

    for (const auto& window : windows_) {
      window->Render();
    }

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    const auto& clear_color = context_->clear_color();
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#ifndef __EMSCRIPTEN__
    glfwSwapBuffers(window_);
#endif
  }
#ifdef __EMSCRIPTEN__
  EMSCRIPTEN_MAINLOOP_END;
#endif
}

Window* App::RegisterWindow(std::unique_ptr<Window> window) {
  Window* raw = window.get();
  workspace_->AddPanel(raw);
  windows_.emplace_back(std::move(window));
  return raw;
}

}  // namespace rvsim