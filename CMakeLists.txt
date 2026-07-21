cmake_minimum_required(VERSION 3.8.0)

# Use solution folders to organize projects
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Make sure DLL and EXE targets go to the same directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Output directory for static lib (.LIB)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for shared lib (.DLL)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for executables (.EXE)

cmake_policy(SET CMP0079 NEW)

project(LearningDirectX12 LANGUAGES CXX)

# Modify Begin:2026-07-21 by BestHui
option(DX12_RENDERER_ENABLE_SM610_LINALG "Build RaytracingDemo CHS with the SM6.10 LinAlg preview path." ON)
# Modify End

# Libraries
add_subdirectory(DX12Library)
# Modify Begin:2026-07-21 by BestHui
# Ray tracing framework code is built through Framework instead of a standalone DXR target.
# Modify End
add_subdirectory(Framework)
add_subdirectory(RenderGraph)

# Demos
add_subdirectory(Demos/AnimationsDemo)
add_subdirectory(Demos/DeferredLightingDemo)
add_subdirectory(Demos/LightingDemo)
add_subdirectory(Demos/ToonDemo)
add_subdirectory(Demos/GrassDemo)
add_subdirectory(Demos/RenderGraphDemo)
add_subdirectory(Demos/MeshletsDemo)
add_subdirectory(Demos/RaytracingDemo)

# Set the startup project.
set_directory_properties(PROPERTIES
        VS_STARTUP_PROJECT LightingDemo
        )
