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

// Single-glyph verdict for the compact row (the narrow panel has no room for
// the full word). A alive / L latebloom / B blowout / F frozen / K black.
static const wchar_t* VerdictShort(int v)
{
    switch (v)
    {
    case LV_ALIVE:     return L"A";
    case LV_LATEBLOOM: return L"L";
    case LV_BLOWOUT:   return L"B";
    case LV_FROZEN:    return L"F";
    case LV_BLACK:     return L"K";
    default:           return L"-";
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
    // Voted rolls sink below unvoted ones, likes at the very bottom (Gary,
    // 2026-07-18: otherwise reaching the votable rolls means skipping over
    // hundreds of already-rated ones). Sort is applied at open only, so a
    // vote never moves a roll out from under the cursor mid-session.
    int ga = (ra->vote == 1) ? 2 : (ra->vote == 0) ? 1 : 0;
    int gb = (rb->vote == 1) ? 2 : (rb->vote == 0) ? 1 : 0;
    if (ga != gb) return ga - gb;
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
        // hugs the left edge and the rest of the screen stays preset. Gary's
        // ask (2026-07-21) — the old 300px min was a slab over the 512 canvas
        // (~58%). SIMPLE_FONT is ~fixed-pixel (not resolution-scaled), so size
        // to a fixed width that just clears the widest line (~180px of compact
        // rows / legend) rather than a %-of-width that overflows when narrow.
        float panelW = 200.0f;
        if (panelW > w - 20.0f) panelW = (float)((w > 40) ? (w - 20) : w);
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
            swprintf(buf, L"%d lv \x00b7 %d nw \x00b7 %d sh",
                     nAlive, nNew, nShelf);
            LatText(buf, x0, line / 2 + line, x1, line / 2 + line * 2, 0xFF9999AA);
            LatText(L"Y like  N pass  U clr",
                    x0, line / 2 + line * 2, x1, line / 2 + line * 3, 0xFF9999AA);
            LatText(L"1-6 tag  D descr  X",
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
                swprintf(buf, L"%s %s%s%s%s", r->id, VerdictShort(r->verdict),
                         nt ? L" #" : L"", tags,
                         r->hasNote ? L" *" : L"");

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

// ========================= Latent > Run search... ============================
// GUI front end for tools\latent_search.ps1 (scan -> triage -> X refresh).
// The dialog collects the script's parameters and launches it DETACHED so the
// search outlives this process; the script's final phase posts WM_LICIUM_LATENT
// back at the main window, which is how X ends up holding the fresh haul.

#include "licium_res.h"

// The repo root is two levels above the exe (vis_milk2\Release\Licium.exe) —
// vendored-tree layout law. Returns 0 if the script isn't found there.
static int ResolveSearchScript(wchar_t* out /*MAX_PATH*/)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    for (int hop = 0; hop < 3; hop++)   // strip Licium.exe, Release, vis_milk2
    {
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (!slash) return 0;
        *slash = 0;
    }
    swprintf(out, L"%s\\tools\\latent_search.ps1", exe);
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

// Pre-fill for the Start field: one past the highest seed any prior search
// consumed (the script appends every run's range to latent_search_ranges.txt
// and refuses overlaps — this makes the default always pass that guard).
static unsigned long long NextFreeStart(void)
{
    wchar_t path[MAX_PATH];
    swprintf(path, L"%slatent_search_ranges.txt", g_plugin.m_szMilkdrop2Path);
    unsigned long long maxEnd = 0;
    FILE* f = _wfopen(path, L"rt");
    if (f)
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            unsigned long long a = 0, b = 0;
            if (sscanf(line, "%llu %llu", &a, &b) == 2 && b > maxEnd) maxEnd = b;
        }
        fclose(f);
    }
    return maxEnd;
}

// Progress state, fed by SearchPollLog() below and drawn by the overlay.
enum { LS_IDLE = 0, LS_SCAN, LS_TRIAGE, LS_OPENING, LS_DONE, LS_FAILED };
static HANDLE  g_searchProc = NULL;
static int     g_searchEmit = 0;
static wchar_t g_searchLogPath[MAX_PATH] = {0};
static int     g_searchPhase   = LS_IDLE;
static float   g_searchFrac    = 0.0f;
static wchar_t g_searchText[80]  = {0};
static wchar_t g_searchTitle[96] = {0};
static DWORD   g_searchEndTick = 0;
static int     g_searchPoll    = 0;

// Glide state. The prospector flushes a scan tick only every ~256K seeds, so
// the measured fraction arrives in big steps seconds apart. The bar advances
// between ticks at the rate the last two ticks implied, capped so it can never
// outrun the tick it is predicting — display-side extrapolation, nothing about
// the search itself is smoothed or estimated away.
static float   g_searchShown    = 0.0f;   // what the bar draws
static float   g_searchPrevFrac = 0.0f;   // measured value before the last tick
static DWORD   g_searchTickTime = 0;      // when the last tick was parsed
static float   g_searchRate     = 0.0f;   // fraction per ms, EMA over ticks
static DWORD   g_searchFrameTick = 0;
static int     g_searchScanTicked = 0;    // a real scan tick has been parsed

static int LaunchSearch(unsigned long long start, unsigned long long count,
                        int emit, int threads, int swarm, int seek, int refresh,
                        int noHarvest)
{
    wchar_t script[MAX_PATH];
    if (!ResolveSearchScript(script)) return 0;

    wchar_t logPath[MAX_PATH];
    swprintf(logPath, L"%slatent_search_last.log", g_plugin.m_szMilkdrop2Path);
    // Fresh log per run: the overlay parses it for progress, and stale lines
    // from a previous run would read as instant completion.
    DeleteFileW(logPath);

    // -Command (not -File) so the run logs somewhere the user can check when a
    // search seems to vanish (e.g. the script's range-overlap guard threw).
    wchar_t cmd[2048];
    swprintf(cmd, L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
                  L"\"& '%s' -Start %llu -Count %llu -Emit %d -Threads %d%s%s%s%s *>> '%s'\"",
             script, start, count, emit, threads,
             swarm     ? L" -Swarm"        : L"",
             seek      ? L" -Seek threads" : L"",
             refresh   ? L""               : L" -NoRefresh",
             noHarvest ? L" -NoHarvest"    : L"",
             logPath);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                        NULL, NULL, &si, &pi))
        return 0;
    CloseHandle(pi.hThread);

    // Keep the process handle: the overlay uses it to tell "still running with
    // a quiet log" apart from "died without writing the completion line".
    if (g_searchProc) CloseHandle(g_searchProc);
    g_searchProc  = pi.hProcess;
    g_searchEmit  = emit;
    wcsncpy_s(g_searchLogPath, MAX_PATH, logPath, _TRUNCATE);
    g_searchPhase = LS_SCAN;
    g_searchFrac  = 0.0f;
    wcscpy_s(g_searchText, L"search: starting");
    g_searchEndTick = 0;
    g_searchShown = g_searchPrevFrac = g_searchRate = 0.0f;
    g_searchTickTime = g_searchFrameTick = 0;
    g_searchScanTicked = 0;
    return 1;
}

