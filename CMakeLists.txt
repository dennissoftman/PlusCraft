cmake_minimum_required(VERSION 3.28)
project(PlusCraft)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(DiligentCore)
add_subdirectory(DiligentFX)
add_subdirectory(DiligentTools)

add_executable(PlusCraft main.cpp)

target_compile_options(PlusCraft PRIVATE -DUNICODE -DENGINE_DLL)
target_compile_definitions(PlusCraft PRIVATE SDL_MAIN_HANDLED)

target_link_libraries(PlusCraft
    Diligent-Common
    Diligent-GraphicsTools
    Diligent-GraphicsEngineVk-shared
    Diligent-GraphicsEngineVkInterface
    Diligent-GraphicsEngineD3D11-shared
    Diligent-GraphicsEngineD3D11Interface
    Diligent-GraphicsEngineD3D12-shared
    Diligent-GraphicsEngineD3D12Interface
)
copy_required_dlls(PlusCraft)

find_package(spdlog REQUIRED)
target_link_libraries(PlusCraft spdlog::spdlog)

find_package(SDL2 REQUIRED)
target_link_libraries(PlusCraft SDL2::SDL2 SDL2::SDL2main)

find_package(glm REQUIRED)
target_link_libraries(PlusCraft glm::glm)
