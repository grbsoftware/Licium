// licium_latent.cpp — Latent Phase C: the X browser (M5b).
//
// See licium_latent.h for the contract and docs\LATENT_DESIGN.md (blessed
// 2026-07-17) for the design. Standalone-only. The laws this file lives by:
//
//   * NOTHING IS EVER DELETED — verdicts, votes and tags are labels. The
//     glitch shelf (blowout/frozen/black) sorts last but stays browsable;
//     the two best visuals of the first haul were force-emitted rejects.
//   * VOTES ARE APPEND-ONLY, LATER-WINS — same file discipline as
//     palette_votes.txt. Votes bind to the roll ID (which IS the seed and
//     regenerates the .milk byte-for-byte, so the ID is as stable as the
//     colors were for palettes).
//   * DESCRIPTIONS ARE THE PRODUCT — Gary's words become the names of new
//     ingredients (the growth-path promotion loop). D reuses the engine's
//     waitstring; while it is active the browser's labels yield and only
//     Enter/Esc are intercepted (the old waitstring-collision lesson).
//   * MODAL MEANS MODAL — eats every key while open, auto-advance held
//     (plugin.cpp's m_bPresetLockedByCode includes LatentBrowserIsOpen).

#ifdef MILKDROP_STANDALONE

#include "api.h"
#include "licium_latent.h"
#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern CPlugin g_plugin;

// ================================ data model =================================

// Verdicts from Phase B (latent_verdicts.jsonl), later-wins per id. Sort rank:
// alive and latebloom lead, untriaged next, then the glitch shelf.
enum LatentVerdict { LV_NONE = 0, LV_ALIVE, LV_LATEBLOOM, LV_BLOWOUT, LV_FROZEN, LV_BLACK };

static int VerdictRank(int v)
{
    switch (v)
    {
    case LV_ALIVE: case LV_LATEBLOOM: return 0;
    case LV_NONE:                     return 1;
    case LV_BLOWOUT:                  return 2;
    case LV_FROZEN:                   return 3;
    default:                          return 4;   // LV_BLACK
    }
}

static const wchar_t* VerdictName(int v)
{
    switch (v)
    {
    case LV_ALIVE:     return L"alive";
    case LV_LATEBLOOM: return L"latebloom";
    case LV_BLOWOUT:   return L"blowout";
    case LV_FROZEN:    return L"frozen";
    case LV_BLACK:     return L"black";
    default:           return L"new";
    }
}

#define LATENT_ID_LEN   20    // "LX1-XXXXXXXX" + headroom for later genvers
#define LATENT_NTAGS    6
#define LATENT_NOTE_LEN 256

static const char* TAG_NAMES[LATENT_NTAGS] =
    { "organic", "geometric", "glitchy", "watery", "sparkly", "???" };

struct LatentRoll
{
    wchar_t       id[LATENT_ID_LEN];
    wchar_t       file[MAX_PATH];
    int           verdict;             // LatentVerdict
    int           vote;                // 1 like / 0 pass / -1 cleared / 999 none
    unsigned char tags[LATENT_NTAGS];
    int           hasNote;
};

static LatentRoll* g_rolls = NULL;
static int         g_count = 0, g_cap = 0;

// Browser state machine. DESCRIBE = the engine waitstring owns the keyboard.
enum { LB_CLOSED = 0, LB_LANDING, LB_BROWSE, LB_DESCRIBE };
static int g_state  = LB_CLOSED;
static int g_sel    = 0;
static int g_scroll = 0;

static wchar_t g_rollsDir[MAX_PATH]  = {0};
static wchar_t g_notesPath[MAX_PATH] = {0};

static void ResolvePaths(void)
{
    if (g_rollsDir[0]) return;
    // Same layout law as Bandit: rolls live in their own subfolder of the
    // engine's preset dir (findable from the normal preset list), state files
    // in the Licium\ root next to palette_votes.txt.
    swprintf(g_rollsDir, L"%slatent\\", g_plugin.m_szPresetDir);
    CreateDirectoryW(g_rollsDir, NULL);
    swprintf(g_notesPath, L"%slatent_notes.txt", g_plugin.m_szMilkdrop2Path);
}

extern "C" const wchar_t* LatentGetRollsDir(void)  { ResolvePaths(); return g_rollsDir; }
extern "C" const wchar_t* LatentGetNotesPath(void) { ResolvePaths(); return g_notesPath; }

