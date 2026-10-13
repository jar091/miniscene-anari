// SPDX-License-Identifier: Apache-2.0

#include "Object.h"

namespace miniscene {

Object::Object(ANARIDataType type, GlobalState *state, const char *subtype)
    : helium::BaseObject(type, state), m_subtype(subtype ? subtype : "")
{}

bool Object::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t /*size*/,
    uint32_t /*flags*/)
{
  if (name == "valid" && type == ANARI_BOOL) {
    helium::writeToVoidP(ptr, isValid());
    return true;
  }
  return false;
}

void Object::commitParameters()
{
  // The parameters are read when the scene is written.
}

void Object::finalize()
{
  // Nothing is built.
}

bool Object::isValid() const
{
  return true;
}

const std::string &Object::subtype() const
{
  return m_subtype;
}

Object *Object::objectParam(const char *name, ANARIDataType type) const
{
  auto value = getParamDirect(name);
  if (!value.valid() || value.type() != type)
    return nullptr;
  return value.getObject<Object>();
}

helium::Array *Object::arrayParam(const char *name) const
{
  auto value = getParamDirect(name);
  if (!value.valid() || !anari::isArray(value.type()))
    return nullptr;
  auto *array = value.getObject<helium::Array>();
  if (!array || anari::isObject(array->elementType()))
    return nullptr;
  return array;
}

helium::ObjectArray *Object::objectArrayParam(const char *name) const
{
  auto value = getParamDirect(name);
  if (!value.valid() || !anari::isArray(value.type()))
    return nullptr;
  auto *array = value.getObject<helium::Array>();
  if (!array || !anari::isObject(array->elementType()))
    return nullptr;
  return static_cast<helium::ObjectArray *>(array);
}

} // namespace miniscene

HELIUM_ANARI_TYPEFOR_DEFINITION(miniscene::Object *);
