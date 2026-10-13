cmake_minimum_required(VERSION 3.24)

project(miniscene-anari
  VERSION 0.1.0
  DESCRIPTION "Khronos ANARI device which writes the scene to a miniScene (.mini) file"
  LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported; use a build directory.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(GenerateExportHeader)

# The code_gen component carries anari_generate_queries().
find_package(anari REQUIRED COMPONENTS code_gen)

# miniScene is the submodule external/miniScene. A reader of the files written
# here has to read the layout of that commit (Vela pins its own copy in
# Vela/external/vela_miniscene). Only the two files of the core library are
# built: the project's own CMakeLists adds importers and tools which are not
# needed.
set(MINISCENE_ANARI_MINISCENE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/miniScene"
  CACHE PATH "Directory of the miniScene sources")
if(NOT EXISTS "${MINISCENE_ANARI_MINISCENE_DIR}/miniScene/Scene.cpp")
  message(FATAL_ERROR
    "miniScene sources not found in ${MINISCENE_ANARI_MINISCENE_DIR}; "
    "run `git submodule update --init` or set MINISCENE_ANARI_MINISCENE_DIR.")
endif()

add_library(miniscene_core STATIC
  ${MINISCENE_ANARI_MINISCENE_DIR}/miniScene/Scene.cpp
  ${MINISCENE_ANARI_MINISCENE_DIR}/miniScene/Serialized.cpp)
target_include_directories(miniscene_core PUBLIC ${MINISCENE_ANARI_MINISCENE_DIR})
# Serial parallel_for: the library then needs nothing beyond its own headers.
target_compile_definitions(miniscene_core PUBLIC OWL_DISABLE_TBB)
if(MSVC)
  target_compile_definitions(miniscene_core PUBLIC
    NOMINMAX _USE_MATH_DEFINES __PRETTY_FUNCTION__=__FUNCSIG__)
  target_compile_options(miniscene_core PRIVATE /W0)
else()
  target_compile_options(miniscene_core PRIVATE -w)
endif()

add_library(anari_library_miniscene SHARED
  src/Device.cpp
  src/Frame.cpp
  src/Library.cpp
  src/Object.cpp
  src/SceneWriter.cpp)

anari_generate_queries(
  DEVICE_TARGET anari_library_miniscene
  CPP_NAMESPACE miniscene
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/MiniSceneDefinitions.json)

generate_export_header(anari_library_miniscene EXPORT_MACRO_NAME "MINISCENE_EXPORT")

target_include_directories(anari_library_miniscene PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(anari_library_miniscene PRIVATE anari::helium miniscene_core)
if(MSVC)
  target_compile_options(anari_library_miniscene PRIVATE /bigobj)
endif()

install(TARGETS anari_library_miniscene
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/miniscene-anari")