static LatentRoll* FindRoll(const wchar_t* id)
{
    for (int i = 0; i < g_count; i++)
        if (!wcscmp(g_rolls[i].id, id)) return &g_rolls[i];
    return NULL;
}

static LatentRoll* FindRollA(const char* idA)
{
    wchar_t id[LATENT_ID_LEN];
    int i = 0;
    while (idA[i] && i < LATENT_ID_LEN - 1) { id[i] = (wchar_t)(unsigned char)idA[i]; i++; }
    id[i] = 0;
    return FindRoll(id);
}

// ---- loading the shelf -------------------------------------------------------

static void ScanRolls(void)
{
    g_count = 0;
    wchar_t pat[MAX_PATH];
    swprintf(pat, L"%slatent_*.milk", g_rollsDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        // latent_<ID>.milk -> ID (any LX genver; foreign lineages just browse)
        wchar_t id[LATENT_ID_LEN];
        size_t len = wcslen(fd.cFileName);
        if (len <= 7 + 5 || len - 12 >= LATENT_ID_LEN) continue;   // "latent_" + ".milk"
        wcsncpy_s(id, LATENT_ID_LEN, fd.cFileName + 7, len - 12);

        if (g_count == g_cap)
        {
            g_cap = g_cap ? g_cap * 2 : 128;
            LatentRoll* nr = (LatentRoll*)realloc(g_rolls, g_cap * sizeof(LatentRoll));
            if (!nr) break;
            g_rolls = nr;
        }
        LatentRoll* r = &g_rolls[g_count++];
        memset(r, 0, sizeof(*r));
        wcscpy_s(r->id, LATENT_ID_LEN, id);
        swprintf(r->file, L"%s%s", g_rollsDir, fd.cFileName);
        r->verdict = LV_NONE;
        r->vote    = 999;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void LoadVerdicts(void)
{
    wchar_t path[MAX_PATH];
    swprintf(path, L"%slatent_verdicts.jsonl", g_plugin.m_szMilkdrop2Path);
    FILE* f = _wfopen(path, L"rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char* pid = strstr(line, "\"id\":\"");
        char* pv  = strstr(line, "\"verdict\":\"");
        if (!pid || !pv) continue;
        pid += 6; pv += 11;
        char idA[LATENT_ID_LEN] = {0};
        for (int i = 0; i < LATENT_ID_LEN - 1 && pid[i] && pid[i] != '"'; i++) idA[i] = pid[i];
        LatentRoll* r = FindRollA(idA);
        if (!r) continue;
        if      (!strncmp(pv, "alive",     5)) r->verdict = LV_ALIVE;
        else if (!strncmp(pv, "latebloom", 9)) r->verdict = LV_LATEBLOOM;
        else if (!strncmp(pv, "blowout",   7)) r->verdict = LV_BLOWOUT;
        else if (!strncmp(pv, "frozen",    6)) r->verdict = LV_FROZEN;
        else if (!strncmp(pv, "black",     5)) r->verdict = LV_BLACK;
    }
    fclose(f);
}

static void LoadVotes(void)
{
    wchar_t path[MAX_PATH];
    swprintf(path, L"%slatent_votes.txt", g_plugin.m_szMilkdrop2Path);
    FILE* f = _wfopen(path, L"rb");
    if (!f) return;
    char idA[LATENT_ID_LEN]; int v;
    char line[128];
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "%19s %d", idA, &v) == 2)
        {
            LatentRoll* r = FindRollA(idA);
            if (r) r->vote = v;   // later lines win
        }
    fclose(f);
}

static void LoadTags(void)
{
    wchar_t path[MAX_PATH];
    swprintf(path, L"%slatent_tags.txt", g_plugin.m_szMilkdrop2Path);
    FILE* f = _wfopen(path, L"rb");
    if (!f) return;
    char idA[LATENT_ID_LEN], tag[32]; int v;
    char line[160];
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "%19s %31s %d", idA, tag, &v) == 3)
        {
            LatentRoll* r = FindRollA(idA);
            if (!r) continue;
            for (int t = 0; t < LATENT_NTAGS; t++)
                if (!strcmp(tag, TAG_NAMES[t])) { r->tags[t] = (unsigned char)(v ? 1 : 0); break; }
        }
    fclose(f);
}

