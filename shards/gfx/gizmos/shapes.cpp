#include "shapes.hpp"
#include "linalg.h"
#include <gfx/geom.hpp>
#include <gfx/mesh_utils.hpp>
#include <gfx/drawables/mesh_drawable.hpp>
#include <gfx/view.hpp>

namespace gfx {

const std::vector<MeshVertexAttribute> &ShapeRenderer::LineVertex::getAttributes() {
  static std::vector<MeshVertexAttribute> attribs = []() {
    std::vector<MeshVertexAttribute> attribs;
    attribs.emplace_back("position", 3, StorageType::Float32);
    attribs.emplace_back("color", 4, StorageType::Float32);
    attribs.emplace_back("direction", 3, StorageType::Float32);
    attribs.emplace_back("offsetSS", 2, StorageType::Float32);
    return attribs;
  }();
  return attribs;
}

const std::vector<MeshVertexAttribute> &ShapeRenderer::SolidVertex::getAttributes() {
  static std::vector<MeshVertexAttribute> attribs = []() {
    std::vector<MeshVertexAttribute> attribs;
    attribs.emplace_back("position", 3, StorageType::Float32);
    attribs.emplace_back("color", 4, StorageType::Float32);
    return attribs;
  }();
  return attribs;
}

// 4 vertices are used to draw a line segment
// 2 placed at the start point and 2 at the end point
// The position & offsetSS attribute is set like this along the line quad:
//  [a, (-1, 1)]-------------------[b, (1, 1)]    y
//  |                                        |    |
//  [a, (-1,-1)]-------------------[b, (1,-1)]    .__ x
// The vertices are then extruded by offsetSS in screen space
FeaturePtr ScreenSpaceSizeFeature::create() {
  FeaturePtr result = std::make_shared<Feature>();
  result->state.set_culling(false);

  shader::EntryPoint &entry =
      result->shaderEntryPoints.emplace_back("screenSpaceLineGeometry", ProgrammableGraphicsStage::Vertex, shader::BlockPtr());

  using namespace gfx::shader::blocks;
  using namespace gfx::shader;
  std::unique_ptr<Compound> code = makeCompoundBlock();
  code->appendLine("var offsetSS =", ReadInput("offsetSS"));
  code->appendLine("var dir = ", ReadInput("direction"));
  code->appendLine("var color = ", ReadInput("color"));
  code->appendLine("var posWS = ", ReadInput("position"));
  code->appendLine("var world = ", ReadBuffer("world", FieldTypes::Float4x4));
  code->appendLine("var view = ", ReadBuffer("view", FieldTypes::Float4x4, "view"));
  code->appendLine("var invView = ", ReadBuffer("invView", FieldTypes::Float4x4, "view"));
  code->appendLine("var proj = ", ReadBuffer("proj", FieldTypes::Float4x4, "view"));
  code->appendLine("var viewport = ", ReadBuffer("viewport", FieldTypes::Float4, "view"));
  code->appendLine("var cameraPosition = invView[3].xyz");
  code->appendLine("var nextWS = posWS+", ReadInput("direction"));
  code->appendLine("var nextProj = proj* view * world * vec4<f32>(nextWS, 1.0)");
  code->appendLine("var nextNDC = nextProj.xyz / nextProj.w");
  code->appendLine("var posProj = proj* view * world * vec4<f32>(posWS, 1.0)");
  code->appendLine("var posNDC = posProj.xyz / posProj.w");
  code->appendLine("var directionSS = normalize(nextNDC.xy - posNDC.xy)"); // Direction of the line in screen space
  code->appendLine("var tangentSS = vec2<f32>(-directionSS.y, directionSS.x)");
  code->appendLine("posProj.x = (posNDC.x + tangentSS.x * offsetSS.y * (1.0/viewport.z) + directionSS.x * offsetSS.x * "
                   "(1.0/viewport.z)) * posProj.w");
  code->appendLine("posProj.y = (posNDC.y + tangentSS.y * offsetSS.y * (1.0/viewport.w) + directionSS.y * offsetSS.x *"
                   "(1.0/viewport.w)) * posProj.w");
  code->append(WriteOutput("position", FieldTypes::Float4, "posProj"));
  entry.code = std::move(code);

  // Apply after & overwrite any base transform
  entry.dependencies.emplace_back("writePosition");

  return result;
}

FeaturePtr NoCullingFeature::create() {
  FeaturePtr result = std::make_shared<Feature>();
  result->state.set_culling(false);

  return result;
}

FeaturePtr GizmoLightingFeature::create() {
  FeaturePtr result = std::make_shared<Feature>();
  result->state.set_culling(false);

  float3 baseLightDir = linalg::normalize(float3(-.5f, -0.5f, -0.2f));

  shader::EntryPoint &entry =
      result->shaderEntryPoints.emplace_back("gizmoLighting", ProgrammableGraphicsStage::Fragment, shader::BlockPtr());

  using namespace gfx::shader::blocks;
  using namespace gfx::shader;
  std::unique_ptr<Compound> code = makeCompoundBlock();
  code->appendLine(fmt::format("var lightDirVS = vec3<f32>({:f}, {:f}, {:f})", baseLightDir.x, baseLightDir.y, baseLightDir.z));
  code->appendLine("var invTransposeView = transpose(", ReadBuffer("invView", FieldTypes::Float4x4, "view"), ")");
  code->appendLine("var normalVS = (invTransposeView * vec4<f32>(", ReadInput("worldNormal"), ", 0.0)).xyz");
  code->appendLine("var normalWS = ", ReadInput("worldNormal"));
  code->appendLine("var nDotL = dot(normalVS, -lightDirVS)");
  code->appendLine("var baseColor = ", ReadGlobal("color"));
  code->appendLine("var lighting = baseColor * mix(0.5, 1.0, max(0.0, nDotL))");
  code->append(WriteGlobal("color", FieldTypes::Float4, "vec4<f32>(lighting.xyz, baseColor.a)"));
  entry.code = std::move(code);

  // Apply after normal/baseColor
  entry.dependencies.emplace_back("textureColor");
  entry.dependencies.emplace_back("writeColor", DependencyType::Before);

  return result;
}

#define UNPACK3(_x) \
  { _x.x, _x.y, _x.z }
#define UNPACK4(_x) \
  { _x.x, _x.y, _x.z, _x.w }

void ShapeRenderer::addLine(float3 a, float3 b, float3 dirA, float3 dirB, float4 color, uint32_t thickness) {
  float xOffset = 0.5f * thickness;
  float yOffset = 1.0f * thickness;
  lineVertices.push_back(LineVertex{
      .position = UNPACK3(a),
      .color = UNPACK4(color),
      .direction = UNPACK3(dirA),
      .offsetSS = {-xOffset, yOffset},
  });
  lineVertices.push_back(LineVertex{
      .position = UNPACK3(b),
      .color = UNPACK4(color),
      .direction = UNPACK3(dirB),
      .offsetSS = {xOffset, yOffset},
  });
  lineVertices.push_back(LineVertex{
      .position = UNPACK3(b),
      .color = UNPACK4(color),
      .direction = UNPACK3(dirB),
      .offsetSS = {xOffset, -yOffset},
  });

  lineVertices.push_back(LineVertex{
      .position = UNPACK3(a),
      .color = UNPACK4(color),
      .direction = UNPACK3(dirA),
      .offsetSS = {-xOffset, -yOffset},
  });
  lineVertices.push_back(LineVertex{
      .position = UNPACK3(a),
      .color = UNPACK4(color),
      .direction = UNPACK3(dirA),
      .offsetSS = {-xOffset, yOffset},
  });
  lineVertices.push_back(LineVertex{
      .position = UNPACK3(b),
      .color = UNPACK4(color),
      .direction = UNPACK3(dirB),
      .offsetSS = {xOffset, -yOffset},
  });
}

void ShapeRenderer::addLine(float3 a, float3 b, float4 color, uint32_t thickness) {
  float3 direction = linalg::normalize(b - a);
  addLine(a, b, direction, direction, color, thickness);
}

void ShapeRenderer::addCircle(float3 center, float3 xBase, float3 yBase, float radius, float4 color, uint32_t thickness,
                              uint32_t resolution) {
  float3 prevPos;
  float3 prevDelta;
  for (size_t i = 0; i < resolution; i++) {
    float t = i / float(resolution - 1) * pi2;
    float tCos = std::cos(t);
    float tSin = std::sin(t);
    float3 pos = center + tCos * xBase * radius + tSin * yBase * radius;
    float3 delta = center + -tSin * xBase + tCos * yBase;
    if (i > 0) {
      addLine(prevPos, pos, prevDelta, delta, color, thickness);
    }
    prevPos = pos;
    prevDelta = delta;
  }
}

void ShapeRenderer::addRect(float3 center, float3 xBase, float3 yBase, float2 size, float4 color, uint32_t thickness) {
  float2 halfSize = size / 2.0f;
  float3 verts[] = {
      center - halfSize.x * xBase - halfSize.y * yBase,
      center + halfSize.x * xBase - halfSize.y * yBase,
      center + halfSize.x * xBase + halfSize.y * yBase,
      center - halfSize.x * xBase + halfSize.y * yBase,
  };

  for (size_t i = 0; i < 4; i++) {
    float3 a = verts[i];
    float3 b = verts[(i + 1) % 4];
    addLine(a, b, color, thickness);
  }
}

void ShapeRenderer::addBox(float3 center, float3 xBase, float3 yBase, float3 zBase, float3 size, float4 color,
                           uint32_t thickness) {
  float3 halfSize = size / 2.0f;
  float3 verts[] = {
      center - halfSize.x * xBase - halfSize.y * yBase - halfSize.z * zBase,
      center + halfSize.x * xBase - halfSize.y * yBase - halfSize.z * zBase,
      center + halfSize.x * xBase + halfSize.y * yBase - halfSize.z * zBase,
      center - halfSize.x * xBase + halfSize.y * yBase - halfSize.z * zBase,

      center - halfSize.x * xBase - halfSize.y * yBase + halfSize.z * zBase,
      center + halfSize.x * xBase - halfSize.y * yBase + halfSize.z * zBase,
      center + halfSize.x * xBase + halfSize.y * yBase + halfSize.z * zBase,
      center - halfSize.x * xBase + halfSize.y * yBase + halfSize.z * zBase,
  };

  auto drawFace4 = [&](size_t start) {
    for (size_t i = 0; i < 4; i++) {
      float3 a = verts[start + i];
      float3 b = verts[start + (i + 1) % 4];
      addLine(a, b, color, thickness);
    }
  };
  drawFace4(0);
  drawFace4(4);
  for (size_t i = 0; i < 4; i++) {
    float3 a = verts[i];
    float3 b = verts[i + 4];
    addLine(a, b, color, thickness);
  }
}

void ShapeRenderer::addBox(float4x4 transform, float3 center, float3 size, float4 color, uint32_t thickness) {
  float4 x(1, 0, 0, 0);
  float4 y(0, 1, 0, 0);
  float4 z(0, 0, 1, 0);
  x = linalg::mul(transform, x);
  y = linalg::mul(transform, y);
  z = linalg::mul(transform, z);
  center = linalg::mul(transform, float4(center, 1.0f)).xyz();
  addBox(center, x.xyz(), y.xyz(), z.xyz(), size, color, thickness);
}

void ShapeRenderer::addPoint(float3 center, float4 color, uint32_t thickness) {
  float3 dir = float3(1, 0, 0);
  float2 prevPos;
  uint32_t resolution = 6 + std::max<int32_t>(0, int32_t(thickness) - 4);
  for (uint32_t i = 0; i < resolution; i++) {
    float t = i / float(resolution - 1) * pi2;
    float2 pos(std::cos(t), std::sin(t));
    if (i > 0) {
      lineVertices.push_back(LineVertex{
          .position = UNPACK3(center),
          .color = UNPACK4(color),
          .direction = UNPACK3(dir),
          .offsetSS = {0, 0},
      });
      lineVertices.push_back(LineVertex{
          .position = UNPACK3(center),
          .color = UNPACK4(color),
          .direction = UNPACK3(dir),
          .offsetSS = {pos.x * thickness, pos.y * thickness},
      });
      lineVertices.push_back(LineVertex{
          .position = UNPACK3(center),
          .color = UNPACK4(color),
          .direction = UNPACK3(dir),
          .offsetSS = {prevPos.x * thickness, prevPos.y * thickness},
      });
    }
    prevPos = pos;
  }
}

void ShapeRenderer::addSolidRect(float3 center, float3 xBase, float3 yBase, float2 size, float4 color, bool culling) {
  float2 halfSize = size / 2.0f;
  float3 verts[] = {
      center - halfSize.x * xBase - halfSize.y * yBase,
      center + halfSize.x * xBase - halfSize.y * yBase,
      center + halfSize.x * xBase + halfSize.y * yBase,
      center - halfSize.x * xBase + halfSize.y * yBase,
  };

  addSolidQuad(verts[0], verts[1], verts[2], verts[3], color, culling);
}

void ShapeRenderer::addSolidQuad(float3 a, float3 b, float3 c, float3 d, float4 color, bool culling) {
  // Render to different vector buffer based on whether culling is enabled
  std::vector<SolidVertex> &solidVertexVec = culling ? solidVertices : unculledSolidVertices;
  solidVertexVec.push_back(SolidVertex{.position = UNPACK3(a), .color = UNPACK4(color)});
  solidVertexVec.push_back(SolidVertex{.position = UNPACK3(b), .color = UNPACK4(color)});
  solidVertexVec.push_back(SolidVertex{.position = UNPACK3(c), .color = UNPACK4(color)});
  solidVertexVec.push_back(SolidVertex{.position = UNPACK3(d), .color = UNPACK4(color)});
  solidVertexVec.push_back(SolidVertex{.position = UNPACK3(a), .color = UNPACK4(color)});
  solidVertexVec.push_back(SolidVertex{.position = UNPACK3(c), .color = UNPACK4(color)});
}

void ShapeRenderer::addDisc(float3 center, float3 xBase, float3 yBase, float outerRadius, float innerRadius, float4 color,
                            bool culling, uint32_t resolution) {

  float3 prevPos;
  float3 innerPrevPos;
  for (size_t i = 0; i < resolution; i++) {
    float t = i / float(resolution - 1) * pi2;
    float tCos = std::cos(t);
    float tSin = std::sin(t);
    float3 pos = center + tCos * xBase * outerRadius + tSin * yBase * outerRadius;
    float3 innerPos = center + tCos * xBase * innerRadius + tSin * yBase * innerRadius;
    if (i > 0) {
      addSolidQuad(prevPos, pos, innerPos, innerPrevPos, color, culling);
    }
    prevPos = pos;
    innerPrevPos = innerPos;
  }
}

void ShapeRenderer::begin() {
  lineVertices.clear();
  solidVertices.clear();
  unculledSolidVertices.clear();
}

void ShapeRenderer::end(DrawQueuePtr queue) {
  if (lineVertices.size() > 0) {
    if (!lineMesh)
      lineMesh = std::make_shared<Mesh>();

    MeshFormat fmt = {
        .primitiveType = PrimitiveType::TriangleList,
        .windingOrder = WindingOrder::CCW,
        .vertexAttributes = LineVertex::getAttributes(),
    };
    lineMesh->update(fmt, lineVertices.data(), lineVertices.size() * sizeof(LineVertex), nullptr, 0);

    auto drawable = std::make_shared<MeshDrawable>(lineMesh);
    drawable->features.push_back(screenSpaceSizeFeature);
    queue->add(drawable);
  }

  if (solidVertices.size() > 0) {
    if (!solidMesh)
      solidMesh = std::make_shared<Mesh>();

    MeshFormat fmt = {
        .primitiveType = PrimitiveType::TriangleList,
        .windingOrder = WindingOrder::CCW,
        .vertexAttributes = SolidVertex::getAttributes(),
    };
    solidMesh->update(fmt, solidVertices.data(), solidVertices.size() * sizeof(SolidVertex), nullptr, 0);

    auto drawable = std::make_shared<MeshDrawable>(solidMesh);
    queue->add(drawable);
  }

  if (unculledSolidVertices.size() > 0) {
    if (!unculledSolidMesh)
      unculledSolidMesh = std::make_shared<Mesh>();

    MeshFormat fmt = {
        .primitiveType = PrimitiveType::TriangleList,
        .windingOrder = WindingOrder::CCW,
        .vertexAttributes = SolidVertex::getAttributes(),
    };
    unculledSolidMesh->update(fmt, unculledSolidVertices.data(), unculledSolidVertices.size() * sizeof(SolidVertex), nullptr, 0);

    auto drawable = std::make_shared<MeshDrawable>(unculledSolidMesh);
    drawable->features.push_back(noCullingFeature);
    queue->add(drawable);
  }
}

GizmoRenderer::GizmoRenderer() { loadGeometry(); }

float GizmoRenderer::getSize(float3 position) const {
  float4 projected = linalg::mul(view->view, float4(position, 1.0f));
  projected /= projected.w;
  float4x4 projMatrix = view->getProjectionMatrix(viewportSize);

  float minPerspective = std::min(projMatrix[0][0], projMatrix[1][1]);

  float distanceFromCamera = std::abs(projected.z);
  float scalingFactor = distanceFromCamera / minPerspective;
  return scalingFactor;
}

void GizmoRenderer::addHandle(float3 origin, float3 direction, float radius, float length, float4 bodyColor, CapType capType,
                              float4 capColor) {
  float capRatio = 1.0f;
  MeshPtr capMesh;
  float4x4 capPreTransform = linalg::identity;
  bool extendBodyToCapCenter = false;
  switch (capType) {
  case CapType::Arrow:
    capRatio = 2.2f;
    capPreTransform = cylinderAdjustment;
    capMesh = arrowMesh;
    break;
  case CapType::Cube:
    capMesh = cubeMesh;
    break;
  case CapType::Sphere:
    capMesh = sphereMesh;
    extendBodyToCapCenter = true;
    break;
  }
  assert(capMesh);
  auto geom = generateHandleGeometry(origin, direction, radius, length, 0.35f, capRatio, extendBodyToCapCenter);

  MeshDrawable::Ptr body = std::make_shared<MeshDrawable>(handleBodyMesh);
  body->transform = linalg::mul(geom.bodyTransform, cylinderAdjustment);
  body->parameters.set("baseColor", bodyColor);
  body->features.push_back(gizmoLightingFeature);
  drawables.push_back(body);

  MeshDrawable::Ptr cap = std::make_shared<MeshDrawable>(capMesh);
  cap->transform = linalg::mul(geom.capTransform, capPreTransform);
  cap->parameters.set("baseColor", capColor);
  cap->features.push_back(gizmoLightingFeature);
  drawables.push_back(cap);
}

void GizmoRenderer::addCubeHandle(float3 center, float size, float4 color) {
  MeshDrawable::Ptr drawable = std::make_shared<MeshDrawable>(cubeMesh);
  drawable->transform = linalg::mul(linalg::translation_matrix(center), linalg::scaling_matrix(float3(size)));
  drawable->parameters.set("baseColor", color);
  drawable->features.push_back(gizmoLightingFeature);
  drawables.push_back(drawable);
}

void GizmoRenderer::begin(ViewPtr view, float2 viewportSize) {
  this->view = view;
  this->viewportSize = viewportSize;
  shapeRenderer.begin();
}

void GizmoRenderer::end(DrawQueuePtr queue) {
  for (auto drawable : drawables) {
    queue->add(drawable);
  }
  drawables.clear();
  view.reset();
  shapeRenderer.end(queue);
}

GizmoRenderer::HandleGeometry GizmoRenderer::generateHandleGeometry(float3 origin, float3 direction, float radius, float length,
                                                                    float bodyRatio, float capRatio, bool extendBodyToCapCenter) {
  HandleGeometry result;

  float capLength = std::min(length, radius * capRatio);
  float bodyLength = length - capLength;
  float bodyRadius = radius * bodyRatio;

  float4x4 rotation = rotationFromXDirection(direction);

  result.capTransform = linalg::identity;
  result.capTransform = linalg::scaling_matrix(float3(capLength, radius, radius));
  result.capTransform = linalg::mul(rotation, result.capTransform);
  result.capTransform = linalg::mul(linalg::translation_matrix(origin + direction * bodyLength), result.capTransform);

  if (extendBodyToCapCenter) {
    bodyLength += capLength * 0.5f;
  }
  result.bodyTransform = linalg::identity;
  result.bodyTransform = linalg::scaling_matrix(float3(bodyLength, bodyRadius, bodyRadius));
  result.bodyTransform = linalg::mul(rotation, result.bodyTransform);
  result.bodyTransform = linalg::mul(linalg::translation_matrix(origin), result.bodyTransform);

  return result;
}

void GizmoRenderer::loadGeometry() {
  {
    geom::CylinderGenerator gen;
    gen.height = 1.0f;
    gen.radiusBottom = gen.radiusTop = 1.0f;
    gen.radialSegments = 10;
    gen.generate();
    handleBodyMesh = createMesh(gen.vertices, gen.indices);
  }
  {
    geom::CylinderGenerator gen;
    gen.height = 1.0f;
    gen.radiusTop = 0.0f;
    gen.radiusBottom = 1.0f;
    gen.radialSegments = 12;
    gen.generate();
    arrowMesh = createMesh(gen.vertices, gen.indices);
  }
  {
    geom::CubeGenerator gen;
    gen.generate();
    cubeMesh = createMesh(gen.vertices, gen.indices);
  }
  {
    geom::SphereGenerator gen;
    gen.generate();
    sphereMesh = createMesh(gen.vertices, gen.indices);
  }

  float4x4 cylinderRotOffset = linalg::rotation_matrix(linalg::rotation_quat(float3(0, 0, 1), -pi * 0.5f));
  float4x4 cylinderTranslationOffset = linalg::translation_matrix(float3(0, 0.5f, 0.0f));
  cylinderAdjustment = linalg::mul(cylinderRotOffset, cylinderTranslationOffset);
}

} // namespace gfx
