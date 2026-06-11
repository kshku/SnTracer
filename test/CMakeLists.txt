enable_testing()

set(SRCS
    test.c
)

add_executable(sn_tracer_test)
target_sources(sn_tracer_test PRIVATE ${SRCS})
target_link_libraries(sn_tracer_test PRIVATE sntracer)

add_test(NAME sn_tracer_test COMMAND sn_tracer_test)

add_executable(example_tracing example_tracing.c)
target_link_libraries(example_tracing PRIVATE sntracer)

add_test(NAME example_tracing COMMAND example_tracing)

add_executable(multithread multithread.c)
target_link_libraries(multithread PRIVATE sntracer)

add_test(NAME multithread COMMAND multithread)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND SN_TRACER_BUILD_SHARED)
    add_custom_target(copy_dlls ALL
        COMMENT "Copy the dlls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_RUNTIME_DLLS:sn_tracer_test>" "$<TARGET_FILE_DIR:sn_tracer_test>"
        COMMAND_EXPAND_LISTS
    )

    add_dependencies(copy_dlls sn_tracer_test multithread example_tracing)
endif()

