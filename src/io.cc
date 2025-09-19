import metal.level;
import metal.light;
import metal.object;
import metal.shader;
import metal.sprite;
import metal.texture;
import metal.debug;
import metal.io;
#include <fmt/core.h>
#include <glad/glad.h>
#include <tinyfiledialogs/tinyfiledialogs.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <pugixml.hpp>
#include <sstream>
#include <unordered_map>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define MATERIAL_KEY_NAME "Material"
#define MATERIAL_DIFFUSE_KEY_NAME "diffuse"
#define MATERIAL_SPECULAR_KEY_NAME "specular"
#define MATERIAL_VERTEX_KEY_NAME "vs"
#define MATERIAL_FRAGMENT_KEY_NAME "fs"
#define MATERIAL_SHININESS_KEY_NAME "shininess"
#define MATERIAL_SHININESS_DEFAULT_VALUE 32.0F
#define OBJECT_FACTORY_KEY(Object)  \
  {                                 \
    #Object, {                      \
      []() { return new Object(); } \
    }                               \
  }
Level* io::g_level = nullptr;
enum : std::uint16_t { kLogSize = 512 };
std::string io::g_engine_directory;
std::unordered_map<std::string, std::function<Object*()>> io::g_object_factory = {
    OBJECT_FACTORY_KEY(Sprite), OBJECT_FACTORY_KEY(DirectionalLight),
    OBJECT_FACTORY_KEY(PointLight), OBJECT_FACTORY_KEY(SpotLight)};
std::string ValidateName(std::string input);
bool CheckFile(std::string_view path) {
  return std::filesystem::exists(path);
}

void io::Init() {
  if (!CheckFile("engine_assets")) {
    tinyfd_messageBox("Engine Directory Not Found",
                      "Please select the engine directory to continue.", "ok", "info", 1);
    char* engine_path = tinyfd_selectFolderDialog("Select the engine directory",
                                                  std::filesystem::current_path().string().c_str());
    if (engine_path != nullptr) {
      g_engine_directory = std::string(engine_path);
      bool copy_assets = tinyfd_messageBox(
          "Copy Assets", "Would you like to copy the engine assets to the current directory?",
          "yesno", "question", 1);
      if (copy_assets) {
        if (!CheckFile("engine_assets")) {
          std::filesystem::create_directory("engine_assets");
        }
        for (const auto& entry : std::filesystem::directory_iterator(g_engine_directory)) {
          auto destination = std::filesystem::path("engine_assets") / entry.path().filename();
          std::filesystem::copy(entry.path(), destination,
                                std::filesystem::copy_options::overwrite_existing);
        }
      }
    } else {
      debug::Throw("Engine directory selection cancelled, application cannot continue.");
    }
  } else {
    io::g_engine_directory = "engine_assets";
  }
  debug::Log("Initialized io");
}

///////////////////////////
///  Level IO functions ///
///////////////////////////

[[nodiscard("Pointer Discarded")]] Level* io::serialized::LoadLevel(std::string_view path) {
  if (!CheckFile(path)) {
    debug::Throw(std::format("Requested file does not exist. {}", path));
  }
  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_file(std::string(path).c_str());
  if (!result) {
    debug::Throw(std::format("Failed to load level file. {}, {}", path, result.description()));
  }

  pugi::xml_node root = doc.child("level");
  if (!root) {
    debug::Throw(std::format("Requested file {} is not a valid level file", path));
  }

  auto* level = new Level();
  level->path_ = path;
  for (pugi::xml_node object_node : root.children("object")) {
    level->AddObject(io::serialized::LoadObject(object_node));
  }
  debug::Log(std::format("Successfully loaded {}", path));
  level->Init();
  return level;
}

void io::FreeLevel(Level* level) {
  if (level == nullptr) {
    return;
  }
  for (auto* object : level->objects_) {
    object->Quit();
    object->has_quit_ = true;
    FreeObject(object);
  }
  delete level;
  level = nullptr;
}

void io::serialized::SaveLevel(const Level* level, std::string_view path) {
  pugi::xml_document doc;
  pugi::xml_node root = doc.append_child("level");
  for (const auto& object : level->objects_) {
    pugi::xml_node object_node = root.append_child("object");
    object_node.append_attribute("type") = object->GetTypeName();
    object->Save(object_node);
  }
  bool save_result = doc.save_file(std::string(path).c_str());
  if (!save_result) {
    debug::Throw(std::format("Failed to save level file. {}", path));
  }
}

////////////////////////////
///  Object IO functions ///
////////////////////////////

[[nodiscard("Pointer Discarded")]] Object* io::serialized::LoadObject(pugi::xml_node& base_node) {
  std::string type_value = base_node.attribute("type").value();
  auto iterator = g_object_factory.find(type_value);
  if (iterator == g_object_factory.end()) {
    debug::Throw(std::format("Unknown object type: {}", type_value));
  }
  Object* object = iterator->second();
  object->Load(base_node);
  return object;
}

