cmake_minimum_required(VERSION 3.28)
project(obs-smooth-zoom VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OBS_BUILD_DIR "" CACHE PATH "Directory containing OBS libobs and frontend CMake packages")
if(OBS_BUILD_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${OBS_BUILD_DIR}")
endif()

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(${PROJECT_NAME} MODULE src/plugin-main.cpp src/zoom_math.hpp)
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W3)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" OUTPUT_NAME "obs-smooth-zoom")

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit")
install(FILES data/zoom.effect DESTINATION "data/obs-plugins/obs-smooth-zoom")

option(BUILD_ZOOM_MATH_TESTS "Build dependency-free zoom geometry unit tests" OFF)
if(BUILD_ZOOM_MATH_TESTS)
    add_executable(zoom_math_test tests/zoom_math_test.cpp)
    enable_testing()
    add_test(NAME zoom_math COMMAND zoom_math_test)
endif()
