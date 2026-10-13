// SPDX-License-Identifier: Apache-2.0

#include "SceneWriter.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace miniscene {

using namespace anari::math;

namespace {

constexpr float PI = 3.14159265358979323846f;

mini::vec3f toMini(const float3 &v)
{
  return mini::vec3f(v.x, v.y, v.z);
}

float3 xyz(const float4 &v)
{
  return float3(v.x, v.y, v.z);
}

float3 xfmPoint(const mat4 &m, const float3 &p)
{
  return xyz(mul(m, float4(p.x, p.y, p.z, 1.f)));
}

float3 xfmVector(const mat4 &m, const float3 &v)
{
  return xyz(mul(m, float4(v.x, v.y, v.z, 0.f)));
}

mini::affine3f toAffine(const mat4 &m)
{
  mini::affine3f retval;
  retval.l.vx = toMini(xyz(m[0]));
  retval.l.vy = toMini(xyz(m[1]));
  retval.l.vz = toMini(xyz(m[2]));
  retval.p = toMini(xyz(m[3]));
  return retval;
}

// Any vector perpendicular to the unit vector `n`.
float3 perpendicular(const float3 &n)
{
  const float3 axis =
      std::abs(n.x) < 0.9f ? float3(1.f, 0.f, 0.f) : float3(0.f, 1.f, 0.f);
  return normalize(cross(axis, n));
}

// A parameter given as a color or as a single value, as four components.
bool constantOf(const helium::AnariAny &value, float4 *constant)
{
  switch (value.type()) {
  case ANARI_FLOAT32: {
    const float v = value.get<float>();
    *constant = float4(v, v, v, 1.f);
    return true;
  }
  case ANARI_FLOAT32_VEC2: {
    const auto v = value.get<float2>();
    *constant = float4(v.x, v.y, 0.f, 1.f);
    return true;
  }
  case ANARI_FLOAT32_VEC3: {
    const auto v = value.get<float3>();
    *constant = float4(v.x, v.y, v.z, 1.f);
    return true;
  }
  case ANARI_FLOAT32_VEC4:
    *constant = value.get<float4>();
    return true;
  default:
    return false;
  }
}

// The radiant intensity of a light on its axis, from whichever of the ANARI
// parameters is set: intensity before power before radiance.
float intensityOf(const Object &light, float solidAngle, float emittingArea)
{
  if (light.hasParam("intensity"))
    return light.getParam<float>("intensity", 1.f);
  if (light.hasParam("power") && solidAngle > 0.f)
    return light.getParam<float>("power", 1.f) / solidAngle;
  if (light.hasParam("radiance") && emittingArea > 0.f)
    return light.getParam<float>("radiance", 1.f) * emittingArea;
  return 1.f;
}

} // namespace

// UvSource ///////////////////////////////////////////////////////////////////

bool SceneWriter::UvSource::valid() const
{
  return !attribute.empty();
}

bool SceneWriter::UvSource::operator==(const UvSource &other) const
{
  return attribute == other.attribute && transform == other.transform
      && offset == other.offset;
}

// SceneWriter ////////////////////////////////////////////////////////////////

SceneWriter::SceneWriter(
    const GlobalState &state, const helium::BaseObject &reporter)
    : m_state(state), m_reporter(reporter)
{}

void SceneWriter::write(
    Object &world, Object *camera, Object *renderer, uint2 frameSize)
{
  m_scene = mini::Scene::create();

  // Surfaces and lights of the world itself are an object placed once.
  addInstance(world, mat4(identity));

  if (auto *instances = world.objectArrayParam("instance")) {
    auto **handles = instances->handlesBegin();
    for (size_t i = 0; i < instances->totalSize(); i++) {
      auto *instance = static_cast<Object *>(handles[i]);
      if (!instance)
        continue;
      auto *group = instance->objectParam("group", ANARI_GROUP);
      if (!group)
        continue;
      addInstance(*group, instance->getParam<mat4>("transform", mat4(identity)));
    }
  }

  if (world.objectArrayParam("volume"))
    note("volumes of the world left out (the format has none)");

  try {
    m_scene->save(m_state.fileName);
  } catch (const std::exception &e) {
    m_reporter.reportMessage(ANARI_SEVERITY_ERROR,
        "writing '%s' failed: %s",
        m_state.fileName.c_str(),
        e.what());
    return;
  }

  writeReport(camera, renderer, frameSize);

  m_reporter.reportMessage(ANARI_SEVERITY_INFO,
      "wrote '%s': %zu instances, %zu meshes, %zu triangles, %zu textures, "
      "%zu quad lights, %zu directional lights",
      m_state.fileName.c_str(),
      m_scene->instances.size(),
      m_meshes.size(),
      m_numTriangles,
      m_textures.size(),
      m_scene->quadLights.size(),
      m_scene->dirLights.size());
  for (const auto &[what, count] : m_notes) {
    m_reporter.reportMessage(
        ANARI_SEVERITY_WARNING, "%s: %zu", what.c_str(), count);
  }
}

