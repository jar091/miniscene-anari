// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// helium
#include "helium/helium_math.h"
// miniScene
#include "miniScene/Scene.h"
// std
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace miniscene {

/*
 * Writes an ANARI world to a miniScene file, within what the format holds.
 *
 * A .mini file has triangle meshes with one set of texture coordinates and
 * one material each, materials with one untransformed texture per parameter,
 * quad, directional and environment lights, and no camera. What an ANARI
 * scene has beyond that is brought into this form where it can be:
 *
 *  - the output transform of a sampler is applied to the texels, which is
 *    exact, as is decoding sRGB texels (a reader takes 8-bit texels as linear)
 *  - a mesh gets the attribute which the base color of its material reads as
 *    its texture coordinates, with the input transform of the sampler applied
 *  - a parameter read from another attribute, from a vertex attribute directly
 *    or from a sampler of another kind becomes a constant: the average of the
 *    texture, or of the attribute over the mesh
 *  - a spot light becomes a quad light and a point light a cube of six, see
 *    the device parameters `spotLight` and `pointLight`
 *
 * Everything which was changed or left out is counted, and written with the
 * camera and the renderer parameters to a report next to the file
 * (<fileName>.txt), which is all a reader gets of those two.
 *
 * Example:
 *   SceneWriter writer(state, frame);
 *   writer.write(world, camera, renderer, frameSize);
 */
class SceneWriter
{
 public:
  SceneWriter(const GlobalState &state, const helium::BaseObject &reporter);

  void write(Object &world,
      Object *camera,
      Object *renderer,
      anari::math::uint2 frameSize);

 private:
  using float4 = anari::math::float4;
  using mat4 = anari::math::mat4;

  // The attribute a texture is looked up with, as an image sampler has it.
  struct UvSource
  {
    std::string attribute;
    mat4 transform{anari::math::identity};
    float4 offset{0.f, 0.f, 0.f, 0.f};

    bool valid() const;
    bool operator==(const UvSource &other) const;
  };

  // A material parameter as the file can hold it.
  struct Value
  {
    float4 constant{0.f, 0.f, 0.f, 1.f};
    mini::Texture::SP texture;
  };

  // Scene structure //

  mini::Object::SP objectFor(Object &group);
  mini::Mesh::SP meshFor(Object &surface);
  void addInstance(Object &group, const mat4 &transform);
  void addLights(Object &holder, const mat4 &transform);
  void addLight(Object &light, const mat4 &transform);
  void addQuadLight(const anari::math::float3 &corner,
      const anari::math::float3 &edge0,
      const anari::math::float3 &edge1,
      const anari::math::float3 &emission);

  // Materials //

  mini::Material::SP materialFor(Object *material, Object &geometry, UvSource &uv);
  mini::Material::SP convertMaterial(
      Object &material, Object &geometry, const UvSource &uv);
  UvSource uvSourceOf(Object &material) const;
  static UvSource uvSourceOfSampler(Object &sampler);
  Value valueOf(Object &material,
      const char *name,
      const float4 &defaultValue,
      Object &geometry,
      const UvSource &uv);
  mini::Texture::SP textureOnly(
      Object &material, const char *name, const UvSource &uv);
  mini::Texture::SP textureFor(Object &sampler);
  float4 averageOf(const mini::Texture::SP &texture);
  float4 attributeAverage(Object &geometry, const std::string &attribute);

  // Report //

  void note(const std::string &what, size_t count = 1);
  void writeReport(
      Object *camera, Object *renderer, anari::math::uint2 frameSize) const;

  const GlobalState &m_state;
  const helium::BaseObject &m_reporter;

  mini::Scene::SP m_scene;
  std::map<Object *, mini::Object::SP> m_objects;
  std::map<std::pair<Object *, Object *>, mini::Mesh::SP> m_meshes;

  struct ConvertedMaterial
  {
    mini::Material::SP material;
    UvSource uv;
  };
  // Keyed by material and geometry; the geometry is null for a material which
  // reads nothing from the mesh it is on.
  std::map<std::pair<Object *, Object *>, ConvertedMaterial> m_materials;
  bool m_materialReadGeometry{false};

  using TextureKey = std::pair<const void *, std::array<float, 21>>;
  std::map<TextureKey, mini::Texture::SP> m_textures;
  std::map<mini::Texture::SP, float4> m_textureAverages;
  std::map<std::pair<Object *, std::string>, float4> m_attributeAverages;

  std::map<std::string, size_t> m_notes;
  size_t m_numTriangles{0};
  size_t m_numTexels{0};
};

} // namespace miniscene