// Mark rolls that have a description; optionally fetch the latest text for one
// id (later-wins) so re-describing starts from the existing words.
static void LoadNotes(const wchar_t* fetchId, wchar_t* fetchOut /*LATENT_NOTE_LEN*/)
{
    if (fetchOut) fetchOut[0] = 0;
    FILE* f = _wfopen(LatentGetNotesPath(), L"rb");
    if (!f) return;
    char line[LATENT_NOTE_LEN * 3 + 64];
    while (fgets(line, sizeof(line), f))
    {
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        LatentRoll* r = FindRollA(line);
        if (r) r->hasNote = 1;
        if (fetchId && fetchOut)
        {
            wchar_t idW[LATENT_ID_LEN];
            int i = 0;
            while (line[i] && i < LATENT_ID_LEN - 1) { idW[i] = (wchar_t)(unsigned char)line[i]; i++; }
            idW[i] = 0;
            if (!wcscmp(idW, fetchId))
            {
                char* text = tab + 1;
                char* nl = strpbrk(text, "\r\n");
                if (nl) *nl = 0;
                MultiByteToWideChar(CP_UTF8, 0, text, -1, fetchOut, LATENT_NOTE_LEN);
                fetchOut[LATENT_NOTE_LEN - 1] = 0;   // keep scanning: later lines win
            }
        }
    }
    fclose(f);
}

static int __cdecl RollCompare(const void* a, const void* b)
{
    const LatentRoll* ra = (const LatentRoll*)a;
    const LatentRoll* rb = (const LatentRoll*)b;
    int d = VerdictRank(ra->verdict) - VerdictRank(rb->verdict);
    if (d) return d;
    return wcscmp(ra->id, rb->id);
}

// ---- appending state ---------------------------------------------------------

static void AppendLineA(const wchar_t* fileName, const char* line)
{
    wchar_t path[MAX_PATH];
    swprintf(path, L"%s%s", g_plugin.m_szMilkdrop2Path, fileName);
    FILE* f = _wfopen(path, L"ab");
    if (f) { fputs(line, f); fclose(f); }
}

static void CastVote(LatentRoll* r, int vote)
{
    r->vote = vote;
    char line[64];
    sprintf(line, "%S %d\r\n", r->id, vote);
    AppendLineA(L"latent_votes.txt", line);
}

static void ToggleTag(LatentRoll* r, int t)
{
    r->tags[t] ^= 1;
    char line[96];
    sprintf(line, "%S %s %d\r\n", r->id, TAG_NAMES[t], r->tags[t]);
    AppendLineA(L"latent_tags.txt", line);
}

static void SaveNote(LatentRoll* r, const wchar_t* text)
{
    if (!text[0]) return;               // empty accept = no line (cancel-like)
    char utf8[LATENT_NOTE_LEN * 3];
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    char line[LATENT_NOTE_LEN * 3 + 32];
    sprintf(line, "%S\t%s\r\n", r->id, utf8);
    AppendLineA(L"latent_notes.txt", line);
    r->hasNote = 1;
}

// ================================ the browser ================================

static void LoadSelection(void)
{
    if (g_count > 0)
        g_plugin.LoadPreset(g_rolls[g_sel].file, 0.0f);   // hard cut: live browse
}

extern "C" void LatentBrowserOpen(void)
{
    if (g_state != LB_CLOSED) return;
    ResolvePaths();
    ScanRolls();
    LoadVerdicts();
    LoadVotes();
    LoadTags();
    LoadNotes(NULL, NULL);
    if (g_count > 1)
        qsort(g_rolls, g_count, sizeof(LatentRoll), RollCompare);
    g_sel = 0;
    g_scroll = 0;

    int seen = GetPrivateProfileIntW(L"settings", L"bLatentIntroSeen", 0,
                                     g_plugin.GetConfigIniFile());
    g_state = seen ? LB_BROWSE : LB_LANDING;
    if (g_state == LB_BROWSE)
        LoadSelection();
}

extern "C" void LatentBrowserClose(void)
{
    if (g_state == LB_DESCRIBE)
        g_plugin.m_waitstring.bActive = false;
    g_state = LB_CLOSED;
}

extern "C" int LatentBrowserIsOpen(void) { return g_state != LB_CLOSED; }

extern "C" void LatentResetIntro(void)
{
    WritePrivateProfileStringW(L"settings", L"bLatentIntroSeen", L"0",
                               g_plugin.GetConfigIniFile());
}

static void LandingDismissed(void)   // continue into the shelf
{
    g_state = LB_BROWSE;
    LoadSelection();
}