// Scene structure ////////////////////////////////////////////////////////////

void SceneWriter::addInstance(Object &group, const mat4 &transform)
{
  auto object = objectFor(group);
  if (object && !object->meshes.empty()) {
    m_scene->instances.push_back(
        mini::Instance::create(object, toAffine(transform)));
  }
  addLights(group, transform);
}

mini::Object::SP SceneWriter::objectFor(Object &group)
{
  if (auto found = m_objects.find(&group); found != m_objects.end())
    return found->second;

  auto object = mini::Object::create();
  if (auto *surfaces = group.objectArrayParam("surface")) {
    auto **handles = surfaces->handlesBegin();
    for (size_t i = 0; i < surfaces->totalSize(); i++) {
      auto *surface = static_cast<Object *>(handles[i]);
      if (!surface)
        continue;
      if (auto mesh = meshFor(*surface))
        object->meshes.push_back(mesh);
    }
  }
  if (group.type() == ANARI_GROUP && group.objectArrayParam("volume"))
    note("volumes of groups left out (the format has none)");

  m_objects[&group] = object;
  return object;
}

mini::Mesh::SP SceneWriter::meshFor(Object &surface)
{
  auto *geometry = surface.objectParam("geometry", ANARI_GEOMETRY);
  auto *material = surface.objectParam("material", ANARI_MATERIAL);
  if (!geometry)
    return {};

  const auto key = std::make_pair(geometry, material);
  if (auto found = m_meshes.find(key); found != m_meshes.end())
    return found->second;

  auto &mesh = m_meshes[key];

  if (geometry->subtype() != "triangle") {
    note("geometries of subtype '" + geometry->subtype()
        + "' left out (the format has triangle meshes only)");
    return mesh;
  }

  auto *positions = geometry->arrayParam("vertex.position");
  if (!positions || positions->elementType() != ANARI_FLOAT32_VEC3
      || positions->totalSize() == 0) {
    note("triangle geometries without float positions left out");
    return mesh;
  }
  const size_t numVertices = positions->totalSize();

  UvSource uv;
  auto miniMaterial = materialFor(material, *geometry, uv);

  mesh = mini::Mesh::create(miniMaterial);
  mesh->vertices.resize(numVertices);
  std::memcpy(mesh->vertices.data(),
      positions->data(),
      numVertices * sizeof(mini::vec3f));

  auto *indices = geometry->arrayParam("primitive.index");
  if (indices
      && (indices->elementType() == ANARI_UINT32_VEC3
          || indices->elementType() == ANARI_INT32_VEC3)) {
    mesh->indices.resize(indices->totalSize());
    std::memcpy(mesh->indices.data(),
        indices->data(),
        indices->totalSize() * sizeof(mini::vec3i));
  } else {
    // Without indices every three vertices are a triangle.
    mesh->indices.resize(numVertices / 3);
    for (size_t i = 0; i < mesh->indices.size(); i++)
      mesh->indices[i] = mini::vec3i(int(3 * i), int(3 * i + 1), int(3 * i + 2));
  }
  const size_t numTriangles = mesh->indices.size();
  m_numTriangles += numTriangles;

  // One value per vertex, or three per triangle: a reader tells the two apart
  // by the count.
  auto perVertexOrCorner = [&](const std::string &name) -> helium::Array * {
    if (auto *array = geometry->arrayParam(("vertex." + name).c_str());
        array && array->totalSize() == numVertices)
      return array;
    if (auto *array = geometry->arrayParam(("faceVarying." + name).c_str());
        array && array->totalSize() == 3 * numTriangles)
      return array;
    return nullptr;
  };

  if (auto *normals = perVertexOrCorner("normal");
      normals && normals->elementType() == ANARI_FLOAT32_VEC3) {
    mesh->normals.resize(normals->totalSize());
    std::memcpy(mesh->normals.data(),
        normals->data(),
        normals->totalSize() * sizeof(mini::vec3f));
  }

  if (uv.valid()) {
    if (auto *coordinates = perVertexOrCorner(uv.attribute)) {
      const bool transformed =
          uv.transform != mat4(identity) || uv.offset != float4(0.f);
      const auto type = coordinates->elementType();
      const void *data = coordinates->data();
      mesh->texcoords.resize(coordinates->totalSize());
      for (size_t i = 0; i < mesh->texcoords.size(); i++) {
        auto value = helium::readAsAttributeValueFlat(data, type, i);
        if (transformed)
          value = mul(uv.transform, value) + uv.offset;
        mesh->texcoords[i] = mini::vec2f(value.x, value.y);
      }
    } else {
      note("textured meshes without the attribute their textures read");
    }
  }

  if (geometry->arrayParam("vertex.tangent")
      || geometry->arrayParam("faceVarying.tangent"))
    note("meshes with tangents (the format has none, a reader derives them)");

  return mesh;
}

