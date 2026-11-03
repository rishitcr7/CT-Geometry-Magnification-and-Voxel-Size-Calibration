cmake_minimum_required(VERSION 3.14)
project(ctgeom VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Header-only core library: link against ctgeom::ctgeom to get the include path.
add_library(ctgeom INTERFACE)
add_library(ctgeom::ctgeom ALIAS ctgeom)
target_include_directories(ctgeom INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(ctgeom INTERFACE cxx_std_17)

add_executable(ctgeom_cli src/cli.cpp)
target_link_libraries(ctgeom_cli PRIVATE ctgeom::ctgeom)

add_executable(sensitivity_report src/sensitivity_report.cpp)
target_link_libraries(sensitivity_report PRIVATE ctgeom::ctgeom)

# Tests (run with: ctest --output-on-failure). They read data/ by relative path,
# so they run from the source directory.
enable_testing()
add_executable(test_geometry tests/test_geometry.cpp)
target_link_libraries(test_geometry PRIVATE ctgeom::ctgeom)
add_executable(test_calibration tests/test_calibration.cpp)
target_link_libraries(test_calibration PRIVATE ctgeom::ctgeom)
add_executable(test_image_edge tests/test_image_edge.cpp)
target_link_libraries(test_image_edge PRIVATE ctgeom::ctgeom)
add_test(NAME geometry    COMMAND test_geometry    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME calibration COMMAND test_calibration WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME image_edge  COMMAND test_image_edge  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Optional Python module (needs pybind11): cmake -DCTGEOM_BUILD_PYTHON=ON ..
option(CTGEOM_BUILD_PYTHON "Build the pybind11 Python module" OFF)
if(CTGEOM_BUILD_PYTHON)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(ctgeom_py python/bindings.cpp)
  target_link_libraries(ctgeom_py PRIVATE ctgeom::ctgeom)
  set_target_properties(ctgeom_py PROPERTIES OUTPUT_NAME ctgeom)
endif()

install(DIRECTORY include/ctgeom DESTINATION include)
