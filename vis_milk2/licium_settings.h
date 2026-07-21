// licium_settings.h — Licium's Settings surface (Edit > Settings...).
//
// Standalone-only. The first setting is the global UI text scale (all on-canvas
// overlay fonts); the dialog is built to grow more rows later. The font-scale
// mechanism itself lives in the shell (CPluginShell::HostSetTextScalePct etc.)
// because it needs the private font objects; this header owns the dialog + the
// automation message.

#ifndef LICIUM_SETTINGS_H
#define LICIUM_SETTINGS_H

#ifdef MILKDROP_STANDALONE

#include <windows.h>

// Automation / cross-thread apply: post to the main window, wParam = scale
// percent (75..300). Mirrors WM_LICIUM_BANDIT_ROLL (0x51) / WALL (0x52) /
// LATENT (0x53). Handled in plugin.cpp -> HostSetTextScalePct(wParam).
#define WM_LICIUM_TEXTSCALE (WM_APP + 0x54)

// Open the modal Settings dialog (Edit > Settings...). Defined in
// licium_settings.cpp; called from the host menu handler.
void LiciumSettingsOpen(HWND hParent);

#endif // MILKDROP_STANDALONE
#endif // LICIUM_SETTINGS_H