// Lights /////////////////////////////////////////////////////////////////////

void SceneWriter::addLights(Object &holder, const mat4 &transform)
{
  auto *lights = holder.objectArrayParam("light");
  if (!lights)
    return;
  auto **handles = lights->handlesBegin();
  for (size_t i = 0; i < lights->totalSize(); i++) {
    if (auto *light = static_cast<Object *>(handles[i]))
      addLight(*light, transform);
  }
}

void SceneWriter::addQuadLight(const float3 &corner,
    const float3 &edge0,
    const float3 &edge1,
    const float3 &emission)
{
  const float3 n = cross(edge0, edge1);
  const float area = length(n);
  if (area <= 0.f)
    return;

  mini::QuadLight quad;
  quad.corner = toMini(corner);
  quad.edge0 = toMini(edge0);
  quad.edge1 = toMini(edge1);
  quad.emission = toMini(emission);
  quad.normal = toMini(n / area);
  quad.area = area;
  m_scene->quadLights.push_back(quad);
}

void SceneWriter::addLight(Object &light, const mat4 &transform)
{
  const auto &subtype = light.subtype();
  const float3 color = light.getParam<float3>("color", float3(1.f));

  if (!light.getParam<bool>("visible", true)) {
    note("lights hidden from the camera written as visible (the format has "
         "no such flag)");
  }

  if (subtype == "directional") {
    const float3 direction = normalize(xfmVector(
        transform, light.getParam<float3>("direction", float3(0.f, 0.f, -1.f))));
    const float irradiance = light.getParam<float>("irradiance", 1.f);

    // Readers (hayStack, Vela) take the irradiance of the light as twice the
    // average of what the format calls its radiance.
    mini::DirLight dirLight;
    dirLight.direction = toMini(direction);
    dirLight.radiance = toMini(color * (0.5f * irradiance));
    m_scene->dirLights.push_back(dirLight);
    return;
  }

  if (subtype == "quad") {
    const float3 position =
        xfmPoint(transform, light.getParam<float3>("position", float3(0.f)));
    float3 edge1 = xfmVector(
        transform, light.getParam<float3>("edge1", float3(1.f, 0.f, 0.f)));
    float3 edge2 = xfmVector(
        transform, light.getParam<float3>("edge2", float3(0.f, 1.f, 0.f)));
    const float area = length(cross(edge1, edge2));
    if (area <= 0.f)
      return;

    // A quad emits like a diffuse surface: its intensity along the normal is
    // its area times its radiance, its power pi times that.
    const float radiance = light.hasParam("radiance")
        ? light.getParam<float>("radiance", 1.f)
        : light.hasParam("intensity")
        ? light.getParam<float>("intensity", 1.f) / area
        : light.hasParam("power")
        ? light.getParam<float>("power", 1.f) / (PI * area)
        : 1.f;
    const auto side = light.getParamString("side", "front");
    if (side != "back")
      addQuadLight(position, edge1, edge2, color * radiance);
    if (side != "front")
      addQuadLight(position, edge2, edge1, color * radiance);
    return;
  }

  if (subtype == "spot") {
    if (m_state.spotLight == "none") {
      note("spot lights left out (the format has none)");
      return;
    }

    const float3 position =
        xfmPoint(transform, light.getParam<float3>("position", float3(0.f)));
    const float3 direction = normalize(xfmVector(
        transform, light.getParam<float3>("direction", float3(0.f, 0.f, -1.f))));
    const float halfAngle =
        std::clamp(0.5f * light.getParam<float>("openingAngle", PI), 0.f, PI);
    const float falloffAngle =
        std::max(light.getParam<float>("falloffAngle", 0.1f), 0.f);
    const float cosOuter = std::cos(halfAngle);
    const float cosInner = std::cos(std::max(halfAngle - falloffAngle, 0.f));
    const float solidAngle = 2.f * PI * (1.f - cosOuter);
    const float radius = light.getParam<float>("radius", 0.f);
    const float intensity = intensityOf(light, solidAngle, PI * radius * radius);

    // What the spot emits: its intensity over the inner cone, and half of it
    // on average over the falloff, which is smooth in the cosine of the angle.
    const float power = intensity * 2.f * PI
        * ((1.f - cosInner) + 0.5f * (cosInner - cosOuter));

    // A square inside the disk of the spot, facing along its axis: a light is
    // often placed inside the lamp it belongs to, and must not reach out of
    // it. It cannot be confined to a cone: it either has the power of the spot
    // and is darker than it inside the cone, or has the intensity of the spot
    // on the axis and emits more than it in all.
    const float size = radius > 0.f ? radius * std::sqrt(2.f) : 0.01f;
    const float area = size * size;
    const float radiance = m_state.spotLight == "intensity"
        ? intensity / area
        : power / (PI * area);

    const float3 u = perpendicular(direction);
    const float3 v = cross(direction, u);
    addQuadLight(position - 0.5f * size * (u + v),
        size * u,
        size * v,
        color * radiance);
    note("spot lights written as a quad light (the format has no spot lights)");
    return;
  }

  if (subtype == "point") {
    if (m_state.pointLight == "none") {
      note("point lights left out (the format has none)");
      return;
    }

    const float3 position =
        xfmPoint(transform, light.getParam<float3>("position", float3(0.f)));
    const float radius = light.getParam<float>("radius", 0.f);
    const float intensity =
        intensityOf(light, 4.f * PI, PI * radius * radius);

    // A cube inside the sphere of the light (see the spot light), with the
    // power of the light: 4 pi I = 6 faces * pi * area * radiance.
    const float size = radius > 0.f ? radius * 2.f / std::sqrt(3.f) : 0.01f;
    const float radiance = 2.f * intensity / (3.f * size * size);

    const float3 axes[3] = {
        float3(1.f, 0.f, 0.f), float3(0.f, 1.f, 0.f), float3(0.f, 0.f, 1.f)};
    for (int axis = 0; axis < 3; axis++) {
      for (float sign : {-1.f, 1.f}) {
        const float3 n = sign * axes[axis];
        const float3 u = sign * axes[(axis + 1) % 3];
        const float3 v = cross(n, u);
        addQuadLight(position + 0.5f * size * (n - u - v),
            size * u,
            size * v,
            color * radiance);
      }
    }
    note("point lights written as a cube of six quad lights (the format has "
         "no point lights)");
    return;
  }

  if (subtype == "hdri") {
    auto radianceValue = light.getParamDirect("radiance");
    auto *radiance = radianceValue.valid()
            && radianceValue.type() == ANARI_ARRAY2D
        ? radianceValue.getObject<helium::Array2D>()
        : nullptr;
    if (!radiance || !radiance->data()) {
      note("hdri lights without a radiance image left out");
      return;
    }
    if (m_scene->envMapLight) {
      note("hdri lights left out (the format has one environment map)");
      return;
    }

    const size_t width = radiance->size().x;
    const size_t height = radiance->size().y;
    const float scale = light.getParam<float>("scale", 1.f);

    // The first row of the texture is the top of the map; the first row of
    // the radiance of an ANARI light is the bottom.
    auto texture = mini::Texture::create();
    texture->size = mini::vec2i(int(width), int(height));
    texture->format = mini::Texture::FLOAT4;
    texture->data.resize(width * height * sizeof(float4));
    auto *texels = reinterpret_cast<float4 *>(texture->data.data());
    for (size_t y = 0; y < height; y++) {
      for (size_t x = 0; x < width; x++) {
        const auto value = helium::readAsAttributeValueFlat(
            radiance->data(), radiance->elementType(), (height - 1 - y) * width + x);
        texels[y * width + x] = float4(value.x * color.x * scale,
            value.y * color.y * scale,
            value.z * color.z * scale,
            1.f);
      }
    }

    // Readers take the frame as up = vz and direction = -vx.
    const float3 up = normalize(
        xfmVector(transform, light.getParam<float3>("up", float3(0.f, 0.f, 1.f))));
    const float3 direction = normalize(xfmVector(
        transform, light.getParam<float3>("direction", float3(1.f, 0.f, 0.f))));
    auto envMap = mini::EnvMapLight::create();
    envMap->texture = texture;
    envMap->transform.l.vx = toMini(-direction);
    envMap->transform.l.vy = toMini(cross(up, -direction));
    envMap->transform.l.vz = toMini(up);
    envMap->transform.p = mini::vec3f(0.f, 0.f, 0.f);
    m_scene->envMapLight = envMap;
    return;
  }

  note("lights of subtype '" + subtype + "' left out");
}