static void BeginDescribe(void)
{
    if (g_count == 0) return;
    LatentRoll* r = &g_rolls[g_sel];
    td_waitstr* ws = &g_plugin.m_waitstring;
    ws->bActive         = true;
    ws->bFilterBadChars = false;
    ws->bDisplayAsCode  = false;
    ws->bOvertypeMode   = false;
    ws->nSelAnchorPos   = -1;
    ws->nMaxLen         = LATENT_NOTE_LEN - 1;
    LoadNotes(r->id, ws->szText);       // re-describe starts from the last words
    ws->nCursorPos = (int)wcslen(ws->szText);
    swprintf(ws->szPrompt, L"Describe %s - what do you see? (Enter saves, Esc cancels)", r->id);
    ws->szToolTip[0] = 0;
    g_state = LB_DESCRIBE;
}

// Voting flows the cursor forward and loads the next roll live — one hand
// resting on Y/N, same law as the wall. The end of the list just stops.
static void AdvanceAndLoad(void)
{
    if (g_sel < g_count - 1)
    {
        g_sel++;
        LoadSelection();
    }
}

extern "C" int LatentBrowserHandleChar(int ch)
{
    switch (g_state)
    {
    case LB_CLOSED:
        return 0;

    case LB_DESCRIBE:
        return 0;   // the engine waitstring owns every char

    case LB_LANDING:
        switch (ch)
        {
        case 'n': case 'N':   // "don't show this page again"
            WritePrivateProfileStringW(L"settings", L"bLatentIntroSeen", L"1",
                                       g_plugin.GetConfigIniFile());
            LandingDismissed();
            return 1;
        case ' ':
            LandingDismissed();
            return 1;
        case 'x': case 'X':
            LatentBrowserClose();
            return 1;
        default:
            return 1;   // modal: eat every char
        }

    default:   // LB_BROWSE
        if (g_count == 0)
        {
            if (ch == 'x' || ch == 'X') LatentBrowserClose();
            return 1;
        }
        switch (ch)
        {
        case 'y': case 'Y': case ' ':
            CastVote(&g_rolls[g_sel], 1);
            AdvanceAndLoad();
            return 1;
        case 'n': case 'N':
            CastVote(&g_rolls[g_sel], 0);
            AdvanceAndLoad();
            return 1;
        case 'u': case 'U':
            CastVote(&g_rolls[g_sel], -1);
            return 1;
        case '1': case '2': case '3': case '4': case '5': case '6':
            ToggleTag(&g_rolls[g_sel], ch - '1');
            return 1;
        case 'd': case 'D':
            BeginDescribe();
            return 1;
        case 'x': case 'X':
            LatentBrowserClose();
            return 1;
        default:
            return 1;   // the browser eats every char while open
        }
    }
}

extern "C" int LatentBrowserHandleKeydown(int vk)
{
    switch (g_state)
    {
    case LB_CLOSED:
        return 0;

    case LB_DESCRIBE:
        // Only the two ending keys are ours; arrows/backspace/etc. fall
        // through (return 0) to the engine's waitstring editor.
        if (vk == VK_RETURN)
        {
            SaveNote(&g_rolls[g_sel], g_plugin.m_waitstring.szText);
            g_plugin.m_waitstring.bActive = false;
            g_state = LB_BROWSE;
            return 1;
        }
        if (vk == VK_ESCAPE)
        {
            g_plugin.m_waitstring.bActive = false;
            g_state = LB_BROWSE;
            return 1;
        }
        return 0;

    case LB_LANDING:
        if (vk == VK_RETURN) { LandingDismissed(); return 1; }
        if (vk == VK_ESCAPE) { LatentBrowserClose(); return 1; }
        return 1;   // modal: swallow everything else

    default:   // LB_BROWSE
        switch (vk)
        {
        case VK_UP:     if (g_sel > 0)           { g_sel--; LoadSelection(); } return 1;
        case VK_DOWN:   if (g_sel < g_count - 1) { g_sel++; LoadSelection(); } return 1;
        case VK_PRIOR:
            if (g_sel > 0) { g_sel -= 16; if (g_sel < 0) g_sel = 0; LoadSelection(); }
            return 1;
        case VK_NEXT:
            if (g_sel < g_count - 1)
            { g_sel += 16; if (g_sel > g_count - 1) g_sel = g_count - 1; LoadSelection(); }
            return 1;
        case VK_HOME:   if (g_sel != 0)           { g_sel = 0;           LoadSelection(); } return 1;
        case VK_END:    if (g_sel != g_count - 1 && g_count) { g_sel = g_count - 1; LoadSelection(); } return 1;
        case VK_RETURN: BeginDescribe(); return 1;
        case VK_ESCAPE: LatentBrowserClose(); return 1;
        default:
            return 1;   // modal: swallow every keydown (incl. Space's keydown half)
        }
    }
}

