// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
#include "SceneWriter.h"
// std
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace miniscene {

Frame::Frame(GlobalState *state) : helium::BaseFrame(state) {}

bool Frame::isValid() const
{
  return true;
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t /*size*/,
    uint32_t /*flags*/)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  }
  if (type == ANARI_INT32 && name == "numSamples") {
    helium::writeToVoidP(ptr, 1);
    return true;
  }
  return false;
}

void Frame::commitParameters()
{
  m_size = getParam<anari::math::uint2>("size", anari::math::uint2(0u, 0u));
  m_colorType = getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_depthType = getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
}

void Frame::finalize()
{
  const size_t numPixels = size_t(m_size.x) * size_t(m_size.y);
  const float gray = 0.18f;

  m_color.clear();
  if (m_colorType == ANARI_FLOAT32_VEC4) {
    const float pixel[4] = {gray, gray, gray, 1.f};
    m_color.resize(numPixels * sizeof(pixel));
    for (size_t i = 0; i < numPixels; i++)
      std::memcpy(m_color.data() + i * sizeof(pixel), pixel, sizeof(pixel));
  } else if (m_colorType == ANARI_UFIXED8_VEC4
      || m_colorType == ANARI_UFIXED8_RGBA_SRGB) {
    // Middle gray is 46 as linear and 118 as sRGB encoded value.
    const uint8_t value = m_colorType == ANARI_UFIXED8_VEC4 ? 46 : 118;
    const uint8_t pixel[4] = {value, value, value, 255};
    m_color.resize(numPixels * sizeof(pixel));
    for (size_t i = 0; i < numPixels; i++)
      std::memcpy(m_color.data() + i * sizeof(pixel), pixel, sizeof(pixel));
  }

  m_depth.clear();
  if (m_depthType == ANARI_FLOAT32)
    m_depth.assign(numPixels, std::numeric_limits<float>::infinity());
}

void Frame::renderFrame()
{
  auto *state = deviceState();
  const auto start = std::chrono::steady_clock::now();

  state->commitBuffer.flush();

  // The same scene rendered again (the next sample of an accumulation) is
  // already in the file.
  if (state->commitBuffer.lastObjectFinalization() > m_lastWritten) {
    m_lastWritten = helium::newTimeStamp();

    auto objectParam = [&](const char *name, ANARIDataType type) -> Object * {
      auto value = getParamDirect(name);
      return value.valid() && value.type() == type ? value.getObject<Object>()
                                                   : nullptr;
    };

    auto *world = objectParam("world", ANARI_WORLD);
    if (!world) {
      reportMessage(ANARI_SEVERITY_ERROR,
          "the frame has no world, no scene is written");
    } else {
      SceneWriter writer(*state, *this);
      writer.write(*world,
          objectParam("camera", ANARI_CAMERA),
          objectParam("renderer", ANARI_RENDERER),
          m_size);
    }
  }

  const auto end = std::chrono::steady_clock::now();
  m_duration = std::chrono::duration<float>(end - start).count();
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  *width = m_size.x;
  *height = m_size.y;

  if (channel == "channel.color" && !m_color.empty()) {
    *pixelType = m_colorType;
    return m_color.data();
  } else if (channel == "channel.depth" && !m_depth.empty()) {
    *pixelType = ANARI_FLOAT32;
    return m_depth.data();
  }

  *width = 0;
  *height = 0;
  *pixelType = ANARI_UNKNOWN;
  return nullptr;
}

void Frame::unmap(std::string_view /*channel*/)
{
  // no-op
}

int Frame::frameReady(ANARIWaitMask /*m*/)
{
  return 1;
}

void Frame::discard()
{
  // The frame is written before renderFrame() returns.
}

GlobalState *Frame::deviceState() const
{
  return (GlobalState *)helium::BaseObject::m_state;
}

} // namespace miniscene

HELIUM_ANARI_TYPEFOR_DEFINITION(miniscene::Frame *);
