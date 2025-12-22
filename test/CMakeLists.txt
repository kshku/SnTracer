set(SRCS
    test.c
)

add_executable(sn_tracer_test)

target_sources(sn_tracer_test PRIVATE ${SRCS})

target_link_libraries(sn_tracer_test PRIVATE sntracer)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_target(copy_dlls ALL
        COMMENT "Copy the dlls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_RUNTIME_DLLS:sn_tracer_test>" "$<TARGET_FILE_DIR:sn_tracer_test>"
        COMMAND_EXPAND_LISTS
    )

    add_dependencies(copy_dlls sn_tracer_test)
endif()

add_executable(multithread multithread.c)
target_link_libraries(multithread PRIVATE sntracer)
