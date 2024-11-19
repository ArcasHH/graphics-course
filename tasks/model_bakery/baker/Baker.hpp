#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <tiny_gltf.h>

struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
};

struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

class Baker
{
public:
  Baker() = default;

  void bakeScene(std::filesystem::path path);

private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

  struct Vertex
  {
    glm::vec4 positionAndNormal;
    glm::vec4 texCoordAndTangentAndPadding;
  };

  static_assert(sizeof(Vertex) == sizeof(float) * 8);

  struct ProcessedMeshes
  {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderElement> relems;
    std::vector<Mesh> meshes;
  };

  ProcessedMeshes processMeshes(const tinygltf::Model& model) const;

private:
  tinygltf::TinyGLTF loader;
  tinygltf::TinyGLTF saver;
};
