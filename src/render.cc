// This should always be at top
#include <glad/glad.h>
// Ok continue

#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <imgui.h>
#include <array>
#include <filesystem>
#include <format>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>
#include <vector>
#include <functional>

import metal.camera;
import metal.light;
import metal.material;
import metal.shader;
import metal.debug;
import metal.devgui;
import metal.input;
import metal.io;
import metal.render;

#define TEXT_COLOR_RED ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
#define GUI_DRAG_STEP 0.1F
#define IMAGE_SIZE ImVec2(100, 100)
#define DEFAULT_CAMERA_FOV 60.0F
#define CAMERA_NEAR_PLANE 0.1F
#define CAMERA_FAR_PLANE 100.0F
#define DEFAULT_CAMERA_SPEED 0.1
float g_camera_speed = DEFAULT_CAMERA_SPEED;
inline void IfNoHUD(const std::function<void()>& fn) {
  if (!dev::g_hud_enabled)
    fn();
}

enum : std::uint16_t { kDefaultWindowWidth = 800, kDefaultWindowHeight = 600 };
using ImGui::TextColored;
float g_camera_fov = DEFAULT_CAMERA_FOV;
std::vector<DirectionalLight*> render::g_directional_lights;
std::vector<PointLight*> render::g_point_lights;
std::vector<SpotLight*> render::g_spot_lights;
unsigned int g_vao, g_vbo;
Framebuffer g_framebuffer;
unsigned int g_screen_vao, g_screen_vbo;
Shader *g_screen_shader = nullptr, *g_screen_shader_blur;
float g_prev_render_factor = render::g_render_settings.render_factor_;
RenderSettings render::g_render_settings{};
GLFWwindow* render::g_window = nullptr;
int render::g_width = kDefaultWindowWidth, render::g_height = kDefaultWindowHeight;
Camera render::g_camera;
ImVec2 g_last_viewport_size = ImVec2(0.0F, 0.0F);

const std::array<float, 32> kGVertices = {-0.5F, 0.5F,  0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
                                          -0.5F, -0.5F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                          0.5F,  0.5F,  0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F,
                                          0.5F,  -0.5F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F};

const std::array<float, 20> kGScreenVertices = {
    -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, -1.0F, -1.0F, 0.0F, 0.0F, 0.0F,
    1.0F,  1.0F, 0.0F, 1.0F, 1.0F, 1.0F,  -1.0F, 0.0F, 1.0F, 0.0F,
};
void DrawRendererStatus();
void RecreateFramebuffer();
void FBSizeCallback(GLFWwindow* window, int width, int height);

void render::Init() {
  if (glfwInit() == GLFW_FALSE) {
    const char* error_desc;
    glfwGetError(&error_desc);
    debug::Throw(std::format("Failed to initialize GLFW. {}", error_desc ? error_desc : "Unknown error"));
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  render::g_window = glfwCreateWindow(render::g_width, render::g_height, "Metal", nullptr, nullptr);
  if (render::g_window == nullptr) {
    const char* error_desc;
    glfwGetError(&error_desc);
    glfwTerminate();
    debug::Throw(std::format("Failed to create GLFW window. {}", error_desc ? error_desc : "Unknown error"));
  }
  glfwMakeContextCurrent(render::g_window);
  glfwSetFramebufferSizeCallback(render::g_window, FBSizeCallback);
  glfwSwapInterval(1);
  
  // Use static_cast instead of reinterpret_cast for function pointer conversion
  auto proc_address = reinterpret_cast<GLADloadproc>(glfwGetProcAddress);
  if (gladLoadGLLoader(proc_address) == 0) {
    debug::Throw("Failed to initialize GLAD");
    glfwTerminate();
    return;
  }
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_MULTISAMPLE);
  glViewport(0, 0, render::g_width, render::g_height);
  glGenVertexArrays(1, &g_vao);
  glGenBuffers(1, &g_vbo);
  glBindVertexArray(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kGVertices), kGVertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), static_cast<void*>(nullptr));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        static_cast<const void*>(static_cast<const char*>(nullptr) + (3 * sizeof(float))));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        static_cast<const void*>(static_cast<const char*>(nullptr) + (6 * sizeof(float))));
  glGenVertexArrays(1, &g_screen_vao);
  glGenBuffers(1, &g_screen_vbo);
  glBindVertexArray(g_screen_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_screen_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kGScreenVertices), kGScreenVertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), static_cast<void*>(nullptr));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        static_cast<const void*>(static_cast<const char*>(nullptr) + (3 * sizeof(float))));
  RecreateFramebuffer();
  auto vertex_path = std::filesystem::path(io::g_engine_directory) / "screen_shader" / "vert.glsl";
  auto fragment_path = std::filesystem::path(io::g_engine_directory) / "screen_shader" / "frag.glsl";
  g_screen_shader = io::LoadShader(vertex_path.string(), fragment_path.string());
  g_screen_shader->Use();
  g_screen_shader->SetInt("scene", 0);

  input::AddHook({GLFW_KEY_W, ButtonState::kHold},
                 []() { IfNoHUD([]() { render::g_camera.position_.z -= g_camera_speed; }); });
  input::AddHook({GLFW_KEY_S, ButtonState::kHold},
                 []() { IfNoHUD([]() { render::g_camera.position_.z += g_camera_speed; }); });
  input::AddHook({GLFW_KEY_A, ButtonState::kHold},
                 []() { IfNoHUD([]() { render::g_camera.position_.x -= g_camera_speed; }); });
  input::AddHook({GLFW_KEY_D, ButtonState::kHold},
                 []() { IfNoHUD([]() { render::g_camera.position_.x += g_camera_speed; }); });
  input::AddHook({GLFW_KEY_E, ButtonState::kHold},
                 []() { IfNoHUD([]() { render::g_camera.position_.y += g_camera_speed; }); });
  input::AddHook({GLFW_KEY_Q, ButtonState::kHold},
                 []() { IfNoHUD([]() { render::g_camera.position_.y -= g_camera_speed; }); });
  debug::Log("Initialized render");
}