// Materials //////////////////////////////////////////////////////////////////

mini::Material::SP SceneWriter::materialFor(
    Object *material, Object &geometry, UvSource &uv)
{
  if (auto found = m_materials.find({material, nullptr});
      found != m_materials.end()) {
    uv = found->second.uv;
    return found->second.material;
  }
  if (auto found = m_materials.find({material, &geometry});
      found != m_materials.end()) {
    uv = found->second.uv;
    return found->second.material;
  }

  if (!material) {
    // The default surface: a gray diffuse one.
    auto fallback = mini::ANARIMaterial::create();
    fallback->baseColor = mini::vec3f(0.8f, 0.8f, 0.8f);
    fallback->metallic = 0.f;
    fallback->specularColor = mini::vec3f(1.f, 1.f, 1.f);
    fallback->attenuationColor = mini::vec3f(1.f, 1.f, 1.f);
    fallback->alphaMode = mini::ANARIMaterial::AM_OPAQUE;
    m_materials[{nullptr, nullptr}] = {fallback, {}};
    uv = {};
    return fallback;
  }

  uv = uvSourceOf(*material);
  m_materialReadGeometry = false;
  auto converted = convertMaterial(*material, geometry, uv);
  if (m_materialReadGeometry)
    note("materials which read a vertex attribute, written once per mesh");
  m_materials[{material, m_materialReadGeometry ? &geometry : nullptr}] = {
      converted, uv};
  return converted;
}

