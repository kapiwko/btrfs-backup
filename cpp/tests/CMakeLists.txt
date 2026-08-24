function(add_btrfsbackup_test target source test_name library)
    add_executable(${target}
        ${source}
    )
    target_include_directories(${target}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/cpp/tests
    )
    target_link_libraries(${target}
        PRIVATE
            ${library}
    )
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

add_subdirectory(model)
add_subdirectory(system)
add_subdirectory(engine)
add_subdirectory(application)
add_subdirectory(cli)
add_subdirectory(integration)
