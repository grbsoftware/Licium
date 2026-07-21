// licium_settings.cpp — Edit > Settings... dialog (standalone-only).
//
// First (and for now only) setting: the global UI text scale, applied through
// CPluginShell::HostSetTextScalePct so every on-canvas overlay grows together.
// The dialog is modal on the render thread (same trade the menu bar and the
// Run-search dialog already make); the value is applied on OK and reverted on
// Cancel. Built so more rows can hang off it later.

#ifdef MILKDROP_STANDALONE

#include "api.h"
#include "licium_settings.h"
#include "licium_res.h"
#include "plugin.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

extern CPlugin g_plugin;

#define UI_SCALE_MIN 75
#define UI_SCALE_MAX 300
#define UI_SCALE_DEF 125

static int s_scaleAtOpen = UI_SCALE_DEF;   // for Cancel-reverts

static void SetScaleReadout(HWND dlg, int pct)
{
    wchar_t buf[16];
    swprintf(buf, L"%d%%", pct);
    SetDlgItemTextW(dlg, IDC_SET_SCALEVAL, buf);
}

// Apply a scale live and update the readout. Applying recreates the overlay
// fonts in place (see HostSetTextScalePct); cheap enough to do per trackbar tick.
static void ApplyScale(HWND dlg, int pct)
{
    if (pct < UI_SCALE_MIN) pct = UI_SCALE_MIN;
    if (pct > UI_SCALE_MAX) pct = UI_SCALE_MAX;
    g_plugin.HostSetTextScalePct(pct);
    SetScaleReadout(dlg, pct);
}

static INT_PTR CALLBACK SettingsDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);

        s_scaleAtOpen = g_plugin.HostGetTextScalePct();

        HWND tb = GetDlgItem(dlg, IDC_SET_SCALE);
        SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELONG(UI_SCALE_MIN, UI_SCALE_MAX));
        SendMessageW(tb, TBM_SETTICFREQ, 25, 0);
        SendMessageW(tb, TBM_SETPAGESIZE, 0, 25);
        SendMessageW(tb, TBM_SETPOS, TRUE, s_scaleAtOpen);
        SetScaleReadout(dlg, s_scaleAtOpen);
        return TRUE;
    }

    case WM_HSCROLL:
        if ((HWND)lp == GetDlgItem(dlg, IDC_SET_SCALE))
        {
            int pct = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
            ApplyScale(dlg, pct);   // live: harmless while the modal freezes the
                                    // render, correct the instant the dialog closes
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_SET_RESET:
            SendMessageW(GetDlgItem(dlg, IDC_SET_SCALE), TBM_SETPOS, TRUE, UI_SCALE_DEF);
            ApplyScale(dlg, UI_SCALE_DEF);
            return TRUE;
        case IDOK:
            // The current trackbar value is already applied + persisted.
            ApplyScale(dlg, (int)SendMessageW(GetDlgItem(dlg, IDC_SET_SCALE), TBM_GETPOS, 0, 0));
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            g_plugin.HostSetTextScalePct(s_scaleAtOpen);   // revert
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        g_plugin.HostSetTextScalePct(s_scaleAtOpen);       // [x] also reverts
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void LiciumSettingsOpen(HWND hParent)
{
    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_LICIUM_SETTINGS),
                    hParent, SettingsDlgProc, 0);
}

#endif // MILKDROP_STANDALONE