SceneWriter::UvSource SceneWriter::uvSourceOfSampler(Object &sampler)
{
  UvSource retval;
  if (sampler.subtype() != "image2D")
    return retval;
  const auto attribute = sampler.getParamString("inAttribute", "attribute0");
  // Only the four generic attributes of a mesh can be its texture coordinates.
  if (attribute.rfind("attribute", 0) != 0)
    return retval;
  retval.attribute = attribute;
  retval.transform = sampler.getParam<mat4>("inTransform", mat4(identity));
  retval.offset = sampler.getParam<float4>("inOffset", float4(0.f));
  return retval;
}

SceneWriter::UvSource SceneWriter::uvSourceOf(Object &material) const
{
  // The textures of the first of these parameters which has one decide what
  // the texture coordinates of the mesh are.
  static const char *const order[] = {"baseColor",
      "color",
      "opacity",
      "emissive",
      "roughness",
      "metallic",
      "normal",
      "specular",
      "specularColor",
      "clearcoat",
      "clearcoatRoughness",
      "transmission",
      "occlusion",
      "sheenColor",
      "sheenRoughness",
      "clearcoatNormal",
      "thickness",
      "ior",
      "attenuationColor",
      "iridescence",
      "iridescenceThickness"};
  for (const char *name : order) {
    if (auto *sampler = material.objectParam(name, ANARI_SAMPLER)) {
      auto uv = uvSourceOfSampler(*sampler);
      if (uv.valid())
        return uv;
    }
  }
  return {};
}

SceneWriter::Value SceneWriter::valueOf(Object &material,
    const char *name,
    const float4 &defaultValue,
    Object &geometry,
    const UvSource &uv)
{
  Value retval;
  retval.constant = defaultValue;

  auto value = material.getParamDirect(name);
  if (!value.valid())
    return retval;

  if (constantOf(value, &retval.constant))
    return retval;

  if (value.type() == ANARI_STRING) {
    // The parameter is an attribute of the mesh.
    retval.constant = attributeAverage(geometry, value.getString());
    note("material parameters read from a vertex attribute, written as its "
         "average over the mesh");
    return retval;
  }

  auto *sampler = material.objectParam(name, ANARI_SAMPLER);
  if (!sampler)
    return retval;

  if (sampler->subtype() == "image2D") {
    auto texture = textureFor(*sampler);
    if (!texture)
      return retval;
    if (uvSourceOfSampler(*sampler) == uv) {
      retval.texture = texture;
    } else {
      retval.constant = averageOf(texture);
      note("textures on other texture coordinates than the base color, "
           "written as their average");
    }
    return retval;
  }

  if (sampler->subtype() == "transform") {
    const auto attribute = attributeAverage(
        geometry, sampler->getParamString("inAttribute", "attribute0"));
    retval.constant =
        mul(sampler->getParam<mat4>("outTransform", mat4(identity)), attribute)
        + sampler->getParam<float4>("outOffset", float4(0.f));
    note("transform samplers written as the transformed average of their "
         "attribute over the mesh");
    return retval;
  }

  note("samplers of subtype '" + sampler->subtype()
      + "' written as the default of their parameter");
  return retval;
}

