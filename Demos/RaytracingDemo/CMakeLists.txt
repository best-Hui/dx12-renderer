cmake_minimum_required(VERSION 3.8.0)

project("RaytracingDemo")

set(HEADER_FILES
        include/RaytracingDemo.h
        include/RenderGraph.User.h
        )

set(SOURCE_FILES
        src/main.cpp
        src/RaytracingDemo.cpp
        src/RenderGraph.User.cpp
        )

set(SHADER_FILES_RAYTRACING
        shaders/RaytracingDemo_RT.hlsl
        )

set(SHADERS_HEADER_FILES
        shaders/RaytracingDemo_RT_Types.hlsli
        shaders/RaytracingDemo_RT_Random.hlsli
        shaders/RaytracingDemo_RT_GBuffer.hlsli
        shaders/RaytracingDemo_RT_Lighting.hlsli
        )

set(SHADER_FILES_VERTEX
        shaders/RaytracingDemo_GBuffer_VS.hlsl
        shaders/RaytracingDemo_Skybox_VS.hlsl
        )

set(SHADER_FILES_PIXEL
        shaders/RaytracingDemo_GBuffer_PS.hlsl
        shaders/RaytracingDemo_Skybox_PS.hlsl
        )

set(TARGET_NAME RaytracingDemo)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/DemoBase.cmake)

target_sources(${TARGET_NAME} PRIVATE ${SHADER_FILES_RAYTRACING})
source_group("Resources\\Shaders" FILES ${SHADER_FILES_RAYTRACING})
set_source_files_properties(${SHADER_FILES_RAYTRACING} PROPERTIES HEADER_FILE_ONLY TRUE)

target_link_libraries(${TARGET_NAME}
        PUBLIC Framework
        PUBLIC RenderGraph
        )

file(GLOB DXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
list(SORT DXC_CANDIDATES)
list(GET DXC_CANDIDATES -1 DXC_EXECUTABLE)

set(RAYTRACING_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILES_RAYTRACING}")
set(RAYTRACING_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/RaytracingDemo_RT.cso")

add_custom_command(TARGET ${TARGET_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
        COMMAND "${DXC_EXECUTABLE}" -T lib_6_3 -Fo "${RAYTRACING_SHADER_OUTPUT}" "${RAYTRACING_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/Shaders" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders" -WX
        VERBATIM
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAYTRACING_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/RaytracingDemo_RT.cso"
        VERBATIM
)
