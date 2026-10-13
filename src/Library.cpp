// SPDX-License-Identifier: Apache-2.0

#include "Device.h"
// anari
#include "anari/backend/LibraryImpl.h"

#include "anari_library_miniscene_export.h"

namespace miniscene {

const char **query_extensions();

struct Library : public anari::LibraryImpl
{
  Library(void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

Library::Library(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice Library::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new Device(this_library());
}

const char **Library::getDeviceExtensions(const char * /*deviceType*/)
{
  return query_extensions();
}

} // namespace miniscene

// Define library entrypoint //////////////////////////////////////////////////

extern "C" MINISCENE_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    miniscene, handle, scb, scbPtr)
{
  return (ANARILibrary) new miniscene::Library(handle, scb, scbPtr);
}
