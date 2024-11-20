#include "Baker.hpp"
#include <spdlog/spdlog.h>

std::uint32_t encode_normal(glm::vec4 normal)
{
  union Byte32{
     uint8_t arr[4];
     uint32_t val;
  }res{};

  res.arr[0] = static_cast<uint8_t>(glm::round(normal.x * 127.0));
  res.arr[1] = static_cast<uint8_t>(glm::round(normal.y * 127.0));
  res.arr[2] = static_cast<uint8_t>(glm::round(normal.z * 127.0));
  res.arr[3] = static_cast<uint8_t>(glm::round(normal.w * 127.0));

  return res.val;
}

std::optional<tinygltf::Model> Baker::loadModel(std::filesystem::path path)
{
  loader.SetImagesAsIs(true);

  tinygltf::Model model;

  std::string error;
  std::string warning;
  bool success = false;

  auto ext = path.extension();
  if (ext == ".gltf")
    success = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
  else if (ext == ".glb")
    success = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
  else
  {
    spdlog::error("glTF: Unknown glTF file extension. Expected .gltf or .glb.");
    return std::nullopt;
  }
  if (!success)
  {
    spdlog::error("glTF: Failed to load model!");
    if (!error.empty())
      spdlog::error("glTF: {}", error);
    return std::nullopt;
  }
  if (!warning.empty())
    spdlog::warn("glTF: {}", warning);
  if (
    !model.extensions.empty() || !model.extensionsRequired.empty() || !model.extensionsUsed.empty())
    spdlog::warn("glTF: No glTF extensions are currently implemented!");
  return model;
}

