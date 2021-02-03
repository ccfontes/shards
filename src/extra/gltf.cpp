/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2021 Giovanni Petrantoni */

#include "./bgfx.hpp"
#include "blocks/shared.hpp"
#include "linalg-shim.hpp"
#include "runtime.hpp"

#include <deque>
#include <filesystem>
#include <optional>
namespace fs = std::filesystem;
using LastWriteTime = decltype(fs::last_write_time(fs::path()));

#include <boost/algorithm/string.hpp>

#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_USE_CPP14
// #define TINYGLTF_ENABLE_DRACO
#include <tinygltf/tiny_gltf.h>
using GLTFModel = tinygltf::Model;
using namespace tinygltf;
#undef Model

/*
TODO:
GLTF.Draw - depending on GFX blocks
GLTF.Simulate - depending on physics simulation blocks
*/
namespace chainblocks {
namespace gltf {
struct GFXPrimitive {
  bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout{};
  uint64_t stateFlags{0};

  GFXPrimitive() {}

  GFXPrimitive(GFXPrimitive &&other) {
    std::swap(vb, other.vb);
    std::swap(ib, other.ib);
    std::swap(layout, other.layout);
  }

  ~GFXPrimitive() {
    if (vb.idx != bgfx::kInvalidHandle) {
      bgfx::destroy(vb);
    }
    if (ib.idx != bgfx::kInvalidHandle) {
      bgfx::destroy(ib);
    }
  }
};

using GFXPrimitiveRef = std::reference_wrapper<GFXPrimitive>;

struct GFXMesh {
  std::string name;
  std::vector<GFXPrimitiveRef> primitives;
};

using GFXMeshRef = std::reference_wrapper<GFXMesh>;

struct Node;
using NodeRef = std::reference_wrapper<Node>;

struct Node {
  std::string name;
  Mat4 transform;

  std::optional<GFXMeshRef> mesh;

  std::vector<NodeRef> children;
};

struct Model {
  std::deque<Node> nodes;
  std::deque<GFXMesh> gfxMeshes;
  std::deque<GFXPrimitive> gfxPrimitives;
  std::optional<NodeRef> rootNode;
};

struct Load {
  static constexpr uint32_t ModelCC = 'gltf';
  static inline Type ModelType{
      {CBType::Object, {.object = {.vendorId = CoreCC, .typeId = ModelCC}}}};
  static inline Type ModelVarType = Type::VariableOf(ModelType);
  static inline ObjectVar<Model> ModelVar{"GLTF-Model", CoreCC, ModelCC};

  static CBTypesInfo inputTypes() { return CoreInfo::StringType; }
  static CBTypesInfo outputTypes() { return ModelType; }

  static inline Parameters Params{
      {"Bitangents",
       CBCCSTR("If we should generate bitangents when loading the model, from "
               "normals and tangent data. Default is true"),
       {CoreInfo::BoolType}}};

  Model *_model{nullptr};
  TinyGLTF _loader;
  size_t _fileNameHash;
  LastWriteTime _fileLastWrite;
  bool _bitangents{true};

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _bitangents = value.payload.boolValue;
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return Var(_bitangents);
    default:
      throw InvalidParameterIndex();
    }
  }

