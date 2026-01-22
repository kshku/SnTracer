if(SN_TRACER_BUILD_SHARED)
    add_library(sntracer SHARED)
    target_compile_definitions(sntracer PRIVATE SN_EXPORT)
else()
    add_library(sntracer STATIC)
    target_compile_definitions(sntracer PUBLIC SN_TRACER_STATIC)
endif()

target_include_directories(sntracer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(sntracer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(sntracer PRIVATE sn_tracer_configs)

add_subdirectory(src)
