// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// helium
#include "helium/BaseFrame.h"
// std
#include <vector>

namespace miniscene {

/*
 * Rendering the frame writes its world to the file of the device, once for
 * every change of the scene: an application which accumulates samples renders
 * the same frame many times. The image is a flat gray, so that an application
 * which shows it shows that nothing was rendered rather than garbage.
 */
struct Frame : public helium::BaseFrame
{
  Frame(GlobalState *state);
  ~Frame() override = default;

  bool isValid() const override;
  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;
  void commitParameters() override;
  void finalize() override;

  void renderFrame() override;
  void *map(std::string_view channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;
  void unmap(std::string_view channel) override;
  int frameReady(ANARIWaitMask m) override;
  void discard() override;

 private:
  GlobalState *deviceState() const;

  anari::math::uint2 m_size{0u, 0u};
  ANARIDataType m_colorType{ANARI_UNKNOWN};
  ANARIDataType m_depthType{ANARI_UNKNOWN};
  std::vector<uint8_t> m_color;
  std::vector<float> m_depth;
  helium::TimeStamp m_lastWritten{0};
  float m_duration{0.f};
};

} // namespace miniscene

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(miniscene::Frame *, ANARI_FRAME);