// ---- drawing ----------------------------------------------------------------
// Same quad idiom as the palette wall / DrawDarkTranslucentBox: SIMPLEVERTEX
// quads in client-centered coordinates; labels queue through m_text and flush
// after the UI pass, landing on top.

typedef struct { float x, y, z; DWORD Diffuse; } LatVertex;
#define LAT_VERTEX_FORMAT (D3DFVF_XYZ | D3DFVF_DIFFUSE)

static void LatRect(IDirect3DDevice9* dev, int w, int h,
                    float x0, float y0, float x1, float y1, DWORD color)
{
    LatVertex v[4];
    for (int i = 0; i < 4; i++)
    {
        v[i].x = -w * 0.5f + ((i % 2) ? x1 : x0);
        v[i].y =  h * 0.5f - ((i / 2) ? y0 : y1);
        v[i].z = 0.0f;
        v[i].Diffuse = color;
    }
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(LatVertex));
}

static void LatText(const wchar_t* s, int x0, int y0, int x1, int y1, DWORD color)
{
    RECT r = { x0, y0, x1, y1 };
    // (the text manager's signature is non-const; it only reads the string)
    g_plugin.m_text.DrawTextW(g_plugin.GetFont(SIMPLE_FONT), (wchar_t*)s, -1, &r,
                              DT_SINGLELINE | DT_NOPREFIX | DT_WORD_ELLIPSIS,
                              color, false, 0xFF000000);
}

// The window is the porthole canvas (a fixed square, often 512px) — every
// line here must fit ~60 chars at SIMPLE_FONT size or it clips at the edge
// (DT_WORD_ELLIPSIS doesn't truncate through m_text). Plain '-' only: this
// font has no em-dash glyph.
static void RenderLanding(IDirect3DDevice9* dev, int w, int h)
{
    LatRect(dev, w, h, 0, 0, (float)w, (float)h, 0xE6000000);

    int line = h / 24;
    int x0 = w / 16, x1 = w - w / 16;
    int y = h / 9;

    LatText(L"LATENT - the shader prospector's shelf",              x0, y, x1, y + line, 0xFFFFFFFF); y += line * 2;
    LatText(L"An offline prospector writes brand-new shader",       x0, y, x1, y + line, 0xFFCCCCCC); y += line;
    LatText(L"formulas nobody designed; an automated eye",          x0, y, x1, y + line, 0xFFCCCCCC); y += line;
    LatText(L"triages them. This shelf is the survivors, live.",    x0, y, x1, y + line, 0xFFCCCCCC); y += line;
    LatText(L"Your votes, tags and words feed the next runs -",     x0, y, x1, y + line, 0xFFCCCCCC); y += line;
    LatText(L"describing a roll names an ingredient.",              x0, y, x1, y + line, 0xFFCCCCCC); y += line;
    LatText(L"Nothing is ever deleted.",                            x0, y, x1, y + line, 0xFFCCCCCC); y += line * 2;

    LatText(L"Up/Down browse (loads live)   PgUp/PgDn jump",        x0, y, x1, y + line, 0xFFAACCEE); y += line;
    LatText(L"Y / Space like   N pass   U clear vote",              x0, y, x1, y + line, 0xFFAACCEE); y += line;
    LatText(L"1-6 tags: organic geometric glitchy",                 x0, y, x1, y + line, 0xFFAACCEE); y += line;
    LatText(L"          watery sparkly ???",                        x0, y, x1, y + line, 0xFFAACCEE); y += line;
    LatText(L"D / Enter describe   X / Esc close",                  x0, y, x1, y + line, 0xFFAACCEE); y += line * 2;

    LatText(L"Enter continues \x00b7 N = don't show this again",    x0, y, x1, y + line, 0xFFFFFFFF); y += line;
    LatText(L"Esc leaves",                                          x0, y, x1, y + line, 0xFFFFFFFF);
}

