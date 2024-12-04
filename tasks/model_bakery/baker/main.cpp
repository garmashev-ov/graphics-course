#include <bit>
#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <filesystem>
#include <tiny_gltf.h>
#include <optional>
#include <iostream>
#include <glm/glm.hpp>
#include <vector>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stack>
#include <etna/GlobalContext.hpp>


struct ProcessedInstances
{
  std::vector<glm::mat4x4> matrices;
  std::vector<std::uint32_t> meshes;
};
struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
  // Not implemented!
  // Material* material;
};

// A mesh is a collection of relems. A scene may have the same mesh
// located in several different places, so a scene consists of **instances**,
// not meshes.
struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

struct Vertex
{
  // First 3 floats are position, 4th float is a packed normal
  glm::vec4 positionAndNormal;
  // First 2 floats are tex coords, 3rd is a packed tangent, 4th is padding
  glm::vec4 texCoordAndTangentAndPadding;
};

struct ProcessedMeshes
{
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<RenderElement> relems;
  std::vector<Mesh> meshes;
};


std::optional<tinygltf::Model> loadModel(std::filesystem::path path)
{
  tinygltf::TinyGLTF loader;

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

ProcessedInstances processInstances(const tinygltf::Model& model)
{
  std::vector nodeTransforms(model.nodes.size(), glm::identity<glm::mat4x4>());

  for (std::size_t nodeIdx = 0; nodeIdx < model.nodes.size(); ++nodeIdx)
  {
    const auto& node = model.nodes[nodeIdx];
    auto& transform = nodeTransforms[nodeIdx];

    if (!node.matrix.empty())
    {
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          transform[i][j] = static_cast<float>(node.matrix[4 * i + j]);
    }
    else
    {
      if (!node.scale.empty())
        transform = scale(
          transform,
          glm::vec3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])));

      if (!node.rotation.empty())
        transform *= mat4_cast(glm::quat(
          static_cast<float>(node.rotation[3]),
          static_cast<float>(node.rotation[0]),
          static_cast<float>(node.rotation[1]),
          static_cast<float>(node.rotation[2])));

      if (!node.translation.empty())
        transform = translate(
          transform,
          glm::vec3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])));
    }
  }

  std::stack<std::size_t> vertices;
  for (auto vert : model.scenes[model.defaultScene].nodes)
    vertices.push(vert);

  while (!vertices.empty())
  {
    auto vert = vertices.top();
    vertices.pop();

    for (auto child : model.nodes[vert].children)
    {
      nodeTransforms[child] = nodeTransforms[vert] * nodeTransforms[child];
      vertices.push(child);
    }
  }

  ProcessedInstances result;

  // Don't overallocate matrices, they are pretty chonky.
  {
    std::size_t totalNodesWithMeshes = 0;
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
      if (model.nodes[i].mesh >= 0)
        ++totalNodesWithMeshes;
    result.matrices.reserve(totalNodesWithMeshes);
    result.meshes.reserve(totalNodesWithMeshes);
  }

  for (std::size_t i = 0; i < model.nodes.size(); ++i)
    if (model.nodes[i].mesh >= 0)
    {
      result.matrices.push_back(nodeTransforms[i]);
      result.meshes.push_back(model.nodes[i].mesh);
    }

  return result;
}

static std::uint32_t encode_normal(glm::vec3 normal)
{
  const std::uint32_t x = static_cast<std::uint32_t>(lround(normal.x * 127.0f) & 0xff);
  const std::uint32_t y = static_cast<std::uint32_t>(lround(normal.y * 127.0f) & 0xff) << 8;
  const std::uint32_t z = static_cast<std::uint32_t>(lround(normal.z * 127.0f) & 0xff) << 16;

  return std::bit_cast<std::uint32_t>(x | y | z);
}