mini::Texture::SP SceneWriter::textureOnly(
    Object &material, const char *name, const UvSource &uv)
{
  auto *sampler = material.objectParam(name, ANARI_SAMPLER);
  if (!sampler)
    return {};
  if (sampler->subtype() != "image2D" || !(uvSourceOfSampler(*sampler) == uv)) {
    note(std::string("'") + name
        + "' maps left out (not an image on the texture coordinates of the "
          "base color)");
    return {};
  }
  return textureFor(*sampler);
}

mini::Material::SP SceneWriter::convertMaterial(
    Object &material, Object &geometry, const UvSource &uv)
{
  auto retval = mini::ANARIMaterial::create();
  auto &m = *retval;

  auto color = [&](const char *name,
                   const float3 &defaultValue,
                   mini::vec3f &field,
                   mini::Texture::SP &texture) {
    const auto value = valueOf(material,
        name,
        float4(defaultValue.x, defaultValue.y, defaultValue.z, 1.f),
        geometry,
        uv);
    field = mini::vec3f(value.constant.x, value.constant.y, value.constant.z);
    texture = value.texture;
  };
  auto scalar = [&](const char *name,
                    float defaultValue,
                    float &field,
                    mini::Texture::SP &texture) {
    const auto value =
        valueOf(material, name, float4(defaultValue), geometry, uv);
    field = value.constant.x;
    texture = value.texture;
  };

  // What is not set is the default of ANARI, not the default of the format.
  if (material.subtype() == "matte") {
    color("color", float3(0.8f), m.baseColor, m.baseColor_texture);
    scalar("opacity", 1.f, m.opacity, m.opacity_texture);
    m.metallic = 0.f;
    m.roughness = 1.f;
    m.specular = 0.f;
    m.specularColor = mini::vec3f(1.f, 1.f, 1.f);
    m.attenuationColor = mini::vec3f(1.f, 1.f, 1.f);
    m.iridescenceIor = 1.3f;
    note("matte materials written as a rough physicallyBased one without "
         "specular (the ANARI material of the format is physicallyBased)");
  } else {
    if (material.subtype() != "physicallyBased") {
      note("materials of subtype '" + material.subtype()
          + "' written as physicallyBased");
    }
    color("baseColor", float3(1.f), m.baseColor, m.baseColor_texture);
    scalar("opacity", 1.f, m.opacity, m.opacity_texture);
    scalar("metallic", 1.f, m.metallic, m.metallic_texture);
    scalar("roughness", 1.f, m.roughness, m.roughness_texture);
    m.normal_texture = textureOnly(material, "normal", uv);
    color("emissive", float3(0.f), m.emissive, m.emissive_texture);
    m.occlusion_texture = textureOnly(material, "occlusion", uv);
    scalar("specular", 0.f, m.specular, m.specular_texture);
    color("specularColor", float3(1.f), m.specularColor, m.specularColor_texture);
    scalar("clearcoat", 0.f, m.clearcoat, m.clearcoat_texture);
    scalar("clearcoatRoughness",
        0.f,
        m.clearcoatRoughness,
        m.clearcoatRoughness_texture);
    m.clearcoatNormal_texture = textureOnly(material, "clearcoatNormal", uv);
    scalar("transmission", 0.f, m.transmission, m.transmission_texture);
    scalar("ior", 1.5f, m.ior, m.ior_texture);
    scalar("thickness", 0.f, m.thickness, m.thickness_texture);
    m.attenuationDistance = material.getParam<float>(
        "attenuationDistance", std::numeric_limits<float>::infinity());
    color("attenuationColor",
        float3(1.f),
        m.attenuationColor,
        m.attenuationColor_texture);
    color("sheenColor", float3(0.f), m.sheenColor, m.sheenColor_texture);
    scalar("sheenRoughness", 0.f, m.sheenRoughness, m.sheenRoughness_texture);
    scalar("iridescence", 0.f, m.iridescence, m.iridescence_texture);
    mini::Texture::SP noTexture;
    scalar("iridescenceIor", 1.3f, m.iridescenceIor, noTexture);
    scalar("iridescenceThickness",
        0.f,
        m.iridescenceThickness,
        m.iridescenceThickness_texture);
  }

  const auto alphaMode = material.getParamString("alphaMode", "opaque");
  m.alphaMode = alphaMode == "blend" ? mini::ANARIMaterial::AM_BLEND
      : alphaMode == "mask"          ? mini::ANARIMaterial::AM_MASK
                                     : mini::ANARIMaterial::AM_OPAQUE;
  m.alphaCutoff = material.getParam<float>("alphaCutoff", 0.5f);

  return retval;
}

