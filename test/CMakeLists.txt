set(SRCS
    test.c
)

add_executable(test)

target_sources(test PRIVATE ${SRCS})

target_link_libraries(test PRIVATE sntracer)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_target(copy_dlls ALL
        COMMENT "Copy the dlls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_RUNTIME_DLLS:test>" "$<TARGET_FILE_DIR:test>"
        COMMAND_EXPAND_LISTS
    )

    add_dependencies(copy_dlls test)
endif()
