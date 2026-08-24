add_library(btrfsbackup-model STATIC
    ../src/model/config_fingerprint.cpp
    ../src/model/identifiers.cpp
    ../src/model/json_io.cpp
    ../src/model/profile.cpp
    ../src/model/profile_wizard_model.cpp
    ../src/model/retention_plan.cpp
    ../src/model/snapshot_inventory.cpp
    ../src/model/validation.cpp
)

target_compile_features(btrfsbackup-model PUBLIC cxx_std_20)
target_include_directories(btrfsbackup-model
    PUBLIC
        ../include
)
target_link_libraries(btrfsbackup-model
    PUBLIC
        nlohmann_json::nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(btrfsbackup-model PRIVATE -Wall -Wextra -Wpedantic)
endif()
