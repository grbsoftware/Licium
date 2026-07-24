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
#define IDC_LS_NOHARVEST  1308
// grading schedule (Phase B): when to run the off-screen triage after the scan
#define IDC_LS_GRADE_NOW  1320   // radio: grade immediately
#define IDC_LS_GRADE_IDLE 1321   // radio: grade once the machine is idle
#define IDC_LS_GRADE_AT   1322   // radio: grade at a set clock time
#define IDC_LS_GRADE_TIME 1323   // edit: HH:MM for the "at" option

// Edit > Settings... dialog (licium_settings.cpp). First row is the global UI
// text scale; built to grow more settings later.
#define IDD_LICIUM_SETTINGS 131
#define IDC_SET_SCALE     1310   // trackbar, 75..300 (percent)
#define IDC_SET_SCALEVAL  1311   // "NNN%" readout
#define IDC_SET_WARN      1312   // occlusion warning text
#define IDC_SET_RESET     1313   // reset to default

// Tune cockpit - Color group (licium_latent.cpp). MODELESS floating dialog
// summoned with 'c' in the X browser: shape the SELECTED roll's color field
// (hue/sat/light/contrast) live over the moving visual. Each row = trackbar +
// editable number field (two-way). Values are a reversible per-roll overlay in
// Licium\latent_tunings.txt - the .milk is never overwritten. Ids in 134x.
#define IDD_TUNE_COLOR      132
#define IDC_TUNE_HUE      1340   // trackbar, -180..180 (degrees)
#define IDC_TUNE_HUEVAL   1341   // editable number
#define IDC_TUNE_SAT      1342   // trackbar, -100..100 (percent)
#define IDC_TUNE_SATVAL   1343
#define IDC_TUNE_LIGHT    1344   // trackbar, -100..100
#define IDC_TUNE_LIGHTVAL 1345
#define IDC_TUNE_CONTRAST 1346   // trackbar, -100..100
#define IDC_TUNE_CONVAL   1347
#define IDC_TUNE_RESET    1348   // snap all knobs back to "as rolled" (center)

#endif // LICIUM_RES_H