void io::FreeObject(Object*& object) {
  if (object == nullptr) {
    return;
  }
  if (object->has_initialized_ && !object->has_quit_) {
    object->Quit();
    object->has_quit_ = true;
  }
  delete object;
  object = nullptr;
}

void io::serialized::SaveObject(const Object* object, pugi::xml_node& base_node) {
  if (object == nullptr) {
    return;
  }
  object->Save(base_node);
}

///////////////////////////
/// Shader IO functions ///
///////////////////////////

std::string ReadFile(std::string_view path);
unsigned int CompileShader(std::string_view code, int type);

[[nodiscard("Pointer Discarded")]] Shader* io::LoadShader(std::string_view vertex_path,
                                                          std::string_view fragment_path) {
  if (!CheckFile(vertex_path) || !CheckFile(fragment_path)) {
    debug::Throw(
        std::format("Requested shader file does not exist. {}, {}", vertex_path, fragment_path));
  }

  unsigned int vertex = 0;
  unsigned int fragment = 0;
  try {
    vertex = CompileShader(ReadFile(vertex_path), GL_VERTEX_SHADER);
    fragment = CompileShader(ReadFile(fragment_path), GL_FRAGMENT_SHADER);
  } catch (std::runtime_error& e) {
    debug::Throw(std::format("Failed to load shader. {}", e.what()));
    return nullptr;
  }
  unsigned int program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  int success;
  std::array<char, kLogSize> log;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success == 0) {
    glGetProgramInfoLog(program, kLogSize, nullptr, log.data());
    debug::Throw(std::format("Shader program link failed. {}", log.data()));
    return nullptr;
  }
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  auto* shader = new Shader(vertex_path, fragment_path);
  shader->id_ = program;
  return shader;
}

void io::FreeShader(Shader*& shader) {
  if (shader == nullptr) {
    return;
  }
  glDeleteProgram(shader->id_);
  delete shader;
  shader = nullptr;
}

////////////////////////////
/// Texture IO functions ///
////////////////////////////

