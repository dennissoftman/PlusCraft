cmake_minimum_required(VERSION 3.28)
project(PlusCraft)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(DiligentCore)
add_subdirectory(DiligentFX)
add_subdirectory(DiligentTools)

add_executable(PlusCraft main.cpp)

target_link_libraries(PlusCraft
    Diligent-GraphicsEngineOpenGL-shared
    Diligent-GraphicsEngineVk-shared
)

find_package(spdlog REQUIRED)
target_link_libraries(PlusCraft spdlog::spdlog)

find_package(SDL2 REQUIRED)
target_link_libraries(PlusCraft SDL2::SDL2 SDL2::SDL2main)

find_package(glm REQUIRED)
target_link_libraries(PlusCraft glm::glm)
