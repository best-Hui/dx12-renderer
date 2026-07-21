cmake_minimum_required(VERSION 3.8.0)

project("DXR")

set(HEADER_FILES
        include/DXR/RaytracingPipeline.h
        include/DXR/RaytracingRenderer.h
        include/DXR/RaytracingScene.h
        include/DXR/RaytracingTypes.h
        include/DXR/ShaderTable.h
        )

set(SOURCE_FILES
        src/RaytracingPipeline.cpp
        src/RaytracingRenderer.cpp
        src/RaytracingScene.cpp
        src/ShaderTable.cpp
        )

set(TARGET_NAME DXR)
add_library(${TARGET_NAME} STATIC ${HEADER_FILES} ${SOURCE_FILES})

target_include_directories(${TARGET_NAME}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/DXR"
        )

if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
endif ()

target_link_libraries(${TARGET_NAME}
        PUBLIC Framework
        )