static INT_PTR CALLBACK SearchDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        wchar_t buf[32];
        swprintf(buf, L"%llu", NextFreeStart());
        SetDlgItemTextW(dlg, IDC_LS_START,   buf);
        SetDlgItemTextW(dlg, IDC_LS_COUNT,   L"1000000");
        SetDlgItemTextW(dlg, IDC_LS_EMIT,    L"40");
        SetDlgItemTextW(dlg, IDC_LS_THREADS, L"10");
        CheckDlgButton(dlg, IDC_LS_REFRESH, BST_CHECKED);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDOK:
        {
            wchar_t buf[32];
            GetDlgItemTextW(dlg, IDC_LS_START, buf, 32);
            unsigned long long start = _wcstoui64(buf, NULL, 10);
            GetDlgItemTextW(dlg, IDC_LS_COUNT, buf, 32);
            unsigned long long count = _wcstoui64(buf, NULL, 10);
            int emit    = GetDlgItemInt(dlg, IDC_LS_EMIT,    NULL, FALSE);
            int threads = GetDlgItemInt(dlg, IDC_LS_THREADS, NULL, FALSE);
            if (count < 1 || emit < 1 || threads < 1 || threads > 64)
            {
                MessageBoxW(dlg, L"Count and Emit must be at least 1; Threads 1-64.",
                            L"Run Latent Search", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            if (!LaunchSearch(start, count, emit, threads,
                              IsDlgButtonChecked(dlg, IDC_LS_SWARM)   == BST_CHECKED,
                              IsDlgButtonChecked(dlg, IDC_LS_SEEK)    == BST_CHECKED,
                              IsDlgButtonChecked(dlg, IDC_LS_REFRESH) == BST_CHECKED,
                              IsDlgButtonChecked(dlg, IDC_LS_NOHARVEST) == BST_CHECKED))
            {
                MessageBoxW(dlg, L"Could not launch tools\\latent_search.ps1 - is the "
                                 L"exe running outside its repo tree?",
                            L"Run Latent Search", MB_OK | MB_ICONERROR);
                return TRUE;   // stay open so the fields aren't lost
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

extern "C" void LatentSearchDialog(HWND owner)
{
    // Modal on the render thread: the loop pauses while the dialog is up, same
    // trade the menu bar already makes while a popup is tracking.
    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_LATENT_SEARCH),
                    owner, SearchDlgProc, 0);
}

// ==================== search progress (overlay + title) ======================
// The launched script logs UTF-16LE (PowerShell *>> to the fresh file); the
// prospector inside it flushes a "...~a/b scanned" line every ~256K seeds and
// the triage phase writes one "... mean=" line per verdicted roll, so the log
// tail is a complete progress feed. Polled every ~30 frames.

// The live main hwnd, stashed by the host on every window (re)creation
// (CPluginShell's own accessor is protected).
extern "C" HWND HostGetMainWnd(void);

// Find the LAST occurrence of needle (wcsstr finds the first).
static const wchar_t* LastOccur(const wchar_t* hay, const wchar_t* needle)
{
    const wchar_t *found = NULL, *p = hay;
    while ((p = wcsstr(p, needle)) != NULL) { found = p; p++; }
    return found;
}

static void SearchPollLog(void)
{
    if (g_searchPhase == LS_IDLE || g_searchPhase == LS_DONE || g_searchPhase == LS_FAILED)
        return;

    // Tail of the log, decoded to a wide string. UTF-16LE when the BOM says so
    // (the normal case - we delete the log before launch and PowerShell recreates
    // it Unicode); raw bytes widened otherwise.
    static wchar_t txt[8192];
    int n = 0;
    HANDLE f = CreateFileW(g_searchLogPath, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        static unsigned char raw[16384];
        DWORD size = GetFileSize(f, NULL);
        DWORD take = (size > sizeof(raw)) ? sizeof(raw) : size;
        SetFilePointer(f, size - take, NULL, FILE_BEGIN);
        DWORD got = 0;
        ReadFile(f, raw, take, &got, NULL);
        CloseHandle(f);

        unsigned char* p = raw;
        if (got >= 2 && ((raw[0] == 0xFF && raw[1] == 0xFE) || size > take))
        {
            // UTF-16LE (skip BOM / realign an odd tail-split to wchar pairs)
            DWORD ofs = (raw[0] == 0xFF && raw[1] == 0xFE) ? 2 : (got & 1);
            for (DWORD i = ofs; i + 1 < got && n < 8190; i += 2)
            {
                wchar_t c = (wchar_t)(raw[i] | (raw[i + 1] << 8));
                txt[n++] = c ? c : L' ';
            }
        }
        else
            for (DWORD i = 0; i < got && n < 8190; i++)
                txt[n++] = (wchar_t)raw[i];
    }
    txt[n] = 0;

    if (LastOccur(txt, L"search complete"))
    {
        g_searchPhase = LS_DONE; g_searchFrac = 1.0f;
        wcscpy_s(g_searchText, L"search done - haul is in X");
        if (!g_searchEndTick) g_searchEndTick = GetTickCount();
        return;
    }
    if (LastOccur(txt, L"overlaps already-searched") || LastOccur(txt, L"Exception") ||
        (g_searchProc && WaitForSingleObject(g_searchProc, 0) == WAIT_OBJECT_0))
    {
        g_searchPhase = LS_FAILED;
        wcscpy_s(g_searchText, L"search FAILED - see latent_search_last.log");
        if (!g_searchEndTick) g_searchEndTick = GetTickCount();
        return;
    }
    if (LastOccur(txt, L"PHASE C"))
    {
        g_searchPhase = LS_OPENING; g_searchFrac = 1.0f;
        wcscpy_s(g_searchText, L"search: opening X");
        return;
    }
    const wchar_t* phaseB = LastOccur(txt, L"PHASE B");
    if (phaseB)
    {
        int done = 0;
        for (const wchar_t* p = phaseB; (p = wcsstr(p, L" mean=")) != NULL; p++) done++;
        g_searchPhase = LS_TRIAGE;
        g_searchFrac  = (g_searchEmit > 0 && done < g_searchEmit)
                        ? (float)done / g_searchEmit : 1.0f;
        swprintf(g_searchText, L"search: triage %d/%d", done, g_searchEmit);
        return;
    }
    const wchar_t* tick = LastOccur(txt, L"...~");
    if (tick)
    {
        unsigned long long a = 0, b = 0;
        if (swscanf(tick, L"...~%llu/%llu", &a, &b) == 2 && b > 0)
        {
            float m = (float)((double)a / (double)b);
            g_searchPhase = LS_SCAN;
            g_searchScanTicked = 1;
            if (m > g_searchFrac)
            {
                // Two ticks give the rate the glide runs at. The EMA keeps a
                // slow thread or a paused machine from whipping the bar.
                DWORD now = GetTickCount();
                if (g_searchTickTime && now > g_searchTickTime)
                {
                    float r = (m - g_searchFrac) / (float)(now - g_searchTickTime);
                    g_searchRate = (g_searchRate > 0.0f) ? (0.6f * g_searchRate + 0.4f * r) : r;
                }
                g_searchTickTime = now;
                g_searchPrevFrac = g_searchFrac;
                g_searchFrac     = m;
            }
            return;   // harvest lines linger in the tail; real ticks outrank them
        }
    }
    // Scanning, but no tick has flushed yet: the prospector only flushes every
    // ~256K seeds, which is about a minute of wall clock at the usual rate.
    // Without this the feed still said "starting" well into the scan.
    if (LastOccur(txt, L"PHASE A"))
    {
        g_searchPhase = LS_SCAN;
        if (!g_searchScanTicked) wcscpy_s(g_searchText, L"search: scanning");
        return;
    }
    // Phase 0 (queue-leftover harvest) — only reached before the first scan
    // tick. LS_TRIAGE keeps it title-only: harvest triage measures the canvas
    // too, so the corner bar must stay out of the frame.
    if (LastOccur(txt, L"HARVEST done"))
    {
        g_searchPhase = LS_SCAN; g_searchFrac = 0.0f;
        wcscpy_s(g_searchText, L"search: early haul in X - scanning");
        return;
    }
    const wchar_t* hv = LastOccur(txt, L"HARVEST:");
    if (hv)
    {
        int total = 0, done = 0;
        swscanf(hv, L"HARVEST: top %d", &total);
        for (const wchar_t* p = hv; (p = wcsstr(p, L" mean=")) != NULL; p++) done++;
        g_searchPhase = LS_TRIAGE;
        if (total > 0) swprintf(g_searchText, L"search: early haul %d/%d", done, total);
        else           wcscpy_s(g_searchText, L"search: checking queue leftovers");
        return;
    }
    // no markers yet: keep "search: starting"
}

extern "C" void LatentSearchOverlayRender(IDirect3DDevice9* dev, int w, int h)
{
    if (g_searchPhase == LS_IDLE) return;

    if (++g_searchPoll >= 30) { g_searchPoll = 0; SearchPollLog(); }

    // Glide the drawn fraction toward (never past) the next expected tick.
    DWORD now = GetTickCount();
    DWORD dtFrame = g_searchFrameTick ? (now - g_searchFrameTick) : 0;
    g_searchFrameTick = now;
    if (g_searchPhase == LS_SCAN)
    {
        if (g_searchShown < g_searchFrac) g_searchShown = g_searchFrac;  // truth wins
        if (g_searchRate > 0.0f && dtFrame)
        {
            // Ceiling = where the next tick is due. Reaching it means the tick
            // is late, and the bar simply waits there rather than lying ahead.
            float step = g_searchFrac - g_searchPrevFrac;
            float ceiling = g_searchFrac + (step > 0.0f ? step : 0.0f);
            if (ceiling > 0.995f) ceiling = 0.995f;
            g_searchShown += g_searchRate * (float)dtFrame;
            if (g_searchShown > ceiling) g_searchShown = ceiling;
        }
        if (g_searchScanTicked)
        {
            // Text follows the glide so the number and the bar never disagree.
            int pct = (int)(g_searchShown * 100.0f);
            if (pct > 99) pct = 99;
            float left = (g_searchRate > 0.0f) ? (1.0f - g_searchShown) / g_searchRate : 0.0f;
            int secs = (int)(left / 1000.0f);
            if (secs > 0 && secs < 100 * 60)
            {
                if (secs >= 60) swprintf(g_searchText, L"search: scanning %d%% (%dm left)", pct, secs / 60);
                else            swprintf(g_searchText, L"search: scanning %d%% (%ds left)", pct, secs);
            }
            else
                swprintf(g_searchText, L"search: scanning %d%%", pct);
        }
    }
    else if (g_searchPhase == LS_DONE || g_searchPhase == LS_OPENING)
        g_searchShown = 1.0f;
    else
        g_searchShown = g_searchFrac;

    // done/failed linger, then clean up (title restored below on the way out)
    if (g_searchEndTick &&
        GetTickCount() - g_searchEndTick > (DWORD)(g_searchPhase == LS_DONE ? 6000 : 15000))
    {
        g_searchPhase = LS_IDLE; g_searchFrac = 0.0f; g_searchText[0] = 0;
        g_searchEndTick = 0;
        if (g_searchProc) { CloseHandle(g_searchProc); g_searchProc = NULL; }
    }

    // Window title carries the phase through ALL phases (it is outside the
    // canvas, so it never lands in a measured frame).
    wchar_t want[96];
    if (g_searchPhase != LS_IDLE) swprintf(want, L"Licium - %s", g_searchText);
    else                          wcscpy_s(want, L"Licium");
    if (wcscmp(want, g_searchTitle) != 0)
    {
        wcscpy_s(g_searchTitle, want);
        HWND main = HostGetMainWnd();
        if (main) SetWindowTextW(main, want);
    }

    // The bar itself: scanning and the end states only. During triage/opening
    // the canvas is being photographed and measured - nothing may draw on it.
    if (g_searchPhase != LS_SCAN && g_searchPhase != LS_DONE && g_searchPhase != LS_FAILED)
        return;
    if (!dev || w <= 0 || h <= 0) return;

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

    // 300 wide: m_text clips rather than ellipsizing, and the longest strings
    // here (scanning + ETA, and the failure line) overran the old 230.
    float bw = 300.0f, bh = 36.0f, mgn = 10.0f;
    float x0 = w - mgn - bw, y0 = h - mgn - bh;
    LatRect(dev, w, h, x0, y0, x0 + bw, y0 + bh, 0xD8000000);
    LatText(g_searchText, (int)x0 + 8, (int)y0 + 3, (int)(x0 + bw) - 4, (int)y0 + 20,
            g_searchPhase == LS_FAILED ? 0xFFFF6060 : 0xFFFFFFFF);
    float bx0 = x0 + 8, bx1 = x0 + bw - 8, by0 = y0 + bh - 12, by1 = y0 + bh - 6;
    LatRect(dev, w, h, bx0, by0, bx1, by1, 0xFF303030);
    if (g_searchShown > 0.0f)
        LatRect(dev, w, h, bx0, by0, bx0 + (bx1 - bx0) * g_searchShown, by1,
                g_searchPhase == LS_FAILED ? 0xFFB03030 : 0xFF30C060);

    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
}

#endif // MILKDROP_STANDALONE
