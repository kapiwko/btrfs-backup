# SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(workflow "${SOURCE_DIR}/.github/workflows/release-gates.yml")
file(READ "${workflow}" contents)
file(READ "${SOURCE_DIR}/.github/workflows/tests.yml" tests_workflow)
file(READ "${SOURCE_DIR}/.github/workflows/codeql.yml" codeql_workflow)
file(READ "${SOURCE_DIR}/.github/workflows/systemd-security.yml" systemd_security_workflow)

function(require_text expected)
    string(FIND "${contents}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Release workflow is missing '${expected}'")
    endif()
endfunction()

function(require_order earlier later)
    string(FIND "${contents}" "${earlier}" earlier_position)
    string(FIND "${contents}" "${later}" later_position)
    if(earlier_position EQUAL -1 OR later_position EQUAL -1)
        message(FATAL_ERROR "Cannot compare missing release workflow steps")
    endif()
    if(NOT earlier_position LESS later_position)
        message(FATAL_ERROR "Release workflow must run '${earlier}' before '${later}'")
    endif()
endfunction()

function(extract_job job next_job output)
    string(FIND "${contents}" "\n  ${job}:\n" start)
    string(FIND "${contents}" "\n  ${next_job}:\n" finish)
    if(start EQUAL -1 OR finish EQUAL -1 OR NOT start LESS finish)
        message(FATAL_ERROR "Cannot isolate release workflow job '${job}'")
    endif()
    math(EXPR length "${finish} - ${start}")
    string(SUBSTRING "${contents}" ${start} ${length} job_contents)
    set(${output} "${job_contents}" PARENT_SCOPE)
endfunction()

function(require_job_text job_contents expected job)
    string(FIND "${${job_contents}}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Release workflow job '${job}' is missing '${expected}'")
    endif()
endfunction()

