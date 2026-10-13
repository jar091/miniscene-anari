// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
#include "helium/BaseObject.h"
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/array/ObjectArray.h"
// std
#include <string>
#include <string_view>

namespace miniscene {

// What is set on the device and shared by its objects.
struct GlobalState : public helium::BaseGlobalDeviceState
{
  GlobalState(ANARIDevice d) : helium::BaseGlobalDeviceState(d) {}

  std::string fileName{"scene.mini"};
  std::string spotLight{"power"};
  std::string pointLight{"cube"};
};

/*
 * Every object of the scene: it keeps its subtype and the parameters it was
 * given. The device renders nothing, so an object has nothing to build when
 * it is committed; SceneWriter reads the parameters when a frame is rendered.
 */
struct Object : public helium::BaseObject
{
  Object(ANARIDataType type, GlobalState *state, const char *subtype = "");
  ~Object() override = default;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;
  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  const std::string &subtype() const;

  // An object parameter of the given ANARI type, null when it is not set or
  // holds something else.
  Object *objectParam(const char *name, ANARIDataType type) const;
  // An array parameter of plain data, with one or two dimensions.
  helium::Array *arrayParam(const char *name) const;
  helium::ObjectArray *objectArrayParam(const char *name) const;

 private:
  std::string m_subtype;
};

} // namespace miniscene

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(miniscene::Object *, ANARI_OBJECT);
