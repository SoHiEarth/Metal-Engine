#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <tinyfiledialogs/tinyfiledialogs.h>
#include <array>
#include <filesystem>
#include <format>
#include <vector>
#include <map>
#include <chrono>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

import metal.level;
import metal.object;
import metal.editable;
import metal.material;
import metal.shader;
import metal.sprite;
import metal.light;
import metal.texture;
import metal.debug;
import metal.devgui;
import metal.io;
import metal.physics;
import metal.render;
import metal.telemetry;
import metal.localization;

#define IMAGE_PREVIEW_SIZE 100, 100
#define DEVGUI_ROUNDING_MORE 8.0F
#define DEVGUI_ROUNDING_LESS 6.0F
#define DEVGUI_FONT_SIZE 18.0F
#define DEFAULT_SHININESS 32.0F
#define DOCK_LEFT_WIDTH 0.3F
#define DOCK_RIGHT_WIDTH 0.3F
#define DOCK_BOTTOM_HEIGHT 0.4F
#define CONSOLE_TYPE_WIDTH 50.0F
#define CONSOLE_TRACEBACK_WIDTH 200.0F
bool dev::g_hud_enabled = false, g_disable_hud_after_frame = false;
Object* g_current_object = nullptr;
std::string g_material_diffuse, g_material_specular, g_material_vertex, g_material_fragment;
int g_material_shininess = DEFAULT_SHININESS;
std::vector<ConsoleMessage> g_console_messages;

void MaterialView(Material*& material);
void DrawProperties();
void DrawDebug();
void DrawLevel();
void DrawLocalization();
void DrawConsole();
void DrawTelemetry();
void DrawMenuFile();
void DrawMenuEdit();
void DrawMenuView();
void DrawMenuEngine();
void DrawMenuHelp();
void DrawMenuBar();

void dev::Init() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& imgui_io = ImGui::GetIO();
  imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = DEVGUI_ROUNDING_MORE;
  style.ChildRounding = DEVGUI_ROUNDING_MORE;
  style.FrameRounding = DEVGUI_ROUNDING_LESS;
  style.PopupRounding = DEVGUI_ROUNDING_LESS;
  style.ScrollbarRounding = DEVGUI_ROUNDING_LESS;
  style.GrabRounding = DEVGUI_ROUNDING_LESS;
  style.TabRounding = DEVGUI_ROUNDING_LESS;
  auto font_path = std::filesystem::path(io::g_engine_directory) / "IBM_Plex_Sans" / "IBMPlexSans-VariableFont_wdth,wght.ttf";
  ImFont* ui_font = imgui_io.Fonts->AddFontFromFileTTF(
      font_path.string().c_str(),
      DEVGUI_FONT_SIZE, nullptr, imgui_io.Fonts->GetGlyphRangesDefault());
  ImGui_ImplGlfw_InitForOpenGL(render::g_window, true);
  ImGui_ImplOpenGL3_Init("#version 150");
  debug::SetCallback(AddConsoleMessage);
  debug::Log("Initialized GUI");
}

void dev::Update() {
  if (!dev::g_hud_enabled) {
    glfwSetInputMode(render::g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    return;
  }
  glfwSetInputMode(render::g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;
  ImGui::Begin("DockSpace_Window", nullptr, window_flags);
  ImGui::PopStyleVar(2);
  ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
  ImGui::DockSpace(dockspace_id, ImVec2(0.0F, 0.0F));
  ImGui::End();
  static bool first_dock_layout = true;
  if (first_dock_layout) {
    first_dock_layout = false;
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);
    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Down, DOCK_BOTTOM_HEIGHT, nullptr, &dock_main_id);
    ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, DOCK_LEFT_WIDTH,
                                                       nullptr, &dock_main_id);
    ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right,
                                                        DOCK_RIGHT_WIDTH, nullptr, &dock_main_id);
    ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
    ImGui::DockBuilderDockWindow("Properties", dock_id_right);
    ImGui::DockBuilderDockWindow("Debug", dock_id_right);
    ImGui::DockBuilderDockWindow("Level", dock_id_left);
    ImGui::DockBuilderDockWindow("Localization", dock_main_id);
    ImGui::DockBuilderDockWindow("Renderer", dock_id_right);
    ImGui::DockBuilderDockWindow("Console", dock_id_bottom);
    ImGui::DockBuilderDockWindow("Telemetry", dock_id_bottom);
    ImGui::DockBuilderFinish(dockspace_id);
  }
  DrawProperties();
  DrawDebug();
  DrawLevel();
  DrawLocalization();
  DrawConsole();
  DrawTelemetry();
  DrawMenuBar();
}

