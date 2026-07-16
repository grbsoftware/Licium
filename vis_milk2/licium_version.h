// licium_version.h
//
// The single source of truth for the Licium version. Included by BOTH the
// resource script (licium.rc -> VERSIONINFO, what Explorer shows on the exe)
// and the host code (standalone_host.cpp -> splash / About), so the number a
// user sees can never drift from the number stamped in the exe metadata.
//
// Scheme: 0.<milestone>.<patch> while pre-1.0 — the minor number tracks the
// port milestones (M2 multi-pass buffers -> 0.2.x). Bump PATCH for fixes
// inside a milestone, MINOR when a milestone lands, MAJOR at first release.

#ifndef LICIUM_VERSION_H
#define LICIUM_VERSION_H

#define LICIUM_VER_MAJOR 0
#define LICIUM_VER_MINOR 2
#define LICIUM_VER_PATCH 1

// Comma form for the VERSIONINFO FILEVERSION/PRODUCTVERSION binary fields.
#define LICIUM_VER_COMMA 0,2,1,0

#define LICIUM_VERSION_STR  "0.2.1"
#define LICIUM_VERSION_WSTR L"0.2.1"

#endif // LICIUM_VERSION_H
