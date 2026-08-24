add_library(btrfsbackup-application STATIC
    ../src/application/backup_service.cpp
    ../src/application/installation_render.cpp
    ../src/application/installation_service.cpp
    ../src/application/installation_validate.cpp
    ../src/application/profile_render.cpp
    ../src/application/profile_service.cpp
    ../src/application/profile_store.cpp
    ../src/application/profile_wizard.cpp
    ../src/application/profile_wizard_device.cpp
    ../src/application/profile_wizard_install.cpp
    ../src/application/profile_wizard_paths.cpp
    ../src/application/profile_wizard_prompt.cpp
    ../src/application/profile_wizard_sources.cpp
    ../src/application/runtime_adapters.cpp
    ../src/application/status_service.cpp
    ../src/application/target_service.cpp
)

target_compile_features(btrfsbackup-application PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-application
    PUBLIC
        ../include
)
target_link_libraries(btrfsbackup-application
    PUBLIC
        btrfsbackup-model
        btrfsbackup-system
        btrfsbackup-engine
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-application PRIVATE -Wall -Wextra -Wpedantic)
endif()