void render::Update() {
  if (render::g_window == nullptr) {
    return;
  }
  render::g_width = std::max(1, render::g_width);
  render::g_height = std::max(1, render::g_height);
  if (g_render_settings.render_factor_ != g_prev_render_factor) {
    RecreateFramebuffer();
  }
  glViewport(0, 0, g_framebuffer.width_, g_framebuffer.height_);
  glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer.framebuffer_);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void render::Render() {
  glViewport(0, 0, render::g_width, render::g_height);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  dev::Update();
  DrawRendererStatus();
  if (dev::g_hud_enabled) {
    ImGui::Begin("Viewport");
    auto image_size = ImGui::GetContentRegionAvail();
    // Use consistent texture coordinates across platforms
    // The coordinate flip is handled by the framebuffer setup instead of platform-specific code
    ImGui::Image(g_framebuffer.colorbuffers_[0], image_size, ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
  } else {
    g_screen_shader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_framebuffer.colorbuffers_[0]);
    glBindVertexArray(g_screen_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
  }
  dev::Render();
  glfwSwapBuffers(render::g_window);
}

void render::Quit() {
  glDeleteVertexArrays(1, &g_vao);
  glDeleteBuffers(1, &g_vbo);
  DeleteFramebuffer(g_framebuffer);

  if (render::g_window != nullptr) {
    glfwDestroyWindow(render::g_window);
    render::g_window = nullptr;
  }
  glfwTerminate();
  debug::Log("Quit render");
}

/////////////////////////////
/// Helper Functions etc. ///
/////////////////////////////

void DrawRendererStatus() {
  if (dev::g_hud_enabled) {
    ImGui::Begin("Renderer");
    ImGui::Text("Directional light count: %d",
                static_cast<int>(render::g_directional_lights.size()));
    ImGui::Text("Point light count: %d", static_cast<int>(render::g_point_lights.size()));
    ImGui::Text("Spot light count: %d", static_cast<int>(render::g_spot_lights.size()));
    ImGui::DragFloat3("Camera Position", glm::value_ptr(render::g_camera.position_));
    ImGui::DragFloat("Camera Field of View", &g_camera_fov);
    ImGui::DragFloat("Camera Speed", &g_camera_speed, 0.01F, 0.01F, 10.0F);
    ImGui::SeparatorText("Screen Shader");
    ImGui::InputFloat("Render Factor", &render::g_render_settings.render_factor_, GUI_DRAG_STEP);
    ImGui::Text("Colorbuffer");
    for (unsigned int colorbuffer : g_framebuffer.colorbuffers_) {
      ImGui::Image(colorbuffer, IMAGE_SIZE);
    }
    ImGui::End();
  }
}

glm::mat4 GetTransform(const glm::vec3& pos, const glm::vec2& scale, const glm::vec3& rot) {
  auto transform = glm::mat4(1.0F);
  transform = glm::translate(transform, pos);
  transform = glm::rotate(transform, rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
  transform = glm::rotate(transform, rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
  transform = glm::rotate(transform, rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
  transform = glm::scale(transform, glm::vec3(scale, 1));
  return transform;
}

void render::RenderTexture(const Material* material, const glm::vec3& pos, const glm::vec2& size,
                           const glm::vec3& rot) {
  if (material == nullptr) {
    return;
  }
  if (!material->IsValid()) {
    return;
  }

  glm::mat4 model = GetTransform(pos, size, rot);
  glm::mat4 view = glm::translate(glm::mat4(1.0F), -render::g_camera.position_);
  glm::mat4 projection = glm::perspective(
      glm::radians(g_camera_fov),
      (static_cast<float>(g_framebuffer.width_) / static_cast<float>(g_framebuffer.height_)),
      CAMERA_NEAR_PLANE, CAMERA_FAR_PLANE);

  material->Bind();
  material->shader_->SetMat4("model", model);
  material->shader_->SetMat4("view", view);
  material->shader_->SetMat4("projection", projection);
  material->shader_->SetVec3("viewPos", render::g_camera.position_);

  material->shader_->SetInt("NUM_DIRECTIONAL_LIGHTS",
                            static_cast<int>(g_directional_lights.size()));
  for (int i = 0; i < g_directional_lights.size(); i++) {
    if (g_directional_lights[i] != nullptr) {
      g_directional_lights[i]->SetUniforms(material->shader_, i);
    }
  }

  material->shader_->SetInt("NUM_POINT_LIGHTS", static_cast<int>(g_point_lights.size()));
  for (int i = 0; i < g_point_lights.size(); i++) {
    if (g_point_lights[i] != nullptr) {
      g_point_lights[i]->SetUniforms(material->shader_, i);
    }
  }

  material->shader_->SetInt("NUM_SPOT_LIGHTS", static_cast<int>(g_spot_lights.size()));
  for (int i = 0; i < g_spot_lights.size(); i++) {
    if (g_spot_lights[i] != nullptr) {
      g_spot_lights[i]->SetUniforms(material->shader_, i);
    }
  }

  glBindVertexArray(g_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

void FBSizeCallback(GLFWwindow* window, int width, int height) {
  render::g_width = width;
  render::g_height = height;
  RecreateFramebuffer();
  glViewport(0, 0, render::g_width, render::g_height);
}

Framebuffer render::CreateFramebuffer(FramebufferCreateInfo& create_info) {
  Framebuffer framebuffer;
  framebuffer.width_ = create_info.width_;
  framebuffer.height_ = create_info.height_;
  glGenFramebuffers(1, &framebuffer.framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.framebuffer_);
  framebuffer.colorbuffers_.resize(create_info.num_colorbuffers_);
  glGenTextures(create_info.num_colorbuffers_, framebuffer.colorbuffers_.data());
  for (unsigned int i = 0; std::cmp_less(i, create_info.num_colorbuffers_); i++) {
    glBindTexture(GL_TEXTURE_2D, framebuffer.colorbuffers_[i]);
    glTexImage2D(GL_TEXTURE_2D, create_info.level_, create_info.colorbuffer_format_,
                 create_info.width_, create_info.height_, create_info.border_, create_info.format_,
                 create_info.type_, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (create_info.same_colorbuffer_attachment_) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                             framebuffer.colorbuffers_[i], 0);
      if (framebuffer.attachments_.empty()) {
        framebuffer.attachments_.push_back(GL_COLOR_ATTACHMENT0);
      }
    } else {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                             framebuffer.colorbuffers_[i], 0);
      framebuffer.attachments_.push_back(GL_COLOR_ATTACHMENT0 + i);
    }
  }

  if (create_info.create_renderbuffer_) {
    glGenRenderbuffers(1, &framebuffer.renderbuffer_);
    glBindRenderbuffer(GL_RENDERBUFFER, framebuffer.renderbuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, create_info.width_,
                          create_info.height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              framebuffer.renderbuffer_);
    glDrawBuffers(framebuffer.attachments_.size(), framebuffer.attachments_.data());
  }
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    debug::Throw("Failed to create framebuffer!");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return framebuffer;
}

void render::DeleteFramebuffer(Framebuffer& framebuffer) {
  if (!framebuffer.attachments_.empty()) {
    framebuffer.attachments_.clear();
  }
  if (framebuffer.renderbuffer_ != 0) {
    glDeleteRenderbuffers(1, &framebuffer.renderbuffer_);
    framebuffer.renderbuffer_ = 0;
  }
  for (unsigned int& colorbuffer : framebuffer.colorbuffers_) {
    if (colorbuffer != 0) {
      glDeleteTextures(1, &colorbuffer);
      colorbuffer = 0;
    }
  }
  if (framebuffer.framebuffer_ != 0) {
    glDeleteFramebuffers(1, &framebuffer.framebuffer_);
    framebuffer.framebuffer_ = 0;
  }
}

void RecreateFramebuffer() {
  render::DeleteFramebuffer(g_framebuffer);
  int framebuffer_width = std::max(1, render::g_width) / render::g_render_settings.render_factor_;
  int framebuffer_height = std::max(1, render::g_height) / render::g_render_settings.render_factor_;
  FramebufferCreateInfo create_info;
  create_info.num_colorbuffers_ = 2;
  create_info.level_ = 0;
  create_info.colorbuffer_format_ = GL_RGBA16F;
  create_info.width_ = framebuffer_width;
  create_info.height_ = framebuffer_height;
  create_info.border_ = 0;
  create_info.format_ = GL_RGBA;
  create_info.type_ = GL_FLOAT;
  create_info.same_colorbuffer_attachment_ = false;
  create_info.create_renderbuffer_ = true;
  g_framebuffer = render::CreateFramebuffer(create_info);
  g_prev_render_factor = render::g_render_settings.render_factor_;
}