Baker::ProcessedMeshes Baker::processMeshes(const tinygltf::Model& model) const
{
  ProcessedMeshes result;

  {
    std::size_t vertexBytes = 0;
    std::size_t indexBytes = 0;
    for (const auto& bufView : model.bufferViews)
    {
      switch (bufView.target)
      {
      case TINYGLTF_TARGET_ARRAY_BUFFER:
        vertexBytes += bufView.byteLength;
        break;
      case TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER:
        indexBytes += bufView.byteLength;
        break;
      default:
        break;
      }
    }
    result.vertices.reserve(vertexBytes / sizeof(Vertex));
    result.indices.reserve(indexBytes / sizeof(std::uint32_t));
  }

  {
    std::size_t totalPrimitives = 0;
    for (const auto& mesh : model.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes)
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(mesh.primitives.size()),
    });

    for (const auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        --result.meshes.back().relemCount;
        continue;
      }

      const auto normalIt = prim.attributes.find("NORMAL");
      const auto tangentIt = prim.attributes.find("TANGENT");
      const auto texcoordIt = prim.attributes.find("TEXCOORD_0");

      const bool hasNormals = normalIt != prim.attributes.end();
      const bool hasTangents = tangentIt != prim.attributes.end();
      const bool hasTexcoord = texcoordIt != prim.attributes.end();
      std::array accessorIndices{
        prim.indices,
        prim.attributes.at("POSITION"),
        hasNormals ? normalIt->second : -1,
        hasTangents ? tangentIt->second : -1,
        hasTexcoord ? texcoordIt->second : -1,
      };

      std::array accessors{
        &model.accessors[prim.indices],
        &model.accessors[accessorIndices[1]],
        hasNormals ? &model.accessors[accessorIndices[2]] : nullptr,
        hasTangents ? &model.accessors[accessorIndices[3]] : nullptr,
        hasTexcoord ? &model.accessors[accessorIndices[4]] : nullptr,
      };

      std::array bufViews{
        &model.bufferViews[accessors[0]->bufferView],
        &model.bufferViews[accessors[1]->bufferView],
        hasNormals ? &model.bufferViews[accessors[2]->bufferView] : nullptr,
        hasTangents ? &model.bufferViews[accessors[3]->bufferView] : nullptr,
        hasTexcoord ? &model.bufferViews[accessors[4]->bufferView] : nullptr,
      };

      result.relems.push_back(RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = static_cast<std::uint32_t>(accessors[0]->count),
      });

      const std::size_t vertexCount = accessors[1]->count;

      std::array ptrs{
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[0]->buffer].data.data()) +
          bufViews[0]->byteOffset + accessors[0]->byteOffset,
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[1]->buffer].data.data()) +
          bufViews[1]->byteOffset + accessors[1]->byteOffset,
        hasNormals
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[2]->buffer].data.data()) +
            bufViews[2]->byteOffset + accessors[2]->byteOffset
          : nullptr,
        hasTangents
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[3]->buffer].data.data()) +
            bufViews[3]->byteOffset + accessors[3]->byteOffset
          : nullptr,
        hasTexcoord
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[4]->buffer].data.data()) +
            bufViews[4]->byteOffset + accessors[4]->byteOffset
          : nullptr,
      };

      std::array strides{
        bufViews[0]->byteStride != 0
          ? bufViews[0]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[0]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[0]->type),
        bufViews[1]->byteStride != 0
          ? bufViews[1]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[1]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[1]->type),
        hasNormals ? (bufViews[2]->byteStride != 0
                        ? bufViews[2]->byteStride
                        : tinygltf::GetComponentSizeInBytes(accessors[2]->componentType) *
                          tinygltf::GetNumComponentsInType(accessors[2]->type))
                   : 0,
        hasTangents ? (bufViews[3]->byteStride != 0
                         ? bufViews[3]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[3]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[3]->type))
                    : 0,
        hasTexcoord ? (bufViews[4]->byteStride != 0
                         ? bufViews[4]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[4]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[4]->type))
                    : 0,
      };

      for (std::size_t i = 0; i < vertexCount; ++i)
      {
        auto& vtx = result.vertices.emplace_back();
        glm::vec3 pos;
        // Fall back to 0 in case we don't have something.
        // NOTE: if tangents are not available, one could use http://mikktspace.com/
        // NOTE: if normals are not available, reconstructing them is possible but will look ugly
        glm::vec3 normal{0};
        glm::vec3 tangent{0};
        glm::vec2 texcoord{0};
        std::memcpy(&pos, ptrs[1], sizeof(pos));

        // NOTE: it's faster to do a template here with specializations for all combinations than to
        // do ifs at runtime. Also, SIMD should be used. Try implementing this!
        if (hasNormals)
          std::memcpy(&normal, ptrs[2], sizeof(normal));
        if (hasTangents)
          std::memcpy(&tangent, ptrs[3], sizeof(tangent));
        if (hasTexcoord)
          std::memcpy(&texcoord, ptrs[4], sizeof(texcoord));


        vtx.positionAndNormal = glm::vec4(pos, std::bit_cast<float>(encode_normal(glm::vec4(normal, 0))));
        vtx.texCoordAndTangentAndPadding =
          glm::vec4(texcoord, std::bit_cast<float>(encode_normal(glm::vec4(tangent, 0))), 0);

        ptrs[1] += strides[1];
        if (hasNormals)
          ptrs[2] += strides[2];
        if (hasTangents)
          ptrs[3] += strides[3];
        if (hasTexcoord)
          ptrs[4] += strides[4];
      }

      // Indices are guaranteed to have no stride
      const std::size_t indexCount = accessors[0]->count;
      if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
      {
        for (std::size_t i = 0; i < indexCount; ++i)
        {
          std::uint16_t index;
          std::memcpy(&index, ptrs[0], sizeof(index));
          result.indices.push_back(index);
          ptrs[0] += 2;
        }
      }
      else if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
      {
        const std::size_t lastTotalIndices = result.indices.size();
        result.indices.resize(lastTotalIndices + indexCount);
        std::memcpy(
          result.indices.data() + lastTotalIndices,
          ptrs[0],
          sizeof(result.indices[0]) * indexCount);
      }
    }
  }

  return result;
}

void Baker::updateBuffer(std::filesystem::path const bin_path, tinygltf::Model& model, ProcessedMeshes& res_meshs)
{  
  std::size_t indsSize = res_meshs.indices.size() * sizeof(uint32_t);
  std::size_t vertsSize = res_meshs.vertices.size() * sizeof(Vertex);

  tinygltf::Buffer buff;
  buff.name = bin_path.stem().string();
  buff.uri = bin_path.filename().string() + "_baked.bin";
  buff.data.resize(indsSize + vertsSize);

  std::memcpy(buff.data.data(), res_meshs.indices.data(), indsSize);
  std::memcpy(buff.data.data() + indsSize, res_meshs.vertices.data(), vertsSize);

  model.buffers.clear();
  model.buffers.push_back(buff);
}

void Baker::updateBufferView(tinygltf::Model& model, ProcessedMeshes& res_meshs)
{  
  std::size_t indsSize = res_meshs.indices.size() * sizeof(uint32_t);
  std::size_t vertsSize = res_meshs.vertices.size() * sizeof(Vertex);

  tinygltf::BufferView indsBufferView{};
  tinygltf::BufferView vertsBufferView{};

  indsBufferView.buffer = 0;
  indsBufferView.byteLength = indsSize;
  indsBufferView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

  vertsBufferView.buffer = 0;
  vertsBufferView.byteOffset = indsSize;
  vertsBufferView.byteLength = vertsSize;
  vertsBufferView.byteStride = sizeof(Vertex);
  vertsBufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;

  model.bufferViews.clear();
  model.bufferViews.push_back(indsBufferView);
  model.bufferViews.push_back(vertsBufferView);
}

