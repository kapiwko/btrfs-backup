// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <sys/stat.h>

#include <state/document/BoundedDocumentReader.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

using btrfsbackup::state::document::BoundedDocumentReader;

void test_reads_regular_document_within_limit() {
    const fs::path root = test_helpers::test_root("bounded-document-reader", "valid");
    const fs::path path = root / "document.json";
    test_helpers::write_file(path, "document");

    test_helpers::expect_eq("bounded read", BoundedDocumentReader{}.read(path, 8), "document");
    fs::remove_all(root);
}

void test_rejects_document_over_limit() {
    const fs::path root = test_helpers::test_root("bounded-document-reader", "oversized");
    const fs::path path = root / "document.json";
    test_helpers::write_file(path, "oversized");

    test_helpers::expect_validation_error(
        "oversized document",
        [&] { (void)BoundedDocumentReader{}.read(path, 8); },
        "exceeds the size limit"
    );
    fs::remove_all(root);
}

void test_rejects_symbolic_link() {
    const fs::path root = test_helpers::test_root("bounded-document-reader", "symlink");
    const fs::path target = root / "target.json";
    const fs::path link = root / "document.json";
    test_helpers::write_file(target, "document");
    fs::create_symlink(target, link);

    test_helpers::expect_validation_error(
        "symbolic link",
        [&] { (void)BoundedDocumentReader{}.read(link, 1024); },
        "cannot read document"
    );
    fs::remove_all(root);
}

void test_rejects_non_regular_file() {
    const fs::path root = test_helpers::test_root("bounded-document-reader", "directory");

    test_helpers::expect_validation_error(
        "non-regular document",
        [&] { (void)BoundedDocumentReader{}.read(root, 1024); },
        "not a regular file"
    );
    fs::remove_all(root);
}

void test_rejects_document_writable_by_others() {
    const fs::path root = test_helpers::test_root("bounded-document-reader", "permissions");
    const fs::path path = root / "document.json";
    test_helpers::write_file(path, "document");
    chmod(path.c_str(), 0666);

    test_helpers::expect_validation_error(
        "unsafe document permissions",
        [&] { (void)BoundedDocumentReader{}.read(path, 1024); },
        "writable by group or others"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_reads_regular_document_within_limit();
    test_rejects_document_over_limit();
    test_rejects_symbolic_link();
    test_rejects_non_regular_file();
    test_rejects_document_writable_by_others();
    return test_helpers::finish("bounded document reader tests");
}
