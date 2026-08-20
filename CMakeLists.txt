cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(OS_WINDOWS)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32)
endif()

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/settings.hpp
    src/audio/audio-capture.cpp
    src/audio/audio-capture.hpp
    src/analysis/tempo-estimator.cpp
    src/analysis/tempo-estimator.hpp
    src/modulation/modulation-state.cpp
    src/modulation/modulation-state.hpp
    src/modulation/modulation-binding.cpp
    src/modulation/modulation-binding.hpp
    src/modulation/tempo-sync.cpp
    src/modulation/tempo-sync.hpp
    src/filters/custom-shader-filter.cpp
    src/filters/custom-shader-filter.hpp
    src/filters/glitch-filter.cpp
    src/filters/glitch-filter.hpp
    src/filters/glow-filter.cpp
    src/filters/glow-filter.hpp
    src/filters/mosaic-filter.cpp
    src/filters/mosaic-filter.hpp
    src/filters/pixelate-filter.cpp
    src/filters/pixelate-filter.hpp
    src/filters/rgb-split-filter.cpp
    src/filters/rgb-split-filter.hpp
    src/filters/shake-filter.cpp
    src/filters/shake-filter.hpp
    src/filters/tile-3d-filter.cpp
    src/filters/tile-3d-filter.hpp
    src/filters/wave-filter.cpp
    src/filters/wave-filter.hpp
    src/filters/zoom-punch-filter.cpp
    src/filters/zoom-punch-filter.hpp
    src/transport/art-protocol.cpp
    src/transport/art-protocol.hpp
    src/transport/browser-event-transport.cpp
    src/transport/browser-event-transport.hpp
    src/transport/websocket-server.cpp
    src/transport/websocket-server.hpp
    src/ui/settings-dialog.cpp
    src/ui/settings-dialog.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Ship licensing information with every installed plugin package.
target_add_resource(${CMAKE_PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
target_add_resource(${CMAKE_PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
