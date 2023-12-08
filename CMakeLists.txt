cmake_minimum_required(VERSION 3.8)

if(DEFINED VCPKG_INSTALLED_DIR)
    if(WIN32 AND MSVC)
        cmake_policy(SET CMP0091 NEW)
        set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "target triplet" FORCE)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

project(
    roguely
    VERSION 1.0.0
    DESCRIPTION "A simple Roguelike in C++/Lua/SDL2"
    HOMEPAGE_URL "https://github.com/cculianu/Roguely"
    LANGUAGES CXX
)

# Set the C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Lua REQUIRED)
find_package(sol2 QUIET)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SDL2_mixer REQUIRED)
set(Sources main.cpp engine.cpp engine.h)

add_executable(roguely ${Sources})
target_compile_features(roguely PUBLIC cxx_std_20)

# Set includes
target_include_directories(roguely PUBLIC ${LUA_INCLUDE_DIR})
# If sol2 not found, fall-back to our sol2 headers included in this repo
if(NOT sol2_FOUND)
    set(sol2inc ${CMAKE_CURRENT_SOURCE_DIR}/libs/sol2/include)
    message("sol2 not found on system, using local fallback header-only includes: ${sol2inc}")
    target_include_directories(roguely PUBLIC ${sol2inc})
endif()

# Windows requires these libs be linked-in
if(WIN32)
    set(winlibs winmm.lib bcrypt.lib)
else()
    set(winlibs)
endif()

# Set verbose warnings
if(MSVC)
  target_compile_options(roguely PRIVATE /W4)
else(GCC OR CLANG)
  target_compile_options(roguely PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(roguely
    PUBLIC
    ${winlibs}
    ${LUA_LIBRARIES}
    SDL2::SDL2 SDL2::SDL2main SDL2::SDL2-static
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/roguely.lua DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