  void cleanup() {
    if (_model) {
      ModelVar.Release(_model);
      _model = nullptr;
    }
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    return awaitne(context, [&]() {
      GLTFModel gltf;
      std::string err;
      std::string warn;
      bool success = false;
      const auto filename = input.payload.stringValue;
      fs::path filepath(filename);
      const auto &ext = filepath.extension();
      const auto hash = std::hash<std::string_view>()(filename);

      if (!fs::exists(filepath)) {
        throw ActivationError("GLTF model file does not exist.");
      }

      if (_model) {
        ModelVar.Release(_model);
        _model = nullptr;
      }
      _model = ModelVar.New();

      _fileNameHash = hash;
      _fileLastWrite = fs::last_write_time(filepath);
      if (ext == ".glb") {
        success = _loader.LoadBinaryFromFile(&gltf, &err, &warn, filename);
      } else {
        success = _loader.LoadASCIIFromFile(&gltf, &err, &warn, filename);
      }

      if (!warn.empty()) {
        LOG(WARNING) << "GLTF warning: " << warn;
      }
      if (!err.empty()) {
        LOG(ERROR) << "GLTF error: " << err;
      }
      if (!success) {
        throw ActivationError("Failed to parse GLTF.");
      }

      if (gltf.defaultScene == -1) {
        throw ActivationError("GLTF model had no default scene.");
      }

      const auto &scene = gltf.scenes[gltf.defaultScene];
      for (const int gltfNodeIdx : scene.nodes) {
        const auto &glnode = gltf.nodes[gltfNodeIdx];
        const std::function<NodeRef(const tinygltf::Node)> processNode =
            [this, &gltf, &processNode](const tinygltf::Node &glnode) {
              Node node{glnode.name};

              if (glnode.matrix.size() != 0) {
                node.transform = Mat4::FromVector(glnode.matrix);
              } else {
                const auto t = linalg::translation_matrix(
                    glnode.translation.size() != 0
                        ? Vec3::FromVector(glnode.translation)
                        : Vec3());
                const auto r = linalg::rotation_matrix(
                    glnode.rotation.size() != 0
                        ? Vec4::FromVector(glnode.rotation)
                        : Vec4::Quaternion());
                const auto s = linalg::scaling_matrix(
                    glnode.scale.size() != 0 ? Vec3::FromVector(glnode.scale)
                                             : Vec3(1.0, 1.0, 1.0));
                node.transform = linalg::mul(linalg::mul(t, r), s);
              }

              // if (glnode.skin != -1) {
              //   // TODO
              // }

              if (glnode.mesh != -1) {
                const auto &glmesh = gltf.meshes[glnode.mesh];
                GFXMesh mesh{glmesh.name};
                for (const auto &glprims : glmesh.primitives) {
                  GFXPrimitive prims{};
                  // we gotta do few things here
                  // build a layout
                  // populate vb and ib
                  std::vector<std::pair<bgfx::Attrib::Enum,
                                        std::reference_wrapper<Accessor>>>
                      accessors;
                  uint32_t vertexSize = 0;
                  for (const auto &[attributeName, attributeIdx] :
                       glprims.attributes) {
                    if (attributeName == "POSITION") {
                      accessors.emplace_back(bgfx::Attrib::Position,
                                             gltf.accessors[attributeIdx]);
                      vertexSize += sizeof(float) * 3;
                    } else if (attributeName == "NORMAL") {
                      accessors.emplace_back(bgfx::Attrib::Normal,
                                             gltf.accessors[attributeIdx]);
                      vertexSize += sizeof(float) * 3;
                    } else if (attributeName == "TANGENT") {
                      accessors.emplace_back(bgfx::Attrib::Tangent,
                                             gltf.accessors[attributeIdx]);
                      if (_bitangents)
                        vertexSize += sizeof(float) * 6;
                      else
                        vertexSize += sizeof(float) * 4;
                    } else if (boost::starts_with(attributeName, "TEXCOORD_")) {
                      int strIndex = std::stoi(attributeName.substr(9));
                      if (strIndex >= 8) {
                        throw ActivationError("GLTF TEXCOORD_ limit exceeded.");
                      }
                      auto texcoord = decltype(bgfx::Attrib::TexCoord0)(
                          int(bgfx::Attrib::TexCoord0) + strIndex);
                      accessors.emplace_back(texcoord,
                                             gltf.accessors[attributeIdx]);
                      vertexSize += sizeof(float) * 2;
                    } else if (boost::starts_with(attributeName, "COLOR_")) {
                      int strIndex = std::stoi(attributeName.substr(6));
                      if (strIndex >= 4) {
                        throw ActivationError("GLTF COLOR_ limit exceeded.");
                      }
                      auto color = decltype(bgfx::Attrib::Color0)(
                          int(bgfx::Attrib::Color0) + strIndex);
                      accessors.emplace_back(color,
                                             gltf.accessors[attributeIdx]);
                      vertexSize += 4;
                    } else {
                      // TODO JOINTS_ and WEIGHTS_
                      LOG(WARNING)
                          << "Ignored a primitive attribute: " << attributeName;
                    }
                  }

                  // lay our data following enum order, pos, normals etc
                  std::sort(accessors.begin(), accessors.end(),
                            [](const auto &a, const auto &b) {
                              return a.first < b.first;
                            });

                  if (accessors.size() > 0) {
                    const auto &[_, ar] = accessors[0];
                    const auto vertexCount = ar.get().count;
                    const auto totalSize = uint32_t(vertexCount) * vertexSize;
                    auto vbuffer = bgfx::alloc(totalSize);
                    auto accessorIdx = 0;
                    auto offsetSize = 0;
                    std::vector<Vec3>
                        normals; // store normals to generate bitangents
                    prims.layout.begin();
                    for (const auto &[attrib, accessorRef] : accessors) {
                      const auto &accessor = accessorRef.get();
                      const auto &view = gltf.bufferViews[accessor.bufferView];
                      const auto &buffer = gltf.buffers[view.buffer];
                      const auto dataBeg =
                          buffer.data.begin() + view.byteOffset;
                      const auto dataEnd = dataBeg + view.byteLength;
                      switch (attrib) {
                      case bgfx::Attrib::Position: {
                        const auto size = sizeof(float) * 3;
                        const auto skips = size + view.byteStride;
                        auto vbufferOffset = offsetSize;
                        offsetSize += size;

                        if (accessor.componentType !=
                                TINYGLTF_COMPONENT_TYPE_FLOAT ||
                            accessor.type != TINYGLTF_TYPE_VEC3) {
                          throw ActivationError("Position vector data was not "
                                                "a float32 vector of 3");
                        }

                        for (auto it = dataBeg; it != dataEnd; it += skips) {
                          const uint8_t *chunk = &(*it);
                          memcpy(vbuffer->data + vbufferOffset, chunk, size);
                          vbufferOffset += vertexSize;
                        }

                        // also update layout
                        prims.layout.add(attrib, 3, bgfx::AttribType::Float);
                      } break;
                      case bgfx::Attrib::Normal: {
                        const auto size = sizeof(float) * 3;
                        const auto skips = size + view.byteStride;
                        auto vbufferOffset = offsetSize;
                        offsetSize += size;

                        if (accessor.componentType !=
                                TINYGLTF_COMPONENT_TYPE_FLOAT ||
                            accessor.type != TINYGLTF_TYPE_VEC3) {
                          throw ActivationError("Normal vector data was not a "
                                                "float32 vector of 3");
                        }

                        for (auto it = dataBeg; it != dataEnd; it += skips) {
                          const float *chunk = (float *)&(*it);
                          memcpy(vbuffer->data + vbufferOffset, chunk, size);
                          vbufferOffset += vertexSize;
                          normals.emplace_back(chunk[0], chunk[1], chunk[2]);
                        }

                        // also update layout
                        prims.layout.add(attrib, 3, bgfx::AttribType::Float);
                      } break;
                      case bgfx::Attrib::Tangent: {
                        if (_bitangents) {
                          const auto gsize = sizeof(float) * 4;
                          const auto gskips = gsize + view.byteStride;
                          const auto osize = sizeof(float) * 6;
                          const auto ssize = sizeof(float) * 3;
                          auto vbufferOffset = offsetSize;
                          offsetSize += osize;

                          if (accessor.componentType !=
                                  TINYGLTF_COMPONENT_TYPE_FLOAT ||
                              accessor.type != TINYGLTF_TYPE_VEC4) {
                            throw ActivationError(
                                "Tangent vector data was not a "
                                "float32 vector of 4");
                          }

                          if (normals.size() == 0) {
                            throw ActivationError(
                                "Got Tangent without normals");
                          }

                          auto idx = 0;
                          for (auto it = dataBeg; it != dataEnd; it += gskips) {
                            const float *chunk = (float *)&(*it);
                            memcpy(vbuffer->data + vbufferOffset, chunk, gsize);

                            auto &normal = normals[idx];
                            Vec3 tangent(chunk[0], chunk[1], chunk[2]);
                            float w = chunk[3];
                            auto bitangent =
                                linalg::cross(linalg::normalize(normal),
                                              linalg::normalize(tangent)) *
                                w;
                            memcpy(vbuffer->data + vbufferOffset + ssize,
                                   &bitangent[0], ssize);

                            vbufferOffset += vertexSize;
                            idx++;
                          }

                          // also update layout
                          prims.layout.add(bgfx::Attrib::Tangent, 3,
                                           bgfx::AttribType::Float);
                          prims.layout.add(bgfx::Attrib::Bitangent, 3,
                                           bgfx::AttribType::Float);
                        } else {
                          // w is handedness
                          const auto size = sizeof(float) * 4;
                          const auto skips = size + view.byteStride;
                          auto vbufferOffset = offsetSize;
                          offsetSize += size;

                          if (accessor.componentType !=
                                  TINYGLTF_COMPONENT_TYPE_FLOAT ||
                              accessor.type != TINYGLTF_TYPE_VEC4) {
                            throw ActivationError(
                                "Tangent vector data was not a "
                                "float32 vector of 4");
                          }

                          for (auto it = dataBeg; it != dataEnd; it += skips) {
                            const float *chunk = (float *)&(*it);
                            memcpy(vbuffer->data + vbufferOffset, chunk, size);
                            vbufferOffset += vertexSize;
                          }

                          // also update layout
                          prims.layout.add(attrib, 4, bgfx::AttribType::Float);
                        }
                      } break;
                      case bgfx::Attrib::Color0:
                      case bgfx::Attrib::Color1:
                      case bgfx::Attrib::Color2:
                      case bgfx::Attrib::Color3: {
                        const auto elemSize = [&]() {
                          if (accessor.componentType ==
                              TINYGLTF_COMPONENT_TYPE_FLOAT)
                            return 4;
                          else if (accessor.componentType ==
                                   TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                            return 2;
                          else // BYTE
                            return 1;
                        }();
                        const auto elemNum = [&]() {
                          if (accessor.type == TINYGLTF_TYPE_VEC3)
                            return 3;
                          else // VEC4
                            return 4;
                        }();
                        const auto gsize = elemSize * elemNum;
                        const auto gskips = gsize + view.byteStride;
                        const auto osize = 4;
                        auto vbufferOffset = offsetSize;
                        offsetSize += osize;

                        for (auto it = dataBeg; it != dataEnd; it += gskips) {
                          switch (elemNum) {
                          case 3: {
                            switch (elemSize) {
                            case 4: { // float
                              const float *chunk = (float *)&(*it);
                              uint8_t *buf = vbuffer->data + vbufferOffset;
                              buf[0] = uint8_t(chunk[0] * 255);
                              buf[1] = uint8_t(chunk[1] * 255);
                              buf[2] = uint8_t(chunk[2] * 255);
                              buf[3] = 255;
                            } break;
                            case 2: {
                              const uint16_t *chunk = (uint16_t *)&(*it);
                              uint8_t *buf = vbuffer->data + vbufferOffset;
                              buf[0] = uint8_t(
                                  (float(chunk[0]) / float(UINT16_MAX)) * 255);
                              buf[1] = uint8_t(
                                  (float(chunk[1]) / float(UINT16_MAX)) * 255);
                              buf[2] = uint8_t(
                                  (float(chunk[2]) / float(UINT16_MAX)) * 255);
                              buf[3] = 255;
                            } break;
                            case 1: {
                              const uint8_t *chunk = (uint8_t *)&(*it);
                              uint8_t *buf = vbuffer->data + vbufferOffset;
                              memcpy(buf, chunk, 3);
                              buf[3] = 255;
                            } break;
                            default:
                              assert(false);
                              break;
                            }
                          } break;
                          case 4: {
                            switch (elemSize) {
                            case 4: { // float
                              const float *chunk = (float *)&(*it);
                              uint8_t *buf = vbuffer->data + vbufferOffset;
                              buf[0] = uint8_t(chunk[0] * 255);
                              buf[1] = uint8_t(chunk[1] * 255);
                              buf[2] = uint8_t(chunk[2] * 255);
                              buf[3] = uint8_t(chunk[3] * 255);
                            } break;
                            case 2: {
                              const uint16_t *chunk = (uint16_t *)&(*it);
                              uint8_t *buf = vbuffer->data + vbufferOffset;
                              buf[0] = uint8_t(
                                  (float(chunk[0]) / float(UINT16_MAX)) * 255);
                              buf[1] = uint8_t(
                                  (float(chunk[1]) / float(UINT16_MAX)) * 255);
                              buf[2] = uint8_t(
                                  (float(chunk[2]) / float(UINT16_MAX)) * 255);
                              buf[3] = uint8_t(
                                  (float(chunk[3]) / float(UINT16_MAX)) * 255);
                            } break;
                            case 1: {
                              const uint8_t *chunk = (uint8_t *)&(*it);
                              uint8_t *buf = vbuffer->data + vbufferOffset;
                              memcpy(buf, chunk, 4);
                            } break;
                            default:
                              assert(false);
                              break;
                            }
                          } break;
                          default:
                            assert(false);
                            break;
                          }
                          vbufferOffset += vertexSize;
                        }

                        prims.layout.add(attrib, 4, bgfx::AttribType::Uint8,
                                         true);
                      } break;
                      case bgfx::Attrib::TexCoord0:
                      case bgfx::Attrib::TexCoord1:
                      case bgfx::Attrib::TexCoord2:
                      case bgfx::Attrib::TexCoord3:
                      case bgfx::Attrib::TexCoord4:
                      case bgfx::Attrib::TexCoord5:
                      case bgfx::Attrib::TexCoord6:
                      case bgfx::Attrib::TexCoord7: {
                        const auto elemSize = [&]() {
                          if (accessor.componentType ==
                              TINYGLTF_COMPONENT_TYPE_FLOAT)
                            return 4;
                          else if (accessor.componentType ==
                                   TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                            return 2;
                          else // BYTE
                            return 1;
                        }();
                        const auto elemNum = 2;
                        const auto gsize = elemSize * elemNum;
                        const auto gskips = gsize + view.byteStride;
                        const auto osize = sizeof(float) * 2;
                        auto vbufferOffset = offsetSize;
                        offsetSize += osize;

                        for (auto it = dataBeg; it != dataEnd; it += gskips) {
                          switch (elemSize) {
                          case 4: { // float
                            const float *chunk = (float *)&(*it);
                            float *buf =
                                (float *)(vbuffer->data + vbufferOffset);
                            buf[0] = chunk[0];
                            buf[1] = chunk[1];
                          } break;
                          case 2: {
                            const uint16_t *chunk = (uint16_t *)&(*it);
                            float *buf =
                                (float *)(vbuffer->data + vbufferOffset);
                            buf[0] = float(chunk[0]) / float(UINT16_MAX);
                            buf[1] = float(chunk[1]) / float(UINT16_MAX);
                          } break;
                          case 1: {
                            const uint8_t *chunk = (uint8_t *)&(*it);
                            float *buf =
                                (float *)(vbuffer->data + vbufferOffset);
                            buf[0] = float(chunk[0]) / float(255);
                            buf[1] = float(chunk[1]) / float(255);
                          } break;
                          default:
                            assert(false);
                            break;
                          }
                          vbufferOffset += vertexSize;
                        }

                        prims.layout.add(attrib, 2, bgfx::AttribType::Float);
                      } break;
                      default:
                        // throw std::runtime_error("Invalid attribute.");
                        break;
                      }
                      accessorIdx++;
                    }
                    // wrap up layout
                    prims.layout.end();
                    prims.vb = bgfx::createVertexBuffer(vbuffer, prims.layout);

                    // check if we have indices
                    if (glprims.indices != -1) {
                      // alright we also use the IB
                      const auto &indices = gltf.accessors[glprims.indices];
                      const auto count = indices.count;
                      int esize;
                      if (count < UINT16_MAX) {
                        esize = 2;
                      } else {
                        esize = 4;
                      }
                      auto ibuffer = bgfx::alloc(esize * count);
                      auto offset = 0;
                      int gsize;
                      // https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#indices
                      if (TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        gsize = 4;
                      } else if (TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        gsize = 2;
                      } else {
                        gsize = 1;
                      }
                      const auto &view = gltf.bufferViews[indices.bufferView];
                      const auto &buffer = gltf.buffers[view.buffer];
                      const auto dataBeg =
                          buffer.data.begin() + view.byteOffset;
                      const auto dataEnd = dataBeg + view.byteLength;

#define FILL_INDEX                                                             \
  if (esize == 4) {                                                            \
    uint32_t *ibuf = (uint32_t *)(ibuffer->data + offset);                     \
    *ibuf = uint32_t(*chunk);                                                  \
  } else {                                                                     \
    uint16_t *ibuf = (uint16_t *)(ibuffer->data + offset);                     \
    *ibuf = uint16_t(*chunk);                                                  \
  }
                      for (auto it = dataBeg; it != dataEnd; it += gsize) {
                        switch (gsize) {
                        case 4: {
                          const uint32_t *chunk = (uint32_t *)&(*it);
                          FILL_INDEX;
                        } break;
                        case 2: {
                          const uint16_t *chunk = (uint16_t *)&(*it);
                          FILL_INDEX;
                        } break;
                        case 1: {
                          const uint8_t *chunk = (uint8_t *)&(*it);
                          FILL_INDEX;
                        } break;
                        default:
                          assert(false);
                          break;
                        }
                        offset += esize;
                      }
#undef FILL_INDEX
                      prims.ib = bgfx::createIndexBuffer(
                          ibuffer, esize == 4 ? BGFX_BUFFER_INDEX32 : 0);
                    } else {
                      prims.stateFlags = BGFX_STATE_PT_TRISTRIP;
                    }
                  }

                  mesh.primitives.emplace_back(
                      _model->gfxPrimitives.emplace_back(std::move(prims)));
                }
                node.mesh = _model->gfxMeshes.emplace_back(std::move(mesh));
              }

              // if (glnode.camera != -1) {
              //   // TODO
              // }

              for (const auto childIndex : glnode.children) {
                const auto &subglnode = gltf.nodes[childIndex];
                node.children.emplace_back(processNode(subglnode));
              }

              return NodeRef(_model->nodes.emplace_back(std::move(node)));
            };
        _model->rootNode = processNode(glnode);
      }

      return ModelVar.Get(_model);
    });
  }
};

struct Draw : public BGFX::BaseConsumer {
  ParamVar _model{};
  CBVar *_bgfxContext{nullptr};
  std::array<CBExposedTypeInfo, 1> _required;

  static CBTypesInfo inputTypes() { return CoreInfo::Float4x4Types; }
  static CBTypesInfo outputTypes() { return CoreInfo::Float4x4Types; }

  static inline Parameters Params{{"Model",
                                   CBCCSTR("The GLTF model to render."),
                                   {Load::ModelType, Load::ModelVarType}}};
  static CBParametersInfo parameters() { return Params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _model = value;
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _model;
    default:
      throw InvalidParameterIndex();
    }
  }

  CBExposedTypesInfo requiredVariables() {
    int idx = -1;
    if (_model.isVariable()) {
      idx++;
      _required[idx].name = _model.variableName();
      _required[idx].help = CBCCSTR("The required model.");
      _required[idx].exposedType = Load::ModelType;
    }
    if (idx == -1) {
      return {};
    } else {
      return {_required.data(), uint32_t(idx + 1), 0};
    }
  }

  CBTypeInfo compose(const CBInstanceData &data) {
    BGFX::BaseConsumer::compose(data);

    if (data.inputType.seqTypes.elements[0].basicType == CBType::Seq) {
      // TODO
      OVERRIDE_ACTIVATE(data, activate);
    } else {
      OVERRIDE_ACTIVATE(data, activateSingle);
    }
    return data.inputType;
  }

  void warmup(CBContext *context) {
    _model.warmup(context);
    _bgfxContext = referenceVariable(context, "GFX.Context");
  }

  void cleanup() {
    _model.cleanup();
    if (_bgfxContext) {
      releaseVariable(_bgfxContext);
      _bgfxContext = nullptr;
    }
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    throw ActivationError("Not yet implemented.");
    return input;
  }

  void renderNode(BGFX::Context *ctx, const Node &node) {
    if (node.mesh) {
      for (const auto &primsRef : node.mesh->get().primitives) {
        const auto &prims = primsRef.get();
        if (prims.vb.idx != bgfx::kInvalidHandle) {
          const auto currentView = ctx->currentView();

          uint64_t state = prims.stateFlags | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_LESS;
          if constexpr (BGFX::CurrentRenderer == BGFX::Renderer::OpenGL) {
            // workaround for flipped Y render to textures
            if (currentView.id > 0) {
              state |= BGFX_STATE_CULL_CCW;
            } else {
              state |= BGFX_STATE_CULL_CW;
            }
          } else {
            state |= BGFX_STATE_CULL_CW;
          }
          bgfx::setState(state);

          bgfx::setVertexBuffer(0, prims.vb);
          if (prims.ib.idx != bgfx::kInvalidHandle) {
            bgfx::setIndexBuffer(prims.ib);
          }

          // // Submit primitive for rendering to the current view.
          bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;
          bgfx::submit(currentView.id, handle);
        }
      }
    }

    for (const auto &snode : node.children) {
      renderNode(ctx, snode);
    }
  }

  CBVar activateSingle(CBContext *context, const CBVar &input) {
    auto *ctx =
        reinterpret_cast<BGFX::Context *>(_bgfxContext->payload.objectValue);
    const auto &modelVar = _model.get();
    const auto model = reinterpret_cast<Model *>(modelVar.payload.objectValue);
    float mat[16];
    memcpy(&mat[0], &input.payload.seqValue.elements[0].payload.float4Value,
           sizeof(float) * 4);
    memcpy(&mat[4], &input.payload.seqValue.elements[1].payload.float4Value,
           sizeof(float) * 4);
    memcpy(&mat[8], &input.payload.seqValue.elements[2].payload.float4Value,
           sizeof(float) * 4);
    memcpy(&mat[12], &input.payload.seqValue.elements[3].payload.float4Value,
           sizeof(float) * 4);
    bgfx::setTransform(mat);
    if (model->rootNode) {
      renderNode(ctx, *model->rootNode);
    }
    return input;
  }
};

void registerBlocks() {
  REGISTER_CBLOCK("GLTF.Load", Load);
  REGISTER_CBLOCK("GLTF.Draw", Draw);
}
} // namespace gltf
} // namespace chainblocks

#ifdef CB_INTERNAL_TESTS
#include "gltf_tests.cpp"
#endif