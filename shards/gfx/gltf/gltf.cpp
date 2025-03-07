#include "gltf.hpp"
#include "gfx/drawables/mesh_tree_drawable.hpp"
#include "gfx/gltf/gltf.hpp"
#include "gfx/linalg.hpp"
#include "linalg.h"
#include "spdlog/spdlog.h"
#include <SDL_config.h>
#include <algorithm>
#include <cmath>
#include <gfx/error_utils.hpp>
#include <gfx/filesystem.hpp>
#include <gfx/material.hpp>
#include <gfx/mesh.hpp>
#include <gfx/texture.hpp>
#include <gfx/texture_file/texture_file.hpp>
#include <gfx/drawables/mesh_drawable.hpp>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <string>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_ENABLE_DRACO
#include <tinygltf/tiny_gltf.h>
using namespace tinygltf;
#pragma GCC diagnostic pop

namespace gfx {

static WGPUAddressMode convertAddressMode(int mode) {
  if (mode < 0)
    return WGPUAddressMode_Repeat;

  switch (mode) {
  case TINYGLTF_TEXTURE_WRAP_REPEAT:
    return WGPUAddressMode_Repeat;
  case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
    return WGPUAddressMode_ClampToEdge;
  case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
    return WGPUAddressMode_MirrorRepeat;
  }
  assert(false);
  return WGPUAddressMode(~0);
}

static WGPUFilterMode convertFilterMode(int mode) {
  if (mode < 0)
    return WGPUFilterMode_Linear;

  switch (mode) {
  case TINYGLTF_TEXTURE_FILTER_NEAREST:
    return WGPUFilterMode_Nearest;
  case TINYGLTF_TEXTURE_FILTER_LINEAR:
    return WGPUFilterMode_Linear;
    // NOTE: Mipmap filtering mode is ignored
  case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
    return WGPUFilterMode_Nearest;
  case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
    return WGPUFilterMode_Linear;
  case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    return WGPUFilterMode_Nearest;
  case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    return WGPUFilterMode_Linear;
  }
  assert(false);
  return WGPUFilterMode(~0);
}

static SamplerState convertSampler(const tinygltf::Sampler &sampler) {
  SamplerState samplerState{};
  samplerState.addressModeU = convertAddressMode(sampler.wrapS);
  samplerState.addressModeV = convertAddressMode(sampler.wrapT);
  samplerState.filterMode = convertFilterMode(sampler.minFilter);
  return samplerState;
}

static TextureFormat convertTextureFormat(const tinygltf::Image &image) {
  TextureFormat result{
      .dimension = TextureDimension::D2,
  };

  auto throwUnsupportedPixelFormat = [&]() {
    throw formatException("Unsupported pixel format for {} components: {} ", image.component, image.pixel_type);
  };
  if (image.component == 4) {
    if (image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
      result.pixelFormat = WGPUTextureFormat_RGBA8UnormSrgb;
    else if (image.pixel_type == TINYGLTF_COMPONENT_TYPE_FLOAT)
      result.pixelFormat = WGPUTextureFormat_RGBA32Float;
    else
      throwUnsupportedPixelFormat();
  } else if (image.component == 2) {
    if (image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
      result.pixelFormat = WGPUTextureFormat_RG8Unorm;
    else if (image.pixel_type == TINYGLTF_COMPONENT_TYPE_FLOAT)
      result.pixelFormat = WGPUTextureFormat_RG32Float;
    else
      throwUnsupportedPixelFormat();
  } else if (image.component == 1) {
    if (image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
      result.pixelFormat = WGPUTextureFormat_R8Unorm;
    else if (image.pixel_type == TINYGLTF_COMPONENT_TYPE_FLOAT)
      result.pixelFormat = WGPUTextureFormat_R32Float;
    else
      throwUnsupportedPixelFormat();
  } else {
    throw formatException("{} component textures not supported", image.component);
  }
  return result;
}

static float4 convertFloat4Vec(const std::vector<double> &v) {
  assert(v.size() == 4);
  return float4(v[0], v[1], v[2], v[3]);
}

static TRS convertNodeTransform(const tinygltf::Node &node) {
  if (node.matrix.size() != 0) {
    return TRS(float4x4(double4x4(node.matrix.data())));
  } else {
    TRS result;
    result.translation = float3(node.translation.size() != 0 ? double3(node.translation.data()) : double3());
    result.rotation = float4(node.rotation.size() != 0 ? double4(node.rotation.data()) : double4(0, 0, 0, 1));
    result.scale = float3(node.scale.size() != 0 ? double3(node.scale.data()) : double3(1, 1, 1));
    return result;
  }
}

struct Loader {
  struct Mesh {
    std::vector<MeshDrawable::Ptr> primitives;
  };

  tinygltf::Model &model;
  std::vector<MeshPtr> primitiveMap;
  std::vector<Mesh> meshMap;
  std::vector<MaterialPtr> materialMap;
  std::vector<TexturePtr> textureMap;
  std::vector<MeshTreeDrawable::Ptr> nodeMap;
  std::vector<MeshTreeDrawable::Ptr> sceneMap;
  std::unordered_map<std::string, gfx::Animation> animations;

  Loader(tinygltf::Model &model) : model(model) {}

  std::string translateAttributeName(const std::string &in) {
    std::string nameLower;
    nameLower.resize(in.size());
    std::transform(in.begin(), in.end(), nameLower.begin(), [](unsigned char c) { return std::tolower(c); });

    const char texcoordPrefix[] = "texcoord_";
    const char colorPrefix[] = "color_";
    if (nameLower.starts_with(texcoordPrefix)) {
      std::string numberSuffix = nameLower.substr(sizeof(texcoordPrefix) - 1);
      return std::string("texCoord") + (numberSuffix.empty() ? "0" : numberSuffix.c_str());
    } else if (nameLower.starts_with(colorPrefix)) {
      return std::string("color") + nameLower.substr(sizeof(colorPrefix) - 1);
    } else {
      return nameLower;
    }
  }

  void reorderVertexAttributes(std::vector<const tinygltf::Accessor *> &accessors, std::vector<size_t> &offsets,
                               MeshFormat &format) {
    int positionIndex = -1;
    for (size_t i = 0; i < format.vertexAttributes.size(); i++) {
      if (format.vertexAttributes[i].name == "position") {
        positionIndex = i;
        break;
      }
    }

    // Make position the first attribute to aid renderdoc debugging
    if (positionIndex > 0) {
      std::swap(accessors[0], accessors[positionIndex]);
      std::swap(format.vertexAttributes[0], format.vertexAttributes[positionIndex]);

      // Recompute offsets
      size_t offset = 0;
      for (size_t i = 0; i < format.vertexAttributes.size(); i++) {
        auto &attrib = format.vertexAttributes[i];
        offsets[i] = offset;
        offset += getStorageTypeSize(attrib.type) * attrib.numComponents;
      }
    }
  }

  MeshDrawable::Ptr loadPrimitive(tinygltf::Primitive &primitive) {
    std::vector<const tinygltf::Accessor *> accessors;
    std::vector<size_t> offsets;
    size_t vertexStride{};
    size_t numVertices{};
    MeshFormat format;

    for (auto &gltfAttrib : primitive.attributes) {
      auto &gltfAccessor = model.accessors[gltfAttrib.second];

      MeshVertexAttribute &attrib = format.vertexAttributes.emplace_back();
      attrib.name = translateAttributeName(gltfAttrib.first);
      attrib.numComponents = tinygltf::GetNumComponentsInType(gltfAccessor.type);
      switch (gltfAccessor.componentType) {
      case TINYGLTF_COMPONENT_TYPE_BYTE:
        attrib.type = StorageType::Int8;
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        attrib.type = StorageType::UInt8;
        break;
      case TINYGLTF_COMPONENT_TYPE_SHORT:
        attrib.type = StorageType::Int16;
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        attrib.type = StorageType::UInt16;
        break;
      case TINYGLTF_COMPONENT_TYPE_INT:
        attrib.type = StorageType::Int32;
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        attrib.type = StorageType::UInt32;
        break;
      case TINYGLTF_COMPONENT_TYPE_FLOAT:
        attrib.type = StorageType::Float32;
        break;
      case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        throw formatException("Accessor {}: double component type is not supported", gltfAttrib.second);
        break;
      }
      accessors.push_back(&gltfAccessor);
      offsets.push_back(vertexStride);
      vertexStride += getStorageTypeSize(attrib.type) * attrib.numComponents;

      if (numVertices == 0) {
        numVertices = gltfAccessor.count;
      } else {
        assert(gltfAccessor.count == numVertices);
      }
    }

    reorderVertexAttributes(accessors, offsets, format);

    std::vector<uint8_t> vertexBuffer;
    vertexBuffer.resize(vertexStride * numVertices);
    for (size_t attribIndex{}; attribIndex < accessors.size(); attribIndex++) {
      size_t dstOffset = offsets[attribIndex];
      const tinygltf::Accessor &accessor = *accessors[attribIndex];
      const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
      const MeshVertexAttribute &attrib = format.vertexAttributes[attribIndex];

      size_t elementSize = getStorageTypeSize(attrib.type) * attrib.numComponents;
      int srcStride = accessor.ByteStride(bufferView);

      const uint8_t *srcPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
      uint8_t *dstPtr = vertexBuffer.data() + dstOffset;
      for (size_t i = 0; i < numVertices; i++) {
        memcpy(dstPtr, srcPtr, elementSize);
        srcPtr += srcStride;
        dstPtr += vertexStride;
      }
    }

    std::vector<uint8_t> indexBuffer;
    bool requireConversion{};
    if (primitive.indices >= 0) {
      const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
      const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
      switch (accessor.componentType) {
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        format.indexFormat = IndexFormat::UInt16;
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        format.indexFormat = IndexFormat::UInt32;
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        format.indexFormat = IndexFormat::UInt16;
        requireConversion = true;
        break;
      default:
        throw formatException("Accessor {}: Unsupported index format", primitive.indices);
      }

      size_t dstElementSize = getIndexFormatSize(format.indexFormat);
      int srcStride = accessor.ByteStride(bufferView);

      indexBuffer.resize(dstElementSize * accessor.count);
      uint8_t *dstPtr = indexBuffer.data();
      const uint8_t *srcPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
      if (requireConversion) {
        for (size_t i = 0; i < accessor.count; i++) {
          switch (accessor.componentType) {
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            ((uint16_t *)dstPtr)[0] = uint16_t(((uint8_t *)srcPtr)[i]);
            break;
          }
          dstPtr += dstElementSize;
          srcPtr += srcStride;
        }
      } else {
        for (size_t i = 0; i < accessor.count; i++) {
          memcpy(dstPtr, srcPtr, dstElementSize);
          dstPtr += dstElementSize;
          srcPtr += srcStride;
        }
      }
    }

    switch (primitive.mode) {
    case TINYGLTF_MODE_TRIANGLES:
      format.primitiveType = PrimitiveType::TriangleList;
      break;
    case TINYGLTF_MODE_TRIANGLE_STRIP:
      format.primitiveType = PrimitiveType::TriangleStrip;
      break;
    default:
      throw formatException("Unsupported primitive mode ({})", primitive.mode);
    }

    MeshPtr mesh = std::make_shared<gfx::Mesh>();
    mesh->update(format, std::move(vertexBuffer), std::move(indexBuffer));

    MeshDrawable::Ptr result = std::make_shared<MeshDrawable>(mesh);
    if (primitive.material >= 0) {
      result->material = materialMap[primitive.material];
    }

    return result;
  }

  void loadMeshes() {
    size_t numMeshes = model.meshes.size();
    meshMap.resize(numMeshes);

    for (size_t i = 0; i < numMeshes; i++) {
      auto &mesh = meshMap[i];

      auto &gltfMesh = model.meshes[i];
      for (auto &gltfPrim : gltfMesh.primitives) {
        MeshDrawable::Ptr primitive = loadPrimitive(gltfPrim);
        mesh.primitives.push_back(primitive);
      }
    }
  }

  void initNode(size_t nodeIndex) {
    MeshTreeDrawable::Ptr &node = nodeMap[nodeIndex];
    node = std::make_shared<MeshTreeDrawable>();

    const Node &gltfNode = model.nodes[nodeIndex];
    node->label = gltfNode.name;

    if (gltfNode.mesh >= 0) {
      for (auto &prim : meshMap[gltfNode.mesh].primitives) {
        node->drawables.push_back(prim);
      }
    }

    node->trs = convertNodeTransform(gltfNode);

    for (size_t i = 0; i < gltfNode.children.size(); i++) {
      int childNodeIndex = gltfNode.children[i];
      MeshTreeDrawable::Ptr &childNode = nodeMap[childNodeIndex];
      if (!childNode) {
        childNode = std::make_shared<MeshTreeDrawable>();
        initNode(childNodeIndex);
      }
      node->addChild(childNode);
    }
  }

  void loadNodes() {
    size_t numNodes = model.nodes.size();
    nodeMap.resize(numNodes);
    for (size_t i = 0; i < numNodes; i++) {
      if (!nodeMap[i])
        initNode(i);
    }
  }

  void loadTextures() {
    size_t numTextures = model.textures.size();
    textureMap.resize(numTextures);
    for (size_t i = 0; i < numTextures; i++) {
      const tinygltf::Texture &gltfTexture = model.textures[i];
      const tinygltf::Image &gltfImage = model.images[gltfTexture.source];

      TexturePtr &texture = textureMap[i];
      texture = std::make_shared<Texture>();

      SamplerState samplerState{};
      if (gltfTexture.sampler >= 0) {
        samplerState = convertSampler(model.samplers[gltfTexture.sampler]);
      }

      int2 resolution(gltfImage.width, gltfImage.height);
      texture
          ->init(TextureDesc{
              .format = convertTextureFormat(gltfImage),
              .resolution = resolution,
              .data = ImmutableSharedBuffer(gltfImage.image.data(), gltfImage.image.size()),
          })
          .initWithSamplerState(samplerState);
    }
  }

  void loadMaterials() {
    size_t numMaterials = model.materials.size();
    materialMap.resize(numMaterials);
    for (size_t i = 0; i < model.materials.size(); i++) {
      MaterialPtr &material = materialMap[i];
      material = std::make_shared<Material>();

      const tinygltf::Material &gltfMaterial = model.materials[i];

      auto convertTextureParam = [&](const char *name, const TextureInfo &textureInfo) {
        if (textureInfo.index >= 0) {
          TexturePtr texture = textureMap[textureInfo.index];
          material->parameters.set(name, TextureParameter(texture, textureInfo.texCoord));
        }
      };

      auto convertOptionalFloat4Param = [&](const char *name, const std::vector<double> &param, const float4 &defaultValue) {
        float4 f4val = convertFloat4Vec(param);
        if (f4val != defaultValue) {
          material->parameters.set(name, f4val);
        }
      };
      auto convertOptionalFloatParam = [&](const char *name, const float &param, const float &defaultValue) {
        if (param != defaultValue) {
          material->parameters.set(name, param);
        }
      };

      convertOptionalFloat4Param("baseColor", gltfMaterial.pbrMetallicRoughness.baseColorFactor, float4(1, 1, 1, 1));
      convertOptionalFloatParam("roughness", gltfMaterial.pbrMetallicRoughness.roughnessFactor, 1.0f);
      convertOptionalFloatParam("metallic", gltfMaterial.pbrMetallicRoughness.metallicFactor, 0.0f);
      convertTextureParam("baseColorTexture", gltfMaterial.pbrMetallicRoughness.baseColorTexture);
      convertTextureParam("metallicRougnessTexture", gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture);
    }
  }

  animation::Track loadAnimationTrack(const tinygltf::Animation &gltfAnimation, const tinygltf::AnimationChannel &gltfChannel) {
    auto &gltfAnimSampler = gltfAnimation.samplers[gltfChannel.sampler];
    auto &input = model.accessors[gltfAnimSampler.input];
    auto &output = model.accessors[gltfAnimSampler.output];

    animation::Track result;
    if (gltfAnimSampler.interpolation == "LINEAR") {
      result.interpolation = animation::Interpolation::Linear;
    } else if (gltfAnimSampler.interpolation == "STEP") {
      result.interpolation = animation::Interpolation::Step;
    } else if (gltfAnimSampler.interpolation == "CUBICSPLINE") {
      result.interpolation = animation::Interpolation::Cubic;
    } else {
      throw formatException("Interpolation mode not supported: {}", gltfAnimSampler.interpolation);
    }

    const tinygltf::BufferView &inputBufferView = model.bufferViews[input.bufferView];
    const tinygltf::BufferView &outputBufferView = model.bufferViews[output.bufferView];
    const tinygltf::Buffer &inputBuffer = model.buffers[inputBufferView.buffer];
    const tinygltf::Buffer &outputBuffer = model.buffers[outputBufferView.buffer];
    const uint8_t *inputData = inputBuffer.data.data() + inputBufferView.byteOffset + input.byteOffset;
    const uint8_t *outputData = outputBuffer.data.data() + outputBufferView.byteOffset + output.byteOffset;

    size_t numFrames = input.count;
    size_t numComponents = GetNumComponentsInType(output.type);
    result.elementSize = numComponents;
    for (size_t i = 0; i < numFrames; i++) {
      const float &time = *(float *)(inputData + input.ByteStride(inputBufferView) * i);
      result.times.emplace_back(time);
    }

    // Convert animation values to float
    size_t numValues = numFrames;
    if (result.interpolation == animation::Interpolation::Cubic)
      numValues = numFrames * 3;
    assert(numValues == output.count);
    result.data.resize(numComponents * numValues);

    for (size_t i = 0; i < numValues; i++) {
      float *outData = result.data.data() + numComponents * i;
      const uint8_t *inputData = outputData + output.ByteStride(outputBufferView) * i;
      switch (output.componentType) {
      case TINYGLTF_COMPONENT_TYPE_FLOAT:
        for (size_t c = 0; c < numComponents; c++)
          outData[c] = ((float *)inputData)[c];
        break;
      case TINYGLTF_COMPONENT_TYPE_BYTE:
        for (size_t c = 0; c < numComponents; c++)
          outData[c] = std::max((float)((int8_t *)inputData)[c] / float(0x7f), -1.0f);
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        for (size_t c = 0; c < numComponents; c++)
          outData[c] = (float)((uint8_t *)inputData)[c] / float(0xff);
        break;
      case TINYGLTF_COMPONENT_TYPE_SHORT:
        for (size_t c = 0; c < numComponents; c++)
          outData[c] = std::max((float)((int16_t *)inputData)[c] / float(0x7fff), -1.0f);
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        for (size_t c = 0; c < numComponents; c++)
          outData[c] = (float)((uint16_t *)inputData)[c] / float(0x7fff);
        break;
      default:
        throw formatException("Invalid component type for animation {}/{}: {}", gltfAnimation.name, output.name,
                              output.componentType);
      }
    }

    result.targetNode = this->nodeMap[gltfChannel.target_node];
    if (gltfChannel.target_path == "rotation") {
      result.target = animation::BuiltinTarget::Rotation;
      if (numComponents != 4)
        throw formatException("Invalid animation {}: rotation requires 4 components", gltfAnimation.name);
    } else if (gltfChannel.target_path == "translation") {
      result.target = animation::BuiltinTarget::Translation;
      if (numComponents != 3)
        throw formatException("Invalid animation {}: translation requires 3 components", gltfAnimation.name);
    } else if (gltfChannel.target_path == "scale") {
      result.target = animation::BuiltinTarget::Scale;
      if (numComponents != 3)
        throw formatException("Invalid animation {}: scale requires 3 components", gltfAnimation.name);
    } else if (gltfChannel.target_path == "weights") {
      SPDLOG_WARN("morph target animations are not implemented");
    } else {
      throw formatException("Invalid animation target {}: {}", gltfAnimation.name, gltfChannel.target_path);
    }

    return result;
  }

  void loadAnimations() {
    size_t numAnimations = model.animations.size();
    for (size_t i = 0; i < numAnimations; i++) {
      tinygltf::Animation &gltfAnimation = model.animations[i];
      auto &animation = animations.emplace(gltfAnimation.name, Animation()).first->second;

      for (auto &gltfChannel : gltfAnimation.channels) {
        animation.tracks.emplace_back(loadAnimationTrack(gltfAnimation, gltfChannel));
      }
    }
  }

  void load() {
    loadTextures();
    loadMaterials();
    loadMeshes();
    loadNodes();
    loadAnimations();

    size_t numScenes = model.scenes.size();
    sceneMap.resize(numScenes);
    for (size_t i = 0; i < numScenes; i++) {
      const Scene &gltfScene = model.scenes[i];

      MeshTreeDrawable::Ptr &scene = sceneMap[i];

      if (gltfScene.nodes.size() == 1) {
        // Extract single model node
        scene = nodeMap[gltfScene.nodes[0]];
      } else {
        // Group into parent node
        scene = std::make_shared<MeshTreeDrawable>();
        for (size_t i = 0; i < gltfScene.nodes.size(); i++) {
          int nodeIndex = gltfScene.nodes[i];
          MeshTreeDrawable::Ptr node = nodeMap[nodeIndex];
          scene->addChild(node);
        }
      }
    }
  }
};

template <typename T> glTF load(T loader) {
  tinygltf::Model model;
  loader(model);

  if (model.defaultScene == -1) {
    throw formatException("glTF model has no default scene");
  }

  Loader gfxLoader(model);
  gfxLoader.load();

  glTF result;
  result.root = std::move(gfxLoader.sceneMap[model.defaultScene]);
  result.animations = std::move(gfxLoader.animations);
  return result;
}

static inline bool isPossiblyText(const fs::path &inFilepath) {
  const auto &ext = inFilepath.extension();
  return ext == ".gltf" || ext == ".json" || ext == ".txt";
}

glTF loadGltfFromFile(std::string_view inFilepath) {
  tinygltf::TinyGLTF context;
  auto loader = [&](tinygltf::Model &model) {
    fs::path filepath((std::string(inFilepath)));

    if (!fs::exists(filepath)) {
      throw formatException("glTF model file \"{}\" does not exist", filepath.string());
    }

    std::string err;
    std::string warn;
    bool success{};
    if (isPossiblyText(filepath)) {
      success = context.LoadASCIIFromFile(&model, &err, &warn, filepath.string());
    } else {
      success = context.LoadBinaryFromFile(&model, &err, &warn, filepath.string());
    }

    if (!success) {
      throw formatException("Failed to load glTF model \"{}\"\nErrors: {}\nWarnings: {}", filepath.string(), err, warn);
    }
  };

  return load(loader);
}

glTF loadGltfFromMemory(const uint8_t *data, size_t dataLength) {
  tinygltf::TinyGLTF context;
  auto loader = [&](tinygltf::Model &model) {
    std::string err;
    std::string warn;
    bool success{};
    success = context.LoadBinaryFromMemory(&model, &err, &warn, data, dataLength);

    if (!success) {
      throw formatException("Failed to load glTF model \nErrors: {}\nWarnings: {}", err, warn);
    }
  };

  return load(loader);
}
} // namespace gfx