mini::Texture::SP SceneWriter::textureFor(Object &sampler)
{
  auto imageValue = sampler.getParamDirect("image");
  auto *image = imageValue.valid() && imageValue.type() == ANARI_ARRAY2D
      ? imageValue.getObject<helium::Array2D>()
      : nullptr;
  if (!image || !image->data() || image->totalSize() == 0)
    return {};

  const mat4 outTransform = sampler.getParam<mat4>("outTransform", mat4(identity));
  const float4 outOffset = sampler.getParam<float4>("outOffset", float4(0.f));
  const bool nearest = sampler.getParamString("filter", "linear") == "nearest";

  TextureKey key;
  key.first = image;
  for (int column = 0; column < 4; column++) {
    for (int row = 0; row < 4; row++)
      key.second[column * 4 + row] = outTransform[column][row];
    key.second[16 + column] = outOffset[column];
  }
  key.second[20] = nearest ? 1.f : 0.f;

  if (auto found = m_textures.find(key); found != m_textures.end())
    return found->second;

  for (const char *wrap : {"wrapMode1", "wrapMode2"}) {
    const auto mode = sampler.getParamString(wrap, "clampToEdge");
    if (mode != "repeat") {
      note("texture wrap mode '" + mode
          + "' not written (the format has none, a reader picks one)");
      break;
    }
  }

  const size_t width = image->size().x;
  const size_t height = image->size().y;
  const size_t numTexels = width * height;
  const auto type = image->elementType();
  const bool isIdentity =
      outTransform == mat4(identity) && outOffset == float4(0.f);

  auto texture = mini::Texture::create();
  texture->size = mini::vec2i(int(width), int(height));
  texture->filterMode = nearest ? mini::Texture::FILTER_NEAREST
                                : mini::Texture::FILTER_BILINEAR;

  if (isIdentity && type == ANARI_UFIXED8_VEC4) {
    // Linear 8-bit texels are what a reader takes them for.
    texture->format = mini::Texture::RGBA_UINT8;
    texture->data.resize(numTexels * 4);
    std::memcpy(texture->data.data(), image->data(), texture->data.size());
  } else {
    // Decoded and transformed here, both of which commute with the filtering
    // of the texture, so the float texels give what the sampler gave.
    texture->format = mini::Texture::FLOAT4;
    texture->data.resize(numTexels * sizeof(float4));
    auto *texels = reinterpret_cast<float4 *>(texture->data.data());
    for (size_t i = 0; i < numTexels; i++) {
      const auto texel = helium::readAsAttributeValueFlat(image->data(), type, i);
      texels[i] = isIdentity ? texel : mul(outTransform, texel) + outOffset;
    }
  }

  m_numTexels += numTexels;
  m_textures[key] = texture;
  return texture;
}

SceneWriter::float4 SceneWriter::averageOf(const mini::Texture::SP &texture)
{
  if (auto found = m_textureAverages.find(texture);
      found != m_textureAverages.end())
    return found->second;

  const size_t numTexels =
      size_t(texture->size.x) * size_t(texture->size.y);
  double sum[4] = {0., 0., 0., 0.};
  if (texture->format == mini::Texture::FLOAT4) {
    const auto *texels = reinterpret_cast<const float *>(texture->data.data());
    for (size_t i = 0; i < numTexels * 4; i++)
      sum[i % 4] += texels[i];
  } else {
    for (size_t i = 0; i < numTexels * 4; i++)
      sum[i % 4] += texture->data[i] / 255.;
  }

  float4 average(0.f, 0.f, 0.f, 1.f);
  if (numTexels > 0) {
    average = float4(float(sum[0] / numTexels),
        float(sum[1] / numTexels),
        float(sum[2] / numTexels),
        float(sum[3] / numTexels));
  }
  m_textureAverages[texture] = average;
  return average;
}

SceneWriter::float4 SceneWriter::attributeAverage(
    Object &geometry, const std::string &attribute)
{
  m_materialReadGeometry = true;

  const auto key = std::make_pair(&geometry, attribute);
  if (auto found = m_attributeAverages.find(key);
      found != m_attributeAverages.end())
    return found->second;

  // The value of an attribute a mesh does not have.
  float4 average(0.f, 0.f, 0.f, 1.f);

  helium::Array *array = nullptr;
  for (const char *scope : {"vertex.", "faceVarying.", "primitive."}) {
    if ((array = geometry.arrayParam((scope + attribute).c_str())))
      break;
  }

  if (array && array->data() && array->totalSize() > 0) {
    double sum[4] = {0., 0., 0., 0.};
    const size_t count = array->totalSize();
    for (size_t i = 0; i < count; i++) {
      const auto value = helium::readAsAttributeValueFlat(
          array->data(), array->elementType(), i);
      for (int c = 0; c < 4; c++)
        sum[c] += value[c];
    }
    average = float4(float(sum[0] / count),
        float(sum[1] / count),
        float(sum[2] / count),
        float(sum[3] / count));
  } else {
    // One value for the whole geometry.
    constantOf(geometry.getParamDirect(attribute), &average);
  }

  m_attributeAverages[key] = average;
  return average;
}

