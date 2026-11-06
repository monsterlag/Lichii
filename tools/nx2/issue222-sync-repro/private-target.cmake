# Include once at the END of src/CMakeLists.txt in a disposable cloud checkout.
# This is deliberately not a production source change or CTest registration.
if(NOT TARGET core OR NOT TARGET video_core)
    message(FATAL_ERROR "Include this file after Lichii's normal source targets")
endif()
add_executable(issue222_sync_repro EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/repro.cpp")
target_link_libraries(issue222_sync_repro PRIVATE common core video_core Threads::Threads
                                                ${PLATFORM_LIBRARIES})
if(TARGET SDL3::SDL3)
    target_link_libraries(issue222_sync_repro PRIVATE SDL3::SDL3)
endif()
set_target_properties(issue222_sync_repro PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/issue222-private/$<CONFIG>")