void Baker::updateAccessors(std::vector<tinygltf::Accessor>& new_accessors, tinygltf::Primitive& prim, RenderElement& relem)
{
    const auto normalIt = prim.attributes.find("NORMAL");
    const auto tangentIt = prim.attributes.find("TANGENT");
    const auto texcoordIt = prim.attributes.find("TEXCOORD_0");

    const bool hasNormals = normalIt != prim.attributes.end();
    const bool hasTangents = tangentIt != prim.attributes.end();
    const bool hasTexcoord = texcoordIt != prim.attributes.end();

    prim.attributes.clear();

    std::uint32_t indexOffset = relem.indexOffset;
    std::uint32_t count = relem.indexCount;
    size_t offset = relem.vertexOffset * sizeof(Vertex);

    {
        tinygltf::Accessor inds_accessor{};

        inds_accessor.bufferView = 0;
        inds_accessor.byteOffset = indexOffset * sizeof(uint32_t);
        inds_accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        inds_accessor.count = count;
        inds_accessor.type = TINYGLTF_TYPE_SCALAR;
        
        prim.indices = static_cast<int>(new_accessors.size());
        new_accessors.push_back(inds_accessor);
    }

    {
        tinygltf::Accessor pos_accessor{};
        pos_accessor.bufferView = 1;
        pos_accessor.byteOffset = offset;
        pos_accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        pos_accessor.count = count;
        pos_accessor.type = TINYGLTF_TYPE_VEC3;

        prim.attributes.insert({ "POSITION", static_cast<int>(new_accessors.size()) });
        new_accessors.push_back(pos_accessor);
    }

    if(hasNormals)
    {
        tinygltf::Accessor normal_accessor{};
        normal_accessor.bufferView = 1;
        normal_accessor.byteOffset = offset + 12;
        normal_accessor.normalized = true;
        normal_accessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
        normal_accessor.count = count;
        normal_accessor.type = TINYGLTF_TYPE_VEC3;   

        prim.attributes.insert({ "NORMAL", static_cast<int>(new_accessors.size()) });
        new_accessors.push_back(normal_accessor);
    }
    
    if(hasTexcoord)
    {
        tinygltf::Accessor texcoord_accessor{};
        texcoord_accessor.bufferView = 1;
        texcoord_accessor.byteOffset = offset + 16;
        texcoord_accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        texcoord_accessor.count = count;
        texcoord_accessor.type = TINYGLTF_TYPE_VEC2;

        prim.attributes.insert({"TEXCOORD_0", static_cast<int>(new_accessors.size())});
        new_accessors.push_back(texcoord_accessor);
    }

    if(hasTangents)    
    {
        tinygltf::Accessor tangent_accessor{};
        tangent_accessor.bufferView = 1;
        tangent_accessor.byteOffset = offset + 24;
        tangent_accessor.normalized = true;
        tangent_accessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
        tangent_accessor.count = count;
        tangent_accessor.type = TINYGLTF_TYPE_VEC4;

        prim.attributes.insert({"TANGENT", static_cast<int>(new_accessors.size())});
        new_accessors.push_back(tangent_accessor);
    }
}

void Baker::bakeScene(std::filesystem::path path) 
{
  auto maybeModel = loadModel(path);
  if (!maybeModel.has_value())
    return;
  auto model = std::move(*maybeModel);

  ProcessedMeshes resultMeshes = processMeshes(model);

  model.extensionsRequired.push_back("KHR_mesh_quantization");
  model.extensionsUsed.push_back("KHR_mesh_quantization");

  std::filesystem::path model_path = path.parent_path() / path.stem();
  updateBuffer(model_path, model, resultMeshes);
  updateBufferView(model,resultMeshes);

  std::vector<tinygltf::Accessor> new_accessors;
  for (std::size_t i = 0; i < model.meshes.size(); ++i)
  {
     auto& mesh = model.meshes[i];
     for (std::size_t j = 0; j < mesh.primitives.size(); ++j)
     {
        auto& relem = resultMeshes.relems[resultMeshes.meshes[i].firstRelem + j];
        updateAccessors(new_accessors, mesh.primitives[j], relem);
     }
  }
  model.accessors = std::move(new_accessors);

  std::string filename = model_path.string() + "_baked.gltf";
  saver.SetImagesAsIs(true);
  saver.WriteGltfSceneToFile(&model, filename, false, false, true, false);
}
