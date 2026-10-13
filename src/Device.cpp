// SPDX-License-Identifier: Apache-2.0

#include "Device.h"
#include "Frame.h"
// helium
#include "helium/array/Array3D.h"
// std
#include <cstdlib>
#include <cstring>

#include "anari_library_miniscene_queries.h"

namespace miniscene {

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D Device::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  helium::Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return (ANARIArray1D) new helium::ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new helium::Array1D(deviceState(), md);
}

ANARIArray2D Device::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  helium::Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;

  return (ANARIArray2D) new helium::Array2D(deviceState(), md);
}

ANARIArray3D Device::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  helium::Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;

  return (ANARIArray3D) new helium::Array3D(deviceState(), md);
}

ANARIObject Device::newObject(ANARIDataType type, const char *subtype)
{
  return (ANARIObject) new Object(type, deviceState(), subtype);
}

ANARICamera Device::newCamera(const char *subtype)
{
  return (ANARICamera)newObject(ANARI_CAMERA, subtype);
}

ANARIFrame Device::newFrame()
{
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGeometry Device::newGeometry(const char *subtype)
{
  return (ANARIGeometry)newObject(ANARI_GEOMETRY, subtype);
}

ANARIGroup Device::newGroup()
{
  return (ANARIGroup)newObject(ANARI_GROUP);
}

ANARIInstance Device::newInstance(const char *subtype)
{
  return (ANARIInstance)newObject(ANARI_INSTANCE, subtype);
}

ANARILight Device::newLight(const char *subtype)
{
  return (ANARILight)newObject(ANARI_LIGHT, subtype);
}

ANARIMaterial Device::newMaterial(const char *subtype)
{
  return (ANARIMaterial)newObject(ANARI_MATERIAL, subtype);
}

ANARIRenderer Device::newRenderer(const char *subtype)
{
  return (ANARIRenderer)newObject(ANARI_RENDERER, subtype);
}

ANARISampler Device::newSampler(const char *subtype)
{
  return (ANARISampler)newObject(ANARI_SAMPLER, subtype);
}

ANARISpatialField Device::newSpatialField(const char *subtype)
{
  return (ANARISpatialField)newObject(ANARI_SPATIAL_FIELD, subtype);
}

ANARISurface Device::newSurface()
{
  return (ANARISurface)newObject(ANARI_SURFACE);
}

ANARIVolume Device::newVolume(const char *subtype)
{
  return (ANARIVolume)newObject(ANARI_VOLUME, subtype);
}

ANARIWorld Device::newWorld()
{
  return (ANARIWorld)newObject(ANARI_WORLD);
}

// Query functions ////////////////////////////////////////////////////////////

const char **Device::getObjectSubtypes(ANARIDataType objectType)
{
  return miniscene::query_object_types(objectType);
}

const void *Device::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return miniscene::query_object_info(
      objectType, objectSubtype, infoName, infoType);
}

const void *Device::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return miniscene::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other //////////////////////////////////////////////////////////////////////

Device::Device(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<GlobalState>(this_device());
  deviceCommitParameters();
}

Device::Device(ANARILibrary l) : helium::BaseDevice(l)
{
  m_state = std::make_unique<GlobalState>(this_device());
  deviceCommitParameters();
}

Device::~Device()
{
  deviceState()->commitBuffer.clear();
}

void Device::deviceCommitParameters()
{
  auto &state = *deviceState();

  // An application which cannot set device parameters (a viewer, a test
  // suite) names the file in the environment instead.
  const char *fileNameFromEnv = getenv("MINISCENE_ANARI_FILE");
  state.fileName = getParamString(
      "fileName", fileNameFromEnv ? fileNameFromEnv : "scene.mini");
  state.spotLight = getParamString("spotLight", "power");
  state.pointLight = getParamString("pointLight", "cube");

  helium::BaseDevice::deviceCommitParameters();
}

int Device::deviceGetProperty(const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t /*mask*/)
{
  static const std::string version = "miniscene 0.1.0";

  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, query_extensions());
    return 1;
  } else if (prop == "version" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 100);
    return 1;
  } else if (prop == "version.major" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 0);
    return 1;
  } else if (prop == "version.minor" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 1);
    return 1;
  } else if (prop == "version.patch" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 0);
    return 1;
  } else if (prop == "version.name.size" && type == ANARI_UINT64) {
    helium::writeToVoidP(mem, uint64_t(version.size() + 1));
    return 1;
  } else if (prop == "version.name" && type == ANARI_STRING) {
    std::memset(mem, 0, size);
    std::memcpy(mem, version.data(), std::min(uint64_t(version.size()), size - 1));
    return 1;
  } else if (prop == "anariVersion.major" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 1);
    return 1;
  } else if (prop == "anariVersion.minor" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 1);
    return 1;
  }
  return 0;
}

GlobalState *Device::deviceState() const
{
  return (GlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace miniscene
