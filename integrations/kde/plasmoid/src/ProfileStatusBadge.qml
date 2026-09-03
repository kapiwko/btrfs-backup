// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQml

QtObject {
    function icon(profileStatus) {
        if ((profileStatus?.lastError?.length ?? 0) > 0
                || !(profileStatus?.configurationValid ?? false)
                || (profileStatus?.run?.state ?? "") === "failed")
            return "emblem-error"
        if (!(profileStatus?.target?.connected ?? false))
            return "emblem-unavailable"
        if (profileStatus?.target?.spaceBelowMinimum ?? false)
            return "emblem-warning"
        if (profileStatus?.target?.mounted ?? false)
            return "emblem-success"
        if (profileStatus?.target?.unlocked ?? false)
            return "emblem-unlocked"
        return "emblem-locked"
    }
}