// Report /////////////////////////////////////////////////////////////////////

void SceneWriter::note(const std::string &what, size_t count)
{
  m_notes[what] += count;
}

void SceneWriter::writeReport(
    Object *camera, Object *renderer, uint2 frameSize) const
{
  std::ofstream out(m_state.fileName + ".txt");
  if (!out)
    return;

  auto vec = [](const float3 &v) {
    return std::to_string(v.x) + " " + std::to_string(v.y) + " "
        + std::to_string(v.z);
  };

  out << "miniScene export of an ANARI scene\n";
  out << "file: " << m_state.fileName << "\n";
  out << "instances: " << m_scene->instances.size() << "\n";
  out << "meshes: " << m_meshes.size() << "\n";
  out << "triangles: " << m_numTriangles << "\n";
  out << "textures: " << m_textures.size() << " (" << m_numTexels
      << " texels)\n";
  out << "quad lights: " << m_scene->quadLights.size() << "\n";
  out << "directional lights: " << m_scene->dirLights.size() << "\n";
  out << "environment map: " << (m_scene->envMapLight ? "yes" : "no") << "\n";

  out << "\n# Camera (the format has none)\n";
  if (camera) {
    const float3 position = camera->getParam<float3>("position", float3(0.f));
    const float3 direction =
        camera->getParam<float3>("direction", float3(0.f, 0.f, -1.f));
    const float3 up = camera->getParam<float3>("up", float3(0.f, 1.f, 0.f));
    out << "subtype: " << camera->subtype() << "\n";
    out << "position: " << vec(position) << "\n";
    out << "direction: " << vec(direction) << "\n";
    out << "up: " << vec(up) << "\n";
    out << "lookAt: " << vec(position + direction) << "\n";
    if (camera->hasParam("fovy")) {
      const float fovy = camera->getParam<float>("fovy", PI / 3.f);
      out << "fovy: " << fovy << " rad = " << fovy * 180.f / PI << " deg\n";
    }
    if (camera->hasParam("height"))
      out << "height: " << camera->getParam<float>("height", 1.f) << "\n";
    out << "aspect: " << camera->getParam<float>("aspect", 1.f) << "\n";
    if (camera->hasParam("near"))
      out << "near: " << camera->getParam<float>("near", 0.f) << "\n";
    if (camera->hasParam("far"))
      out << "far: " << camera->getParam<float>("far", 0.f) << "\n";
    out << "apertureRadius: " << camera->getParam<float>("apertureRadius", 0.f)
        << "\n";
    out << "focusDistance: " << camera->getParam<float>("focusDistance", 1.f)
        << "\n";
  } else {
    out << "none\n";
  }
  out << "frame size: " << frameSize.x << " " << frameSize.y << "\n";

  out << "\n# Renderer (the format has none)\n";
  if (renderer) {
    if (renderer->hasParam("background")) {
      float4 background(0.f, 0.f, 0.f, 1.f);
      if (constantOf(renderer->getParamDirect("background"), &background)) {
        out << "background: " << background.x << " " << background.y << " "
            << background.z << " " << background.w << "\n";
      } else {
        out << "background: image\n";
      }
    }
    if (renderer->hasParam("ambientColor")) {
      out << "ambientColor: "
          << vec(renderer->getParam<float3>("ambientColor", float3(1.f)))
          << "\n";
    }
    for (const char *name : {"ambientRadiance", "clampDirect", "clampIndirect"}) {
      if (renderer->hasParam(name))
        out << name << ": " << renderer->getParam<float>(name, 0.f) << "\n";
    }
    for (const char *name : {"pixelSamples",
             "maxBounce",
             "maxDiffuseBounce",
             "maxGlossyBounce",
             "maxTransmissionBounce",
             "maxRayDepth"}) {
      if (renderer->hasParam(name))
        out << name << ": " << renderer->getParam<int>(name, 0) << "\n";
    }
    if (renderer->hasParam("denoise")) {
      out << "denoise: " << (renderer->getParam<bool>("denoise", false) ? 1 : 0)
          << "\n";
    }
  } else {
    out << "none\n";
  }

  out << "\n# Changed or left out\n";
  if (m_notes.empty())
    out << "nothing\n";
  for (const auto &[what, count] : m_notes)
    out << count << " x " << what << "\n";
}

} // namespace miniscene