void dev::AddConsoleMessage(const char* traceback, const char* message, std::uint8_t type) {
  ConsoleMessage console_message{traceback, message, ConsoleMessageType(type)};
  g_console_messages.push_back(console_message);
}

void dev::Render() {
  if (!dev::g_hud_enabled) {
    return;
  }
  ImGui::Render();
  ImGui::EndFrame();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  if (g_disable_hud_after_frame) {
    dev::g_hud_enabled = false;
    g_disable_hud_after_frame = false;
  }
}

void dev::Quit() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  debug::Log("Quit GUI");
}

std::vector<float> fps_history_;
std::vector<std::map<std::string, std::chrono::milliseconds>> timings_history_;

void DrawTelemetry() {
  ImGui::Begin("Telemetry");
  ImGui::SeparatorText("FPS");
  float fps = ImGui::GetIO().Framerate;
  fps_history_.push_back(fps);
  if (fps_history_.size() > 100) {
    fps_history_.erase(fps_history_.begin());
  }
  if (ImGui::BeginTable("FPSTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0F);
    ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0F);
    ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("FPS");
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.1f", fps);
    ImGui::TableSetColumnIndex(2);
    ImGui::PlotLines("##fps_plot", fps_history_.data(), fps_history_.size(), 0, nullptr, 0.0F,
                     *std::max_element(fps_history_.begin(), fps_history_.end()), ImVec2(0, 50));
    ImGui::EndTable();
  }
  ImGui::SeparatorText("Update Timings");
  if (ImGui::BeginTable("TelemetryTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0F);
    ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0F);
    ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    auto timings = telemetry::GetTimings();
    timings_history_.push_back(timings);
    if (timings_history_.size() > 100) {
      timings_history_.erase(timings_history_.begin());
    }
    for (const auto& [label, time] : timings) {
      ImGui::PushID(label.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%s", label.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%.3f ms", time.count() * 1.0F);
      ImGui::TableSetColumnIndex(2);
      std::array<float, 100> plot_data{};
      int plot_data_count = 0;
      for (const auto& history_entry : timings_history_) {
        auto it = history_entry.find(label);
        if (it != history_entry.end() && plot_data_count < 100) {
          plot_data[plot_data_count] = it->second.count() * 1.0F;
          plot_data_count++;
        }
      }
      if (plot_data_count > 0) {
        ImGui::PlotLines("##plot", plot_data.data(), plot_data_count, 0, nullptr, 0.0F,
                         *std::max_element(plot_data.begin(), plot_data.begin() + plot_data_count),
                         ImVec2(0, 50));
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Initialization Log");
  if (ImGui::BeginTable("InitLogsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0F);
    ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    auto logged_timings = telemetry::DownloadTimings("Engine Init");
    for (const auto& [name, time] : logged_timings) {
      ImGui::PushID(&name);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%s", name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%.3f", time.count() * 1.0F);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void DrawProperties() {
  ImGui::Begin("Properties");
  // Cache object's position, update it via physics if changed
  auto old_position = g_current_object != nullptr ? *g_current_object->position_ : glm::vec3();
  if (g_current_object != nullptr && io::g_level != nullptr) {
    if (ImGui::Button("Remove")) {
      io::g_level->RemoveObject(g_current_object);
      g_current_object = nullptr;
    } else {
      for (const auto& field : g_current_object->reg_) {
        if (field != nullptr) {
          field->RenderInterface();
        }
      }
      MaterialView(g_current_object->material_);
      ImGui::SeparatorText("Other Info");
      ImGui::LabelText("Type", "%s", g_current_object->GetTypeName().c_str());
      ImGui::Value("Has Initialized", g_current_object->has_initialized_);
      ImGui::Value("Has Quit", g_current_object->has_quit_);
    }
  }
  if (g_current_object != nullptr && old_position != *g_current_object->position_) {
    physics::SetBodyPosition(g_current_object->physics_body, *g_current_object->position_);
  }
  ImGui::End();
}

void DrawDebug() {
  ImGui::Begin("Debug");
  ImGui::Checkbox("Trace Source File", &debug::trace_source_file_);
  ImGui::Checkbox("Trace Function Name", &debug::trace_function_name_);
  ImGui::Checkbox("Trace Line Number", &debug::trace_line_number_);
  ImGui::End();
}

void DrawConsole() {
  ImGui::Begin("Console");
  ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false,
                    ImGuiWindowFlags_HorizontalScrollbar);
  if (ImGui::BeginTable(
          "ConsoleTable", 3,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, CONSOLE_TYPE_WIDTH);
    ImGui::TableSetupColumn("Traceback", ImGuiTableColumnFlags_WidthFixed, CONSOLE_TRACEBACK_WIDTH);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const auto& message : g_console_messages) {
      ImGui::TableNextRow();
      ImGui::PushID(&message);
      ImGui::TableSetColumnIndex(0);
      switch (message.type_) {
        case ConsoleMessageType::ConsoleMessageType_Info:
          ImGui::TextColored(ImVec4(0.0F, 1.0F, 0.0F, 1.0F), "Info");
          break;
        case ConsoleMessageType::ConsoleMessageType_Warning:
          ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.0F, 1.0F), "Warning");
          break;
        case ConsoleMessageType::ConsoleMessageType_Error:
          ImGui::TextColored(ImVec4(1.0F, 0.0F, 0.0F, 1.0F), "Error");
          break;
        default:
          ImGui::Text("Unknown");
          break;
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%s", message.traceback_.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%s", message.message_.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
  if (ImGui::Button("Clear")) {
    g_console_messages.clear();
  }
  ImGui::End();
}

void DevNewLevel() {
  const char* filter_patterns[] = {"*.xml"};
  const char* file =
      tinyfd_saveFileDialog("Save Level XML", "new_level.xml", 1, filter_patterns, "Level Files");
  if (file != nullptr) {
    io::FreeLevel(io::g_level);
    io::g_level = new Level();
    io::g_level->path_ = file;
  }
}

void DevOpenLevel() {
  const char* filter_patterns[] = {"*.xml"};
  const char* file =
      tinyfd_openFileDialog("Open Level XML", "", 1, filter_patterns, "Level Files", 0);
  if (file != nullptr) {
    io::FreeLevel(io::g_level);
    io::g_level = io::serialized::LoadLevel(file);
    g_current_object = nullptr;
  }
}

void DrawLevel() {
  ImGui::Begin("Level");
  if (io::g_level == nullptr) {
    if (ImGui::Button("New Level")) {
      DevNewLevel();
    }
    if (ImGui::Button("Open Level")) {
      DevOpenLevel();
    }
  }
  if (io::g_level != nullptr) {
    ImGui::LabelText("Level Path", "%s", io::g_level->path_.c_str());
    ImGui::SeparatorText("Scene Objects");
    if (ImGui::BeginTable(
            "AddObjectTable", 1,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
      for (const auto& object : io::g_level->objects_) {
        if (object != nullptr) {
          ImGui::TableNextRow();
          ImGui::PushID(object);
          ImGui::TableSetColumnIndex(0);
          if (ImGui::Selectable(std::format("{}", *object->name_).c_str())) {
            g_current_object = object;
          }
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
}
#define BUFFER_SIZE 128
void DrawLocalization() {
  ImGui::Begin("Localization");
  ImGui::InputText("Language", &localization::g_language);
  ImGui::SameLine();
  if (ImGui::Button("Add")) {
    localization::g_dictionary.insert({"", ""});
  }
  std::vector<std::pair<std::string, std::string>> renamed_entries;
  std::vector<std::string> keys_to_delete;
  if (ImGui::BeginTable(
          "EditableMapTable", 3,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Key");
    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Actions");
    ImGui::TableHeadersRow();
    int index = 0;
    for (auto& [key, value] : localization::g_dictionary) {
      ImGui::TableNextRow();
      ImGui::PushID(index);
      ImGui::TableSetColumnIndex(0);
      std::array<char, BUFFER_SIZE> key_buffer;
      strncpy(key_buffer.data(), key.c_str(), sizeof(key_buffer));
      key_buffer[sizeof(key_buffer) - 1] = '\0';
      if (ImGui::InputText("##Key", key_buffer.data(), sizeof(key_buffer))) {
        if (key != key_buffer.data() && strlen(key_buffer.data()) > 0) {
          keys_to_delete.push_back(key);
          renamed_entries.emplace_back(key_buffer.data(), value);
        }
      }
      ImGui::TableSetColumnIndex(1);
      std::array<char, BUFFER_SIZE> value_buffer;
      strncpy(value_buffer.data(), value.c_str(), sizeof(value_buffer));
      value_buffer[sizeof(value_buffer) - 1] = '\0';
      if (ImGui::InputText("##Value", value_buffer.data(), sizeof(value_buffer))) {
        localization::g_dictionary[key] = value_buffer.data();
      }
      ImGui::TableSetColumnIndex(2);
      if (ImGui::Button("Remove")) {
        keys_to_delete.push_back(key);
      }
      ImGui::PopID();
      index++;
    }
    for (const auto& key : keys_to_delete) {
      localization::g_dictionary.erase(key);
    }
    for (const auto& [new_key, value] : renamed_entries) {
      localization::g_dictionary[new_key] = value;
    }
    ImGui::EndTable();
  }
  if (ImGui::Button("Load")) {
    const char* filter_patterns[] = {"*.xml"};
    const char* file = tinyfd_openFileDialog("Open Localization Data", "", 1, filter_patterns,
                                             "Localization Files", 0);
    if (file != nullptr) {
      localization::Load(file);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    const char* filter_patterns[] = {"*.xml"};
    const char* file = tinyfd_saveFileDialog("Save Localization Data",
                                             std::string(localization::g_language + ".xml").c_str(),
                                             1, filter_patterns, "Localization Files");
    if (file != nullptr) {
      localization::Save(file);
    }
  }
  ImGui::End();
}

void MaterialView(Material*& material) {
  ImGui::SeparatorText("Material");
  if (ImGui::BeginTabBar("LoadType")) {
    if (ImGui::BeginTabItem("Directory")) {
      if (ImGui::Button("Load")) {
        char* material_path_c = tinyfd_selectFolderDialog("Select Material Directory", "");
        if (material_path_c != nullptr) {
          std::string material_path = std::string(material_path_c);
          io::FreeMaterial(material);
          material = io::LoadMaterial(material_path + "/diffuse.png",
                                      material_path + "/specular.png", material_path + "/vert.glsl",
                                      material_path + "/frag.glsl", DEFAULT_SHININESS);
          g_material_diffuse = material->diffuse_->path_;
          g_material_specular = material->specular_->path_;
          g_material_vertex = material->shader_->vertex_path_;
          g_material_fragment = material->shader_->fragment_path_;
        }
      }
      ImGui::EndTabItem();
      ImGui::SameLine();
    }
    if (ImGui::BeginTabItem("Specific")) {
      ImGui::InputText("Material Diffuse", &g_material_diffuse);
      ImGui::InputText("Material Specular", &g_material_specular);
      ImGui::InputText("Material Vertex", &g_material_vertex);
      ImGui::InputText("Material Fragment", &g_material_fragment);
      ImGui::DragInt("Material Shininess", &g_material_shininess);
      if (ImGui::Button("Load")) {
        io::FreeMaterial(material);
        material = io::LoadMaterial(g_material_diffuse, g_material_specular, g_material_vertex,
                                    g_material_fragment, g_material_shininess);
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  if (ImGui::Button("Clear")) {
    io::FreeMaterial(material);
    material = nullptr;
  }
  if (material == nullptr) {
    ImGui::TextColored({1, 0, 0, 1}, "No material loaded");
    return;
  }
  if (material != nullptr) {
    ImGui::Text("Diffuse");
    if (material->diffuse_ != nullptr) {
      ImGui::Image(material->diffuse_->id_, ImVec2(IMAGE_PREVIEW_SIZE));
      ImGui::LabelText("Path", "%s", material->diffuse_->path_.c_str());
    } else {
      ImGui::TextColored({1, 0, 0, 1}, "Diffuse texture not loaded");
    }
    ImGui::Text("Specular");
    if (material->specular_ != nullptr) {
      ImGui::Image(material->specular_->id_, ImVec2(IMAGE_PREVIEW_SIZE));
      ImGui::LabelText("Path", "%s", material->specular_->path_.c_str());
    } else {
      ImGui::TextColored({1, 0, 0, 1}, "Specular texture not loaded");
    }
    ImGui::DragFloat("Shininess", &material->shininess_);
    ImGui::Text("Shader");
    if (material->shader_ == nullptr) {
      ImGui::TextColored({1, 0, 0, 1}, "Shader not loaded");
    } else {
      ImGui::LabelText("Vertex Path", "%s", material->shader_->vertex_path_.c_str());
      ImGui::LabelText("Fragment Path", "%s", material->shader_->fragment_path_.c_str());
    }
  }
}

void DrawMenuFile() {
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New")) {
      DevNewLevel();
    }
    if (ImGui::MenuItem("Open")) {
      DevOpenLevel();
    }
    ImGui::BeginDisabled(io::g_level == nullptr);
    if (ImGui::MenuItem("Save")) {
      if (io::g_level != nullptr) {
        io::serialized::SaveLevel(io::g_level, io::g_level->path_);
      }
    }
    if (ImGui::MenuItem("Save As")) {
      if (io::g_level != nullptr) {
        const char* filter_patterns[] = {"*.xml"};
        const char* file = tinyfd_saveFileDialog("Save Level XML", "new_level.xml", 1,
                                                 filter_patterns, "Level Files");
        if (file != nullptr) {
          io::serialized::SaveLevel(io::g_level, file);
          io::g_level->path_ = file;
        }
      }
    }
    ImGui::EndDisabled();
    if (ImGui::MenuItem("Exit")) {
      glfwSetWindowShouldClose(render::g_window, GLFW_TRUE);
    }
    ImGui::EndMenu();
  }
}

void DrawMenuEdit() {
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::BeginMenu("Add")) {
      ImGui::BeginDisabled(io::g_level == nullptr);
      for (const auto& [name, func] : io::g_object_factory) {
        if (ImGui::MenuItem(std::format("{}", name).c_str())) {
          io::g_level->AddObject(func(), name);
        }
      }
      ImGui::EndDisabled();
      ImGui::EndMenu();
    }
    ImGui::BeginDisabled((g_current_object == nullptr) || (io::g_level == nullptr));
    if (ImGui::MenuItem("Remove Selected")) {
      io::g_level->RemoveObject(g_current_object);
    }
    ImGui::EndDisabled();
    ImGui::EndMenu();
  }
}

void DrawMenuView() {
  if (ImGui::BeginMenu("View")) {
    if (ImGui::MenuItem("Toggle HUD", "F1")) {
      g_disable_hud_after_frame = true;
    }
    ImGui::EndMenu();
  }
}

void DrawMenuEngine() {
  if (ImGui::BeginMenu("Engine")) {
    ImGui::EndMenu();
  }
}

void DrawMenuHelp() {
  if (ImGui::BeginMenu("Help")) {
    ImGui::EndMenu();
  }
}

void DrawMenuBar() {
  if (ImGui::BeginMainMenuBar()) {
    DrawMenuFile();
    DrawMenuEdit();
    DrawMenuView();
    DrawMenuEngine();
    DrawMenuHelp();
    ImGui::EndMainMenuBar();
  }
}