[[nodiscard("Pointer Discarded")]] Texture* io::LoadTexture(std::string_view path) {
  auto* texture = new Texture();
  texture->path_ = path;
  glGenTextures(1, &texture->id_);
  stbi_set_flip_vertically_on_load(1);
  unsigned char* data =
      stbi_load(std::string(path).c_str(), &texture->width_, &texture->height_, &texture->channels_, 0);
  if (data == nullptr) {
    delete texture;
    texture = nullptr;
    debug::Throw(std::format("Failed to load texture. Details: {}", path));
    return nullptr;
  }
  GLenum format = GL_RGB;
  switch (texture->channels_) {
    case 1:
      format = GL_RED;
      break;
    case 3:
      format = GL_RGB;
      break;
    case 4:
      format = GL_RGBA;
      break;
  }
  glBindTexture(GL_TEXTURE_2D, texture->id_);
  glTexImage2D(GL_TEXTURE_2D, 0, format, texture->width_, texture->height_, 0, format,
               GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  stbi_image_free(data);
  return texture;
}

void io::FreeTexture(Texture*& texture) {
  if (texture == nullptr) {
    return;
  }
  glDeleteTextures(1, &texture->id_);
  delete texture;
  texture = nullptr;
}

/////////////////////////////
/// Material IO functions ///
/////////////////////////////

[[nodiscard("Pointer Discarded")]] Material* io::LoadMaterial(std::string_view diffuse,
                                                              std::string_view specular,
                                                              std::string_view vertex,
                                                              std::string_view fragment,
                                                              float shininess) {
  auto* material = new Material();
  material->shininess_ = shininess;
  try {
    material->diffuse_ = io::LoadTexture(diffuse);
    material->specular_ = io::LoadTexture(specular);
    material->shader_ = io::LoadShader(vertex, fragment);
  } catch (std::runtime_error& e) {
    debug::Log(std::format("Material load failed. {}", e.what()));
    io::FreeMaterial(material);
    return nullptr;
  }
  material->shader_->Use();
  material->shader_->SetInt("material.diffuse", 0);
  material->shader_->SetInt("material.specular", 1);
  material->is_initialized_ = true;
  return material;
}

void io::FreeMaterial(Material*& material) {
  if (material == nullptr) {
    return;
  }
  io::FreeTexture(material->diffuse_);
  io::FreeTexture(material->specular_);
  io::FreeShader(material->shader_);
  delete material;
  material = nullptr;
}

[[nodiscard("Pointer Discarded")]] Material* io::serialized::LoadMaterial(pugi::xml_node& node) {
  pugi::xml_node material_node = node.child(MATERIAL_KEY_NAME);
  if (!material_node) {
    return nullptr;
  }
  return io::LoadMaterial(material_node.attribute(MATERIAL_DIFFUSE_KEY_NAME).as_string(),
                          material_node.attribute(MATERIAL_SPECULAR_KEY_NAME).as_string(),
                          material_node.attribute(MATERIAL_VERTEX_KEY_NAME).as_string(),
                          material_node.attribute(MATERIAL_FRAGMENT_KEY_NAME).as_string(),
                          material_node.attribute(MATERIAL_SHININESS_KEY_NAME)
                              .as_float(MATERIAL_SHININESS_DEFAULT_VALUE));
}

void io::serialized::SaveMaterial(const Material* material, pugi::xml_node& base_node) {
  if (material == nullptr) {
    return;
  }

  if (!material->IsValid()) {
    debug::Log("Material is invalid (missing texture or shader), skipping save");
    return;
  }

  pugi::xml_node node = base_node.child(MATERIAL_KEY_NAME);
  if (!node) {
    node = base_node.append_child(MATERIAL_KEY_NAME);
  }
  node.append_attribute(MATERIAL_DIFFUSE_KEY_NAME).set_value(material->diffuse_->path_);
  node.append_attribute(MATERIAL_SPECULAR_KEY_NAME).set_value(material->specular_->path_);
  node.append_attribute(MATERIAL_VERTEX_KEY_NAME).set_value(material->shader_->vertex_path_);
  node.append_attribute(MATERIAL_FRAGMENT_KEY_NAME).set_value(material->shader_->fragment_path_);
  node.append_attribute(MATERIAL_SHININESS_KEY_NAME).set_value(material->shininess_);
}

///////////////////////////////
/// Serialized IO functions ///
///////////////////////////////

glm::vec3 io::serialized::LoadVec3(pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  glm::vec3 value;
  value.x = node.attribute("x").as_float(0.0F);
  value.y = node.attribute("y").as_float(0.0F);
  value.z = node.attribute("z").as_float(0.0F);
  return value;
}

glm::vec2 io::serialized::LoadVec2(pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  glm::vec2 value;
  value.x = node.attribute("x").as_float(0.0F);
  value.y = node.attribute("y").as_float(0.0F);
  return value;
}

std::string io::serialized::LoadString(pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  return node.attribute("value").as_string("");
}

int io::serialized::LoadInt(pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  return node.attribute("value").as_int(0);
}

float io::serialized::LoadFloat(pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  return node.attribute("value").as_float(0.0F);
}

void io::serialized::SaveVec3(const glm::vec3* value, pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  if (!node) {
    node = base_node.append_child(name);
  }
  node.append_attribute("x").set_value(value->x);
  node.append_attribute("y").set_value(value->y);
  node.append_attribute("z").set_value(value->z);
}

void io::serialized::SaveVec2(const glm::vec2* value, pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  if (!node) {
    node = base_node.append_child(name);
  }
  node.append_attribute("x").set_value(value->x);
  node.append_attribute("y").set_value(value->y);
}

void io::serialized::SaveString(const std::string* value, pugi::xml_node& base_node,
                                std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  if (!node) {
    node = base_node.append_child(name);
  }
  node.append_attribute("value").set_value(value->c_str());
}

void io::serialized::SaveInt(const int* value, pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  if (!node) {
    node = base_node.append_child(name);
  }
  node.append_attribute("value").set_value(*value);
}

void io::serialized::SaveFloat(const float* value, pugi::xml_node& base_node, std::string name) {
  name = ValidateName(name);
  pugi::xml_node node = base_node.child(name);
  if (!node) {
    node = base_node.append_child(name);
  }
  node.append_attribute("value").set_value(*value);
}

///////////////////////////
/// Helper IO functions ///
///////////////////////////

std::string ValidateName(std::string input) {
  std::ranges::replace(input.begin(), input.end(), ' ', '_');
  return input;
}

std::string ReadFile(std::string_view path) {
  if (!std::filesystem::exists(path)) {
    debug::Throw(std::format("Requested file does not exist. {}", path));
  }
  std::ifstream file;
  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  try {
    file.open(std::string(path));
    std::stringstream stream;
    stream << file.rdbuf();
    file.close();
    return stream.str();
  } catch (std::ifstream::failure e) {
    debug::Throw(std::format("File IO failure. {}, {}", path, e.what()));
  }
  return "";
}

[[nodiscard("Shader Discarded")]] unsigned int CompileShader(std::string_view code, int type) {
  if (code.empty()) {
    debug::Throw("Got empty code.");
  }
  // Ensure null-terminated string for OpenGL
  std::string code_str(code);
  const char* code_data = code_str.c_str();
  unsigned int shader;
  int success;
  std::array<char, kLogSize> log;
  shader = glCreateShader(type);
  glShaderSource(shader, 1, &code_data, nullptr);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (success == 0) {
    glGetShaderInfoLog(shader, kLogSize, nullptr, log.data());
    debug::Throw(std::format("Shader compilation failed. {}", log.data()));
  }
  return shader;
}
