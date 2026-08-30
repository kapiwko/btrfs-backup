// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/CtlTool.hpp>

int main(int argc, char** argv) {
    return btrfsbackup::cli::ctl_tool_main(argc, argv);
}
