cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(compilerconfig)
include(defaults)
include(helpers)

option(BUILD_TESTS "Build unit tests" ON)

find_package(libobs REQUIRED)

include(FetchContent)

# Third-party code is declared SYSTEM: its headers are included as external, so
# their warnings are not reported against our sources.
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  SYSTEM)
FetchContent_MakeAvailable(json)

set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_MBED_TLS ON CACHE BOOL "" FORCE)
FetchContent_Declare(ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5
  SYSTEM)
FetchContent_MakeAvailable(ixwebsocket)
# IXWebSocket's own sources warn about narrowing (Windows SOCKET -> int,
# size_t -> int) on paths that are harmless for us; reviewed 2026-10-20.
# Silence them so the build log only shows warnings from our code, and never
# let them fail the build. COMPILE_WARNING_AS_ERROR covers CMake's mechanism;
# the macOS Xcode generator applies -Werror via GCC_TREAT_WARNINGS_AS_ERRORS.
set_target_properties(ixwebsocket PROPERTIES
  COMPILE_WARNING_AS_ERROR OFF
  XCODE_ATTRIBUTE_GCC_TREAT_WARNINGS_AS_ERRORS NO)
target_compile_options(ixwebsocket PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)

# Our own targets build with a stricter warning level than the template's /W3;
# warnings are errors by default (CMAKE_COMPILE_WARNING_AS_ERROR).
set(VILA_GLT_WARNINGS $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>)

include(cmake/common/embed-models.cmake)

add_library(${CMAKE_PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/api-key.cpp
  src/model-catalog.cpp
  src/model-store.cpp
  ${MODELS_JSON_CPP}
  src/base64.cpp
  src/dotenv.cpp
  src/audio-convert.cpp
  src/audio-pacing.cpp
  src/ring-buffer.cpp
  src/live-protocol.cpp
  src/backoff.cpp
  src/output-delay.cpp
  src/session-config.cpp
  src/source.cpp
  src/translation-session.cpp
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${VILA_GLT_WARNINGS})
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
  OBS::libobs
  ixwebsocket
  nlohmann_json::nlohmann_json
  # bcrypt is a Windows crypto library needed by mbedTLS there; not on mac/Linux.
  $<$<PLATFORM_ID:Windows>:bcrypt>
)
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
