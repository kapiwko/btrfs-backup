add_btrfsbackup_test(
    btrfsbackup-profile-schema-contract-tests
    profile_schema_contract_tests.cpp
    profile-schema-contract-tests
    btrfsbackup-config-model
)

target_compile_definitions(btrfsbackup-profile-schema-contract-tests
    PRIVATE BTRFSBACKUP_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
