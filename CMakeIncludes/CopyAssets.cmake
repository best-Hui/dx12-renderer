# Modify Begin:2026-07-21 by BestHui
# Copy shared runtime assets once to avoid parallel post-build copy races.
if (NOT TARGET CopyRuntimeAssets)
        add_custom_target(CopyRuntimeAssets
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/Assets"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Assets/" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/Assets"
                VERBATIM
                )
endif()

add_dependencies(${TARGET_NAME} CopyRuntimeAssets)
# Modify End