extern "C" void LatentBrowserRender(IDirect3DDevice9* dev, int w, int h)
{
    if (g_state == LB_CLOSED || g_state == LB_DESCRIBE || !dev || w <= 0 || h <= 0)
        return;   // describing: yield the screen to the engine's waitstring

    // State block mirrors DrawDarkTranslucentBox (restore at the end).
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetFVF(LAT_VERTEX_FORMAT);
    dev->SetTexture(0, NULL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    if (g_state == LB_LANDING)
    {
        RenderLanding(dev, w, h);
    }
    else
    {
        // Side panel over the live roll: the roll IS the content, so the list
        // takes the left edge only and the rest of the screen stays preset.
        float panelW = w * 0.40f;
        if (panelW < 300.0f) panelW = (float)((w < 300) ? w : 300);
        LatRect(dev, w, h, 0, 0, panelW, (float)h, 0xD8000000);
        LatRect(dev, w, h, panelW, 0, panelW + 2.0f, (float)h, 0xFF303030);

        // Header lines must fit the ~300px panel (~40 chars): the overflow
        // draws over the live roll, not ellipsized (see RenderLanding note).
        int line    = h / 30;
        if (line < 15) line = 15;
        int headerH = line * 4 + line / 2;
        int rowH    = line + 2;
        int x0 = 10, x1 = (int)panelW - 10;

        // header
        {
            wchar_t buf[160];
            int nAlive = 0, nNew = 0, nShelf = 0;
            for (int i = 0; i < g_count; i++)
            {
                int rk = VerdictRank(g_rolls[i].verdict);
                if      (rk == 0) nAlive++;
                else if (rk == 1) nNew++;
                else              nShelf++;
            }
            swprintf(buf, L"LATENT   %d/%d", g_count ? g_sel + 1 : 0, g_count);
            LatText(buf, x0, line / 2, x1, line / 2 + line, 0xFFFFFFFF);
            swprintf(buf, L"%d alive \x00b7 %d new \x00b7 %d shelf",
                     nAlive, nNew, nShelf);
            LatText(buf, x0, line / 2 + line, x1, line / 2 + line * 2, 0xFF9999AA);
            LatText(L"Y like  N pass  U clear  1-6 tag",
                    x0, line / 2 + line * 2, x1, line / 2 + line * 3, 0xFF9999AA);
            LatText(L"D describe  X close",
                    x0, line / 2 + line * 3, x1, line / 2 + line * 4, 0xFF9999AA);
        }

        if (g_count == 0)
        {
            LatText(L"No rolls yet - run tools\\latent_prospect, then latent_triage.ps1.",
                    x0, headerH + rowH, x1, headerH + rowH * 2, 0xFFCCCCCC);
        }
        else
        {
            int visible = (h - headerH - 4) / rowH;
            if (visible < 1) visible = 1;
            if (g_sel < g_scroll)               g_scroll = g_sel;
            if (g_sel >= g_scroll + visible)    g_scroll = g_sel - visible + 1;

            for (int row = 0; row < visible && g_scroll + row < g_count; row++)
            {
                LatentRoll* r = &g_rolls[g_scroll + row];
                int   idx = g_scroll + row;
                float ry0 = (float)(headerH + row * rowH);
                float ry1 = ry0 + rowH - 1;

                if (idx == g_sel)
                    LatRect(dev, w, h, 4.0f, ry0, panelW - 4.0f, ry1, 0xFF2A2A34);

                // vote strip at the row's left edge (green like / dark-red pass)
                if (r->vote == 1)      LatRect(dev, w, h, 4.0f, ry0, 8.0f, ry1, 0xFF30C060);
                else if (r->vote == 0) LatRect(dev, w, h, 4.0f, ry0, 8.0f, ry1, 0xFF702020);

                wchar_t tags[LATENT_NTAGS + 1];
                int nt = 0;
                for (int t = 0; t < LATENT_NTAGS; t++)
                    if (r->tags[t]) tags[nt++] = (wchar_t)(L'1' + t);
                tags[nt] = 0;

                wchar_t buf[96];
                swprintf(buf, L"%s  %s%s%s%s%s", r->id, VerdictName(r->verdict),
                         nt ? L"  #" : L"", tags,
                         r->hasNote ? L"  *" : L"", L"");

                DWORD col;
                int rk = VerdictRank(r->verdict);
                if      (idx == g_sel) col = 0xFFFFFFFF;
                else if (rk == 0)      col = 0xFFCCDDCC;
                else if (rk == 1)      col = 0xFF999999;
                else                   col = 0xFF776666;
                LatText(buf, x0 + 4, (int)ry0, x1, (int)ry1, col);
            }
        }
    }

    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
}

#endif // MILKDROP_STANDALONE