function(reject_job_text job_contents rejected job)
    string(FIND "${${job_contents}}" "${rejected}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "Release workflow job '${job}' must not contain '${rejected}'")
    endif()
endfunction()

function(require_job_order job_contents earlier later job)
    string(FIND "${${job_contents}}" "${earlier}" earlier_position)
    string(FIND "${${job_contents}}" "${later}" later_position)
    if(earlier_position EQUAL -1 OR later_position EQUAL -1)
        message(FATAL_ERROR "Cannot compare missing steps in release workflow job '${job}'")
    endif()
    if(NOT earlier_position LESS later_position)
        message(FATAL_ERROR "Release workflow job '${job}' must run '${earlier}' before '${later}'")
    endif()
endfunction()

function(require_workflow_text workflow_contents expected workflow_name)
    string(FIND "${${workflow_contents}}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Workflow '${workflow_name}' is missing required check definition '${expected}'")
    endif()
endfunction()

extract_job("required-ci" "packaging" required_ci_job)
extract_job("packaging" "real-btrfs" packaging_job)
extract_job("real-btrfs" "qemu" real_btrfs_job)
extract_job("qemu" "publish" qemu_job)

require_job_text(required_ci_job "needs: release-context" "required-ci")
require_job_text(required_ci_job "Require successful commit checks" "required-ci")
require_job_text(required_ci_job "repos/\${GITHUB_REPOSITORY}/commits/\${GITHUB_SHA}/check-runs" "required-ci")
require_job_text(required_ci_job "-f filter=latest" "required-ci")
require_job_text(required_ci_job "if [[ \"\${status}\" == \"completed\" && \"\${conclusion}\" == \"success\" ]]" "required-ci")
require_job_text(required_ci_job "Timed out waiting for required commit checks" "required-ci")

foreach(required_check IN ITEMS
        clang-format
        clang-tidy
        "GCC tests"
        "Clang tests"
        "GCC (manager disabled) tests"
        "Clang (manager disabled) tests"
        architecture-tests
        kde-dbus-contract
        sanitizers
        fuzz-smoke
        strict-warnings
        "Analyze C++"
        analyze-service)
    require_job_text(required_ci_job "\"${required_check}\"" "required-ci")
endforeach()

foreach(tests_check IN ITEMS
        clang-format
        clang-tidy
        architecture-tests
        kde-dbus-contract
        sanitizers
        fuzz-smoke
        strict-warnings)
    require_workflow_text(tests_workflow "\n  ${tests_check}:\n" "tests")
endforeach()
require_workflow_text(tests_workflow "name: \${{ matrix.compiler }} tests" "tests")
foreach(compiler IN ITEMS
        GCC
        Clang
        "GCC (manager disabled)"
        "Clang (manager disabled)")
    require_workflow_text(tests_workflow "compiler: ${compiler}" "tests")
endforeach()
require_workflow_text(codeql_workflow "name: Analyze C++" "CodeQL")
require_workflow_text(systemd_security_workflow "\n  analyze-service:\n" "systemd security")

require_job_text(packaging_job "needs: required-ci" "packaging")

require_job_text(real_btrfs_job "needs: packaging" "real-btrfs")
require_job_text(real_btrfs_job "Download preserved release artifacts" "real-btrfs")
require_job_text(real_btrfs_job "name: release-artifacts-\${{ github.sha }}" "real-btrfs")
require_job_text(real_btrfs_job "sha256sum --check SHA256SUMS" "real-btrfs")
require_job_text(real_btrfs_job "PACKAGE_DIR=\"\${GITHUB_WORKSPACE}/build/release-artifacts\"" "real-btrfs")
require_job_text(real_btrfs_job "--target real-btrfs-integration" "real-btrfs")
reject_job_text(real_btrfs_job "PACKAGE_BUILDER=docker" "real-btrfs")
require_job_order(real_btrfs_job "Download preserved release artifacts" "Verify preserved release artifacts" "real-btrfs")
require_job_order(real_btrfs_job "Verify preserved release artifacts" "Run real Btrfs gate" "real-btrfs")

require_job_text(qemu_job "needs: packaging" "qemu")
require_job_text(qemu_job "Download preserved release artifacts" "qemu")
require_job_text(qemu_job "name: release-artifacts-\${{ github.sha }}" "qemu")
require_job_text(qemu_job "sha256sum --check SHA256SUMS" "qemu")
require_job_text(qemu_job "PACKAGE_DIR=\"\${GITHUB_WORKSPACE}/build/release-artifacts\"" "qemu")
require_job_text(qemu_job "--target qemu-hotplug-integration" "qemu")
reject_job_text(qemu_job "PACKAGE_BUILDER=docker" "qemu")
require_job_order(qemu_job "Download preserved release artifacts" "Verify preserved release artifacts" "qemu")
require_job_order(qemu_job "Verify preserved release artifacts" "Run QEMU provisioning and hotplug gate" "qemu")

require_text("tags:")
require_text("- \"v*.*.*\"")
require_text("name: release-artifacts-\${{ github.sha }}")
require_text("path: build/release-artifacts/")
require_text("retention-days: 90")
require_text("checks: read")
require_text("required-ci:")
require_text("- packaging")
require_text("- real-btrfs")
require_text("- qemu")
require_text("needs:\n      - packaging\n      - real-btrfs\n      - qemu")
require_text("Validate signed annotated release tag")
require_text(".verification.verified")
require_text("actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a")
require_text("actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c")
require_text("actions/attest-build-provenance@4d101475d8b20a2381f78447822ac1eab6504dd8")
require_text("gh release create")
require_text("--draft")
require_text("Verify published release assets")
require_text("sha256sum --check ../release-artifacts/SHA256SUMS")
require_text("gh release edit \"\${GITHUB_REF_NAME}\" --draft=false")

require_order("Verify release checksums" "Preserve verified release artifacts")
require_order("Download gated release artifacts" "Verify downloaded release artifacts")
require_order("Verify downloaded release artifacts" "Attest gated release artifacts")
require_order("Attest gated release artifacts" "Create draft GitHub release from gated artifacts")
require_order("Create draft GitHub release from gated artifacts" "Verify published release assets")
require_order("Verify published release assets" "Publish verified GitHub release")