static std::uint32_t encode_best_fit_normal(glm::vec3 normal)
{
  int window = 1;
  std::uint32_t bestFitNormal = encode_normal(normal);
  glm::ivec3 normalFirst = {
    bestFitNormal & 0xff, (bestFitNormal >> 8) & 0xff, (bestFitNormal >> 16) & 0xff};
  normalFirst = ((normalFirst + 128) % 256) - 128;
  double bestError = glm::length(normal - normalize(glm::vec3(normalFirst)));

  for (int i = normal.x <= 0 ? -127 : 0; i <= (normal.x <= 0 ? 0 : 127); ++i)
  {
    int jlb =
      std::max(-127, std::min(int(std::floor(float(i) * normal.y / normal.x)) - window, 127));
    int jub =
      std::min(127, std::max(int(std::ceil(float(i) * normal.y / normal.x)) + window, -127));
    int klb =
      std::max(-127, std::min(int(std::floor(float(i) * normal.z / normal.x)) - window, 127));
    int kub =
      std::min(127, std::max(int(std::ceil(float(i) * normal.z / normal.x)) + window, -127));

    for (int j = jlb; j <= jub; ++j)
    {
      for (int k = klb; k <= kub; ++k)
      {
        glm::vec3 curNormal = {i, j, k};
        curNormal /= 127.0f;
        float curError = glm::length(glm::normalize(curNormal) - normal);
        if (curError < bestError)
        {
          const std::uint32_t sx = static_cast<std::uint32_t>((i + 256) % 256);
          const std::uint32_t sy = static_cast<std::uint32_t>((j + 256) % 256) << 8;
          const std::uint32_t sz = static_cast<std::uint32_t>((k + 256) % 256) << 16;
          bestFitNormal = sx | sy | sz;
          bestError = curError;
        }
      }
    }
  }
  return bestFitNormal;
}


ProcessedMeshes processMeshes(const tinygltf::Model& model, bool use_best_fit_normals = false)
{
  // NOTE: glTF assets can have pretty wonky data layouts which are not appropriate
  // for real-time rendering, so we have to press the data first. In serious engines
  // this is mitigated by storing assets on the disc in an engine-specific format that
  // is appropriate for GPU upload right after reading from disc.

  ProcessedMeshes result;

  // Pre-allocate enough memory so as not to hit the
  // allocator on the memcpy hotpath
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

        if (use_best_fit_normals)
        {
          vtx.positionAndNormal =
            glm::vec4(pos, hasNormals ? std::bit_cast<float>(encode_best_fit_normal(normal)) : 0.f);
          vtx.texCoordAndTangentAndPadding = glm::vec4(
            texcoord, hasTangents ? std::bit_cast<float>(encode_best_fit_normal(tangent)) : 0.f, 0);
        }
        else
        {
          vtx.positionAndNormal =
            glm::vec4(pos, hasNormals ? std::bit_cast<float>(encode_normal(normal)) : 0.f);
          vtx.texCoordAndTangentAndPadding = glm::vec4(
            texcoord, hasTangents ? std::bit_cast<float>(encode_normal(tangent)) : 0.f, 0);
        }
        ptrs[1] += strides[1];
        if (hasNormals)
          ptrs[2] += strides[2];
        if (hasTangents)
          ptrs[3] += strides[3];
        if (hasTexcoord)
          ptrs[4] += strides[4];
      }

      // Indices are guaranteed to have no stride
      ETNA_VERIFY(bufViews[0]->byteStride == 0);
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


