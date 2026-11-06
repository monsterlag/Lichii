# Include once at the END of src/CMakeLists.txt in a disposable cloud checkout.
# This is deliberately not a CTest registration or an ordinary build target.
option(LICHII_ISSUE222_WORKER_REPRO "Build the private started-GPU-worker diagnostic" OFF)
if(NOT LICHII_ISSUE222_WORKER_REPRO)
    return()
endif()

if(NOT TARGET core OR NOT TARGET video_core OR NOT TARGET frontend_common)
    message(FATAL_ERROR "Include this file after Lichii's normal source targets")
endif()

# These includes add fixture construction and read-only metadata access only.
target_compile_definitions(core PRIVATE LICHII_ISSUE222_WORKER_REPRO)
target_compile_definitions(video_core PRIVATE LICHII_ISSUE222_WORKER_REPRO)

# Normal GUI, CLI and test executables each provide their own VMA implementation.
set(issue222_vma_source "${CMAKE_CURRENT_BINARY_DIR}/issue222_vma.cpp")
file(GENERATE OUTPUT "${issue222_vma_source}" CONTENT
    "#define VMA_IMPLEMENTATION\n#include \"video_core/vulkan_common/vma.h\"\n")
add_executable(issue222_sync_repro EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/repro.cpp" "${issue222_vma_source}")
if(NOT MSVC)
    # Match the existing executables' VMA allowances without changing repro warnings.
    set_source_files_properties("${issue222_vma_source}" PROPERTIES COMPILE_OPTIONS
        "-Wno-conversion;-Wno-unused-variable;-Wno-unused-parameter;-Wno-missing-field-initializers")
endif()
target_link_libraries(issue222_sync_repro PRIVATE common core frontend_common video_core Threads::Threads
                                                ${PLATFORM_LIBRARIES})
if(TARGET SDL3::SDL3)
    target_link_libraries(issue222_sync_repro PRIVATE SDL3::SDL3)
endif()
set_target_properties(issue222_sync_repro PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/issue222-private/$<CONFIG>")
