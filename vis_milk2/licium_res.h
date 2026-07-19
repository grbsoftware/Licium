// licium_res.h
//
// Resource ids for the standalone-only licium.rc. Shared between the resource
// script and the host code (standalone_host.cpp) so the icon id has one home.
// Id 1 sorts ahead of the engine's IDI_PLUGIN_ICON (120) in the PE resource
// directory, which is what makes Explorer show the Licium icon on the exe.

#ifndef LICIUM_RES_H
#define LICIUM_RES_H

#define IDI_LICIUM 1

// Holly Watson's milk-splash wordmark, embedded as raw PNG data (RCDATA) and
// decoded with GDI+ at startup for the splash window.
#define IDR_LICIUM_SPLASH 2

// Latent > Run search... dialog (drives tools\latent_search.ps1 — the
// scan -> triage -> X pipeline — from the GUI). Ids from 130x to stay clear
// of the engine's plugin.rc dialogs (low ids) and the 0xE1xx menu range.
#define IDD_LATENT_SEARCH 130
#define IDC_LS_START      1301
#define IDC_LS_COUNT      1302
#define IDC_LS_EMIT       1303
#define IDC_LS_THREADS    1304
#define IDC_LS_SWARM      1305
#define IDC_LS_SEEK       1306
#define IDC_LS_REFRESH    1307

#endif // LICIUM_RES_H