int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    spdlog::error("no file in args");
    argv[1] = (char*)"/home/oleg/graphics-course/resources/scenes/low_poly_dark_town/scene.gltf";
    // return -1;
  }
  bool useBestFitNormals = false;
  if (argc > 2)
  {
    useBestFitNormals = true;
  }
  auto path = std::filesystem::path(argv[1]);
  auto maybeModel = loadModel(path);
  if (!maybeModel)
  {
    return -1;
  }

  if (useBestFitNormals)
  {
    spdlog::warn("using best-fit normals, it may take some time");
  }
  auto model = std::move(*maybeModel);
  model.extensionsUsed.push_back("KHR_mesh_quantization");
  model.extensionsRequired.push_back("KHR_mesh_quantization");
  
  auto [verts, inds, relems, meshs] = processMeshes(model, useBestFitNormals);

  model.buffers.resize(1);
  model.buffers[0].data.resize(inds.size() * sizeof(int32_t) + verts.size() * sizeof(Vertex));
  std::memcpy(model.buffers[0].data.data(), inds.data(), inds.size() * sizeof(int32_t));
  std::memcpy(
    model.buffers[0].data.data() + inds.size() * sizeof(int32_t),
    verts.data(),
    verts.size() * sizeof(Vertex));

  model.buffers[0].name = path.stem().string() + "_baked";
  model.buffers[0].uri = path.stem().string() + "_baked.bin";

  model.bufferViews.clear();
  model.bufferViews.resize(2);

  model.bufferViews[0].buffer = 0;
  model.bufferViews[0].byteOffset = 0;
  model.bufferViews[0].byteLength = inds.size() * sizeof(int32_t);
  model.bufferViews[0].byteStride = 0;
  model.bufferViews[0].target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

  model.bufferViews[1].buffer = 0;
  model.bufferViews[1].byteOffset = model.bufferViews[0].byteLength;
  model.bufferViews[1].byteLength = verts.size() * sizeof(Vertex);
  model.bufferViews[1].byteStride = sizeof(Vertex);
  model.bufferViews[1].target = TINYGLTF_TARGET_ARRAY_BUFFER;

  model.accessors.clear();
  std::vector<tinygltf::Accessor> accessors;

  for (size_t i = 0; i < model.meshes.size(); i++)
  {
    for (size_t j = 0; j < model.meshes[i].primitives.size(); j++)
    {
      auto& prim = model.meshes[i].primitives[j];

      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        continue;
      }

      const auto normalIt = prim.attributes.find("NORMAL");
      const auto tangentIt = prim.attributes.find("TANGENT");
      const auto texcoordIt = prim.attributes.find("TEXCOORD_0");

      const bool hasNormals = normalIt != prim.attributes.end();
      const bool hasTangents = tangentIt != prim.attributes.end();
      const bool hasTexcoord = texcoordIt != prim.attributes.end();

      auto& relem = relems[meshs[i].firstRelem + j];

      std::uint32_t maxIndex = inds[relem.indexOffset];
      glm::vec3 maxValues = glm::vec3(verts[relem.vertexOffset].positionAndNormal);
      glm::vec3 minValues = glm::vec3(verts[relem.vertexOffset].positionAndNormal);
      for (std::uint32_t k = 0; k < relem.indexCount; ++k)
      {
        auto& curIndex = inds[relem.indexOffset + k];
        maxIndex = std::max(maxIndex, curIndex);
        maxValues =
          glm::max(maxValues, glm::vec3(verts[relem.vertexOffset + curIndex].positionAndNormal));
        minValues =
          glm::min(minValues, glm::vec3(verts[relem.vertexOffset + curIndex].positionAndNormal));
      }

      std::array<tinygltf::Accessor, 5> curAccessors;

      curAccessors[0].bufferView = 0;
      curAccessors[0].byteOffset = relem.indexOffset * sizeof(int32_t);
      curAccessors[0].count = relem.indexCount;
      curAccessors[0].componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
      curAccessors[0].type = TINYGLTF_TYPE_SCALAR;
      curAccessors[0].normalized = false;


      // coordinates
      curAccessors[1].bufferView = 1;
      curAccessors[1].byteOffset = relem.vertexOffset * sizeof(Vertex);
      curAccessors[1].count = maxIndex + 1;
      curAccessors[1].maxValues = {maxValues.x, maxValues.y, maxValues.z};
      curAccessors[1].minValues = {minValues.x, minValues.y, minValues.z};
      curAccessors[1].componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
      curAccessors[1].type = TINYGLTF_TYPE_VEC3;
      curAccessors[1].normalized = false;


      // normal
      curAccessors[2].bufferView = 1;
      curAccessors[2].byteOffset = 12 + relem.vertexOffset * sizeof(Vertex);
      curAccessors[2].count = maxIndex + 1;
      curAccessors[2].normalized = !useBestFitNormals;
      curAccessors[2].componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
      curAccessors[2].type = TINYGLTF_TYPE_VEC3;

      // texture coords
      curAccessors[3].bufferView = 1;
      curAccessors[3].byteOffset = 16 + relem.vertexOffset * sizeof(Vertex);
      curAccessors[3].count = maxIndex + 1;
      curAccessors[3].componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
      curAccessors[3].type = TINYGLTF_TYPE_VEC2;
      curAccessors[3].normalized = false;


      // tangent
      curAccessors[4].bufferView = 1;
      curAccessors[4].byteOffset = 24 + relem.vertexOffset * sizeof(Vertex);
      curAccessors[4].count = maxIndex + 1;
      curAccessors[4].normalized = true;
      curAccessors[4].componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
      curAccessors[4].type = TINYGLTF_TYPE_VEC3;

      prim.indices = static_cast<int>(accessors.size());
      accessors.push_back(curAccessors[0]);

      prim.attributes.clear();

      prim.attributes.insert({"POSITION", static_cast<int>(accessors.size())});
      accessors.push_back(curAccessors[1]);

      if (hasNormals)
      {
        prim.attributes.insert({"NORMAL", static_cast<int>(accessors.size())});
        accessors.push_back(curAccessors[2]);
      }
      if (hasTexcoord)
      {
        prim.attributes.insert({"TEXCOORD_0", static_cast<int>(accessors.size())});
        accessors.push_back(curAccessors[3]);
      }
      if (hasTangents)
      {
        prim.attributes.insert({"TANGENT", static_cast<int>(accessors.size())});
        accessors.push_back(curAccessors[4]);
      }
    }
  }
  model.accessors = std::move(accessors);
  tinygltf::TinyGLTF loader;
  auto outputPath = (path.parent_path() / path.stem()).string() + "_baked.gltf";
  loader.WriteGltfSceneToFile(&model, outputPath, false, false, true, false);
  return 0;
}
