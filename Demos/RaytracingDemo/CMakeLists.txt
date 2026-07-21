cmake_minimum_required(VERSION 3.8.0)

project("RaytracingDemo")

set(HEADER_FILES
        include/RaytracingDemo.h
        )

set(SOURCE_FILES
        src/main.cpp
        src/RaytracingDemo.cpp
        )

set(TARGET_NAME RaytracingDemo)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/ProjectBase.cmake)
include(${CMAKE_SOURCE_DIR}/CMakeIncludes/DemoBase.cmake)

target_link_libraries(${TARGET_NAME}
        PUBLIC DXR
        )

file(GLOB DXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
list(SORT DXC_CANDIDATES)
list(GET DXC_CANDIDATES -1 DXC_EXECUTABLE)

set(RAYTRACING_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/RaytracingDemo_RT.hlsl")
set(RAYTRACING_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/RaytracingDemo_RT.cso")

add_custom_command(
        OUTPUT "${RAYTRACING_SHADER_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
        COMMAND "${DXC_EXECUTABLE}" -T lib_6_3 -E "" -Fo "${RAYTRACING_SHADER_OUTPUT}" "${RAYTRACING_SHADER_SOURCE}" -I "${CMAKE_SOURCE_DIR}/Shaders" -WX
        DEPENDS "${RAYTRACING_SHADER_SOURCE}"
        VERBATIM
)

add_custom_target(RaytracingDemo_RaytracingShaders DEPENDS "${RAYTRACING_SHADER_OUTPUT}")
add_dependencies(${TARGET_NAME} RaytracingDemo_RaytracingShaders)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RAYTRACING_SHADER_OUTPUT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/RaytracingDemo_RT.cso"
        VERBATIM
)
