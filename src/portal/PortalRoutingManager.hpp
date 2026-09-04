// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

class PortalRoutingManager {
public:
    struct Paths {
        QString configPath;
        QString statePath;
    };

    struct Result {
        bool ok = false;
        bool changed = false;
        QString message;
    };

    struct Status {
        bool ok = false;
        bool enabled = false;
        bool managed = false;
        QString backendList;
        QString message;
    };

    explicit PortalRoutingManager(Paths paths = defaultPaths());

    static Paths defaultPaths();

    [[nodiscard]] Status status() const;
    [[nodiscard]] Result enable();
    [[nodiscard]] Result disable();

private:
    Paths m_paths;
};
