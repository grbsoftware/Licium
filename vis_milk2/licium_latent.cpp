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
#include "licium_res.h"
#include "licium_oklab.h"
#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <math.h>
#include <wctype.h>   // towupper (lineage-filter prefix match)
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

extern CPlugin g_plugin;
extern "C" HWND HostGetMainWnd(void);   // standalone_host.cpp - dialog parent

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

// Browser state machine. DESCRIBE / FILTER = the engine waitstring owns the
// keyboard (FILTER types a lineage prefix, e.g. "LX5", to narrow the list).
enum { LB_CLOSED = 0, LB_LANDING, LB_BROWSE, LB_DESCRIBE, LB_FILTER };
static int g_state  = LB_CLOSED;
static int g_sel    = 0;
static int g_scroll = 0;

static wchar_t g_rollsDir[MAX_PATH]     = {0};
static wchar_t g_notesPath[MAX_PATH]    = {0};
static wchar_t g_tuningsPath[MAX_PATH]  = {0};

static void ResolvePaths(void)
{
    if (g_rollsDir[0]) return;
    // Same layout law as Bandit: rolls live in their own subfolder of the
    // engine's preset dir (findable from the normal preset list), state files
    // in the Licium\ root next to palette_votes.txt.
    swprintf(g_rollsDir, L"%slatent\\", g_plugin.m_szPresetDir);
    CreateDirectoryW(g_rollsDir, NULL);
    swprintf(g_notesPath,   L"%slatent_notes.txt",   g_plugin.m_szMilkdrop2Path);
    swprintf(g_tuningsPath, L"%slatent_tunings.txt", g_plugin.m_szMilkdrop2Path);
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

// Section a roll falls into (Gary, 2026-07-21 "put all the no's somewhere"):
//   0 TO VOTE (unvoted)  1 LIKED  2 PASSED (the no's shelf, out of the way)
// Likes sit ABOVE the passed shelf so they stay reachable (revisiting a like
// to try other palettes is a first-class flow now — see palette cycling).
static int RollGroup(const LatentRoll* r)
{
    return (r->vote == 0) ? 2 : (r->vote == 1) ? 1 : 0;
}
static const wchar_t* SECTION_LABEL[3] =
    { L"- to vote -", L"- liked -", L"- passed (no's) -" };

// Parse "LX<gen>-<seed>" / "LXS<gen>-<seed>" into (generation, swarm?, seed).
// gen = -1 for anything that doesn't parse, so foreign ids sort last.
static void LatParseId(const wchar_t* id, int* gen, int* swarm, unsigned int* seed)
{
    *gen = -1; *swarm = 0; *seed = 0;
    if (id[0] != L'L' || id[1] != L'X') return;
    const wchar_t* p = id + 2;
    if (*p == L'S') { *swarm = 1; p++; }
    int g = 0, any = 0;
    while (*p >= L'0' && *p <= L'9') { g = g * 10 + (*p - L'0'); p++; any = 1; }
    if (!any) return;
    *gen = g;
    if (*p == L'-') p++;
    *seed = (unsigned int)wcstoul(p, NULL, 16);
}

static int __cdecl RollCompare(const void* a, const void* b)
{
    const LatentRoll* ra = (const LatentRoll*)a;
    const LatentRoll* rb = (const LatentRoll*)b;
    // Group first (unvoted -> liked -> passed shelf). Sort is applied at open
    // only, so a mid-session vote never moves a roll out from under the cursor.
    int ga = RollGroup(ra), gb = RollGroup(rb);
    if (ga != gb) return ga - gb;
    // Then NEWEST-FIRST within the group. Plain id-string descending is NOT
    // enough: "LXS4" > "LX5" char-by-char ('S' 0x53 > '5' 0x35), so a stale
    // swarm-gen-4 pile would outrank a fresh cold gen-5 haul. Sort by parsed
    // GENERATION descending (5 beats 4 regardless of the S), then cold before
    // swarm, then seed descending — so the newest search results sit on top.
    int gena, genb, swa, swb; unsigned int sa, sb;
    LatParseId(ra->id, &gena, &swa, &sa);
    LatParseId(rb->id, &genb, &swb, &sb);
    if (gena != genb) return genb - gena;              // higher generation first
    if (swa  != swb ) return swa - swb;                // cold (0) before swarm (1)
    if (sa   != sb  ) return (sa < sb) ? 1 : -1;       // higher seed first
    return wcscmp(rb->id, ra->id);                     // stable fallback
}

// ---- sectioned display list -------------------------------------------------
// The browse list interleaves section-header rows between groups so the shelf
// structure is visible. g_sel/navigation stay in ROLL space (voting/loading
// unchanged); this list is a render/scroll model only, rebuilt at open.
struct DispEntry { int isHeader; int group; int rollIdx; };
static DispEntry* g_disp       = NULL;
static int*       g_rollToDisp = NULL;   // roll index -> its display row
static int        g_dispCount  = 0;
static int        g_dispCap    = 0;

// Toggle (h): when on, the voted rolls (liked + passed) drop out of the list and
// out of navigation, leaving only the "to vote" rolls. Persisted per Gary's use.
static int        g_hideVoted  = 0;

// Lineage filter (/): a case-insensitive PREFIX on the id, so "LX5" shows only
// the LX5 haul, "LXS4" the swarm-gen-4 pile, "LX" all cold+swarm. Empty = off.
// Lets Gary jump to any lineage regardless of the global newest-first order.
static wchar_t    g_filter[LATENT_ID_LEN] = { 0 };

static int RollMatchesFilter(int i)
{
    if (!g_filter[0]) return 1;
    const wchar_t* id = g_rolls[i].id;
    for (int k = 0; g_filter[k]; k++)
        if (towupper(id[k]) != towupper(g_filter[k])) return 0;   // stops at id's \0 too
    return 1;
}
static int RollVisible(int i)
{
    if (g_hideVoted && RollGroup(&g_rolls[i]) != 0) return 0;
    return RollMatchesFilter(i);
}

static void BuildDisplayList(void)
{
    g_dispCount = 0;
    if (g_count <= 0) return;
    int need = g_count + 4;               // up to 3 headers + slack
    if (need > g_dispCap)
    {
        DispEntry* nd = (DispEntry*)realloc(g_disp, need * sizeof(DispEntry));
        int*       nr = (int*)realloc(g_rollToDisp, g_count * sizeof(int));
        if (!nd || !nr) { free(nd ? nd : g_disp); g_disp = NULL; return; }
        g_disp = nd; g_rollToDisp = nr; g_dispCap = need;
    }
    int prevGroup = -1;
    for (int i = 0; i < g_count; i++)
    {
        int g = RollGroup(&g_rolls[i]);
        if (!RollVisible(i)) { g_rollToDisp[i] = 0; continue; }   // filtered out
        // Section headers only make sense with the shelf showing; when filtering
        // to unvoted-only there is one group, so drop the header entirely.
        if (!g_hideVoted && g != prevGroup)
        {
            g_disp[g_dispCount].isHeader = 1;
            g_disp[g_dispCount].group    = g;
            g_disp[g_dispCount].rollIdx  = -1;
            g_dispCount++;
            prevGroup = g;
        }
        g_rollToDisp[i] = g_dispCount;
        g_disp[g_dispCount].isHeader = 0;
        g_disp[g_dispCount].group    = g;
        g_disp[g_dispCount].rollIdx  = i;
        g_dispCount++;
    }
}

// Navigation is roll-space; these skip filtered-out rolls so the cursor never
// lands on a hidden one. dir = +1 (down) / -1 (up). Returns -1 if none.
static int StepSel(int from, int dir)
{
    for (int i = from + dir; i >= 0 && i < g_count; i += dir)
        if (RollVisible(i)) return i;
    return -1;
}
static int FirstVisible(void)
{
    for (int i = 0; i < g_count; i++) if (RollVisible(i)) return i;
    return -1;
}
static int LastVisible(void)
{
    for (int i = g_count - 1; i >= 0; i--) if (RollVisible(i)) return i;
    return -1;
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

static int g_palCycle = 0;   // 0 = roll's own palette; 1..N = liked-palette index

// Tune cockpit - Color group state (defined further down, forward-declared here
// so LoadSelection can re-apply a roll's saved color tuning as you browse).
#define TUNE_N 5                                 // hue sat light contrast cycle
static int  g_curTune[TUNE_N] = { 0, 0, 0, 0, 0 };   // 0 on every knob = as rolled
static bool LoadTuneFor(const wchar_t* wid, int out[TUNE_N]);
static void ApplyCurTuneLive(void);
static void TuneRefreshDlg(void);

static void LoadSelection(void)
{
    g_palCycle = 0;                                       // new roll -> own palette
    if (g_count <= 0) return;
    if (!LoadTuneFor(g_rolls[g_sel].id, g_curTune))       // reversible per-roll overlay
        for (int i = 0; i < TUNE_N; i++) g_curTune[i] = 0;
    ApplyCurTuneLive();                                   // raw roll if neutral, else recolored
    TuneRefreshDlg();                                     // sync an open Color dialog
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
    g_hideVoted = GetPrivateProfileIntW(L"settings", L"bLatentHideVoted", 0,
                                        g_plugin.GetConfigIniFile());
    BuildDisplayList();
    int fv = FirstVisible();
    g_sel = (fv >= 0) ? fv : 0;
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

// Lineage filter entry (/). Reuses the engine waitstring like describe does,
// pre-seeded with the current filter so re-filtering edits it in place.
static void BeginFilter(void)
{
    td_waitstr* ws = &g_plugin.m_waitstring;
    ws->bActive         = true;
    ws->bFilterBadChars = false;
    ws->bDisplayAsCode  = false;
    ws->bOvertypeMode   = false;
    ws->nSelAnchorPos   = -1;
    ws->nMaxLen         = LATENT_ID_LEN - 1;
    for (int k = 0; k < LATENT_ID_LEN; k++) ws->szText[k] = g_filter[k];   // seed from current
    ws->nCursorPos = (int)wcslen(ws->szText);
    swprintf(ws->szPrompt,
             L"Filter by lineage prefix (e.g. LX5) - Enter applies, Esc cancels, empty = all");
    ws->szToolTip[0] = 0;
    g_state = LB_FILTER;
}

// Commit the typed prefix, rebuild the display list, and park the cursor on the
// first surviving roll (or nothing, if the filter matches none).
static void ApplyFilter(void)
{
    const wchar_t* s = g_plugin.m_waitstring.szText;
    int k = 0;
    for (; s[k] && k < LATENT_ID_LEN - 1; k++) g_filter[k] = s[k];
    g_filter[k] = 0;
    g_plugin.m_waitstring.bActive = false;
    g_state = LB_BROWSE;
    BuildDisplayList();
    int fv = FirstVisible();
    g_sel    = (fv >= 0) ? fv : 0;
    g_scroll = 0;
    if (g_count && g_dispCount && RollVisible(g_sel)) LoadSelection();
}

// Voting flows the cursor forward and loads the next roll live — one hand
// resting on Y/N, same law as the wall. The end of the list just stops.
static void AdvanceAndLoad(void)
{
    int n = StepSel(g_sel, +1);
    if (n >= 0) { g_sel = n; LoadSelection(); }
}

// h: toggle hiding the voted rolls (yes's + no's). Rebuilds the list, keeps the
// cursor on a visible roll, and persists the choice.
static void ToggleHideVoted(void)
{
    g_hideVoted = !g_hideVoted;
    WritePrivateProfileStringW(L"settings", L"bLatentHideVoted",
                               g_hideVoted ? L"1" : L"0", g_plugin.GetConfigIniFile());
    if (g_count && !RollVisible(g_sel))          // selection just got hidden
    {
        int n = StepSel(g_sel, +1);
        if (n < 0) n = StepSel(g_sel, -1);
        if (n >= 0) g_sel = n;
    }
    BuildDisplayList();
    g_scroll = 0;
}

// Cast a like/pass, flow to the next roll, and - when filtering to unvoted-only
// - drop the just-voted roll from view so voting steadily empties the list.
static void VoteAndAdvance(int v)
{
    CastVote(&g_rolls[g_sel], v);
    AdvanceAndLoad();
    if (g_hideVoted)
    {
        if (!RollVisible(g_sel))                // voted the last visible roll
        { int n = StepSel(g_sel, -1); if (n >= 0) { g_sel = n; LoadSelection(); } }
        BuildDisplayList();
    }
}

// ---- palette cycling (Gary, 2026-07-21) -------------------------------------
// "cycle between palettes I've green lit ... some of these would look better
// with different ones." Per-file, never global. Pressing p re-dresses THIS
// roll with the next liked palette (from the Palettes pillar's votes). p walks
// [own, liked0, liked1, ...] and wraps back to own; moving off the roll resets.
//
// It RECOLORS THE EXISTING .milk IN PLACE — it swaps only the palette-derived
// color literals (buf0 ink injection + the comp ramp stops + the sheen tint)
// and leaves the tree, medium and DECAY exactly as they were. The first cut at
// this re-emitted the seed through the current grammar instead, which regrew a
// DIFFERENT shader (and, when the new roll drew a near-zero decay, one with no
// feedback smear at all — Gary's "flashes with no smearing"). Recoloring the
// dressing block is exact for LX2+ (four ramp stops), best-effort for LX1
// (three stops), and works on every lineage because the dressing block is
// identical across them. No subprocess, so the cut is instant.
#define LAT_MAX_LIKED 512
static char g_likedPal[LAT_MAX_LIKED][48];   // "RRGGBB,RRGGBB,..." packed-hex list
static int  g_likedCount  = 0;
static int  g_likedLoaded = 0;

static void LoadLikedPalettes(void)
{
    if (g_likedLoaded) return;
    g_likedLoaded = 1;
    g_likedCount  = 0;

    wchar_t path[MAX_PATH];
    swprintf(path, L"%spalette_votes.txt", g_plugin.m_szMilkdrop2Path);
    FILE* f = _wfopen(path, L"rb");
    if (!f) return;

    // later-wins per color-list: upsert into a small keyed table, keep vote==1.
    static char keyStr[2048][48];
    static int  keyVote[2048];
    int nk = 0;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char colpart[64]; int vote;
        if (sscanf(line, "%63s %d", colpart, &vote) != 2) continue;
        char emit[48]; int e = 0;
        for (char* c = colpart; *c && e < 47; c++)
            if (*c != '#') emit[e++] = *c;               // "#RRGGBB,..." -> "RRGGBB,..."
        emit[e] = 0;
        if (!e) continue;
        int k = -1;
        for (int i = 0; i < nk; i++) if (!strcmp(keyStr[i], emit)) { k = i; break; }
        if (k < 0 && nk < 2048) { k = nk++; strncpy(keyStr[k], emit, 47); keyStr[k][47] = 0; }
        if (k >= 0) keyVote[k] = vote;                   // later line wins
    }
    fclose(f);

    for (int i = 0; i < nk && g_likedCount < LAT_MAX_LIKED; i++)
        if (keyVote[i] == 1)
        {
            strncpy(g_likedPal[g_likedCount], keyStr[i], 47);
            g_likedPal[g_likedCount][47] = 0;
            g_likedCount++;
        }
}

// Parse a "RRGGBB,RRGGBB,..." liked-palette entry into up to 5 packed-RGB ints.
static int ParseLikedPal(const char* s, unsigned int out[5])
{
    int n = 0;
    while (*s && n < 5)
    {
        char hex[8]; int h = 0;
        while (*s && *s != ',' && h < 7) hex[h++] = *s++;
        hex[h] = 0;
        if (h) out[n++] = (unsigned int)strtoul(hex, NULL, 16);
        if (*s == ',') s++;
    }
    return n;
}

// Perceived luminance for the deep->accent sort — Rec.601 gamma weights, the
// same LicSrgbLum the emitter dresses with (kept in sync by hand; trivial).
static float PalLum(const float* rgb)
{
    return 0.32f*rgb[0] + 0.49f*rgb[1] + 0.29f*rgb[2];
}

// The Latent dressing law, replicated from latent_prospect's EmitPreset
// (luminance sort -> deep = darkest*0.30, four ramp stops, ink = accent/s2
// blend) so an in-place recolor matches what a fresh emit would dress. pn 2..5.
static void DressColors(const unsigned int* pal, int pn,
                        float deep[3], float s1[3], float s2[3],
                        float accent[3], float ink[3])
{
    float rgb[5][3];
    for (int i = 0; i < pn; i++)
    {
        rgb[i][0] = ((pal[i] >> 16) & 0xFF) / 255.0f;
        rgb[i][1] = ((pal[i] >> 8)  & 0xFF) / 255.0f;
        rgb[i][2] = ( pal[i]        & 0xFF) / 255.0f;
    }
    for (int i = 1; i < pn; i++)
        for (int j = i; j > 0 && PalLum(rgb[j]) < PalLum(rgb[j - 1]); j--)
            for (int c = 0; c < 3; c++)
            { float x = rgb[j][c]; rgb[j][c] = rgb[j - 1][c]; rgb[j - 1][c] = x; }
    const int i1 = ((pn - 1) + 1) / 3;
    const int i2 = (2 * (pn - 1) + 1) / 3;
    for (int c = 0; c < 3; c++)
    {
        deep[c]   = rgb[0][c] * 0.30f;
        s1[c]     = rgb[i1][c];
        s2[c]     = rgb[i2][c];
        accent[c] = rgb[pn - 1][c];
        ink[c]    = accent[c] * 0.75f + s2[c] * 0.25f;
    }
}

// Replace the numeric float3(...) token beginning at f3pos in 'line' with
// float3(r,g,b) at the dressing precision, keeping the trailing ')' and the
// rest of the line intact. Returns true if a token was rewritten.
static bool SubFloat3(std::string& line, size_t f3pos, const float* c, const char* sep)
{
    if (f3pos == std::string::npos) return false;
    size_t open = line.find('(', f3pos);
    if (open == std::string::npos) return false;
    size_t close = line.find(')', open);
    if (close == std::string::npos) return false;
    char rep[80];
    _snprintf(rep, sizeof(rep) - 1, "float3(%.4f%s%.4f%s%.4f",
              c[0], sep, c[1], sep, c[2]);
    rep[sizeof(rep) - 1] = 0;
    line.replace(f3pos, close - f3pos, rep);     // ')' at 'close' is preserved
    return true;
}

// Recolor an existing roll .milk into 'dst', swapping only palette-derived color
// literals and leaving the tree, medium and decay untouched. Returns true on write.
static bool RecolorRollFile(const wchar_t* src, const wchar_t* dst,
                            const unsigned int* pal, int pn)
{
    FILE* f = _wfopen(src, L"rb");
    if (!f) return false;
    std::string data;
    { char b[4096]; size_t r; while ((r = fread(b, 1, sizeof(b), f)) > 0) data.append(b, r); }
    fclose(f);

    float deep[3], s1[3], s2[3], accent[3], ink[3];
    DressColors(pal, pn, deep, s1, s2, accent, ink);
    const bool fourStop = data.find("- 2.0)") != std::string::npos;

    std::string out;
    out.reserve(data.size() + 64);
    size_t i = 0;
    while (i < data.size())
    {
        size_t nl  = data.find('\n', i);
        size_t end = (nl == std::string::npos) ? data.size() : nl;
        std::string line = data.substr(i, end - i);   // may keep a trailing '\r'

        if (line.find("= prev*") != std::string::npos &&
            line.find("* f *")   != std::string::npos)
        {
            // buf0 ink injection: the float3 just before "* f *".
            SubFloat3(line, line.rfind("float3(", line.find("* f *")), ink, ", ");
        }
        else if (line.find("float3 ramp = lerp(") != std::string::npos)
        {
            // comp deep + first stop: two float3 on one line — edit the later
            // one first so the earlier index stays valid.
            size_t p1 = line.find("float3(");
            size_t p2 = line.find("float3(", p1 + 1);
            SubFloat3(line, p2, s1,   ",");
            SubFloat3(line, p1, deep, ",");
        }
        else if (line.find("ramp = lerp(ramp,") != std::string::npos)
        {
            // -2.0 stop is the accent; -1.0 stop is s2 in a 4-stop ramp, but
            // the accent in an LX1 3-stop ramp (no -2.0 line).
            const float* col = (line.find("- 2.0)") != std::string::npos)
                             ? accent
                             : (fourStop ? s2 : accent);
            SubFloat3(line, line.find("float3("), col, ",");
        }
        else if (line.find("ret = ramp + float3(") != std::string::npos)
        {
            SubFloat3(line, line.find("float3("), accent, ",");
        }

        out += line;
        if (nl != std::string::npos) out += '\n';
        i = end + 1;
    }

    FILE* g = _wfopen(dst, L"wb");
    if (!g) return false;
    fwrite(out.data(), 1, out.size(), g);
    fclose(g);
    return true;
}

static void CyclePalette(void)
{
    if (g_count == 0) return;
    LoadLikedPalettes();
    if (g_likedCount == 0) return;                       // nothing greenlit yet

    g_palCycle = (g_palCycle + 1) % (g_likedCount + 1);
    LatentRoll* r = &g_rolls[g_sel];

    if (g_palCycle == 0) { g_plugin.LoadPreset(r->file, 0.0f); return; }  // back to own

    unsigned int pal[5];
    int pn = ParseLikedPal(g_likedPal[g_palCycle - 1], pal);
    if (pn < 2) { g_palCycle = 0; g_plugin.LoadPreset(r->file, 0.0f); return; }

    wchar_t temp[MAX_PATH];
    swprintf(temp, L"%s_palcycle.milk", g_rollsDir);     // not latent_* -> never scanned
    if (RecolorRollFile(r->file, temp, pal, pn))
        g_plugin.LoadPreset(temp, 0.0f);                 // instant, in-engine hard cut
}

// ============================ Tune cockpit: Color ============================
//
// Shape the SELECTED roll's color field live, WITHOUT touching its plot. The
// four knobs act in oklch (perceptually even - see licium_oklab.h): hue rotates
// h, saturation scales C, light shifts L, contrast pushes L away from mid. They
// transform the roll's OWN dressed color literals (the same lines
// RecolorRollFile targets), so any lineage works and the relationships between
// the colors hold. Center (0) on every knob == as rolled. Values are a
// reversible per-roll overlay in latent_tunings.txt (later-wins, same family as
// votes/tags/notes); the roll .milk is never overwritten - a throwaway
// _tunecolor.milk carries the recolor, exactly like _palcycle.milk.

static bool TuneNeutral(const int* t)
{
    for (int i = 0; i < TUNE_N; i++) if (t[i]) return false;
    return true;
}

// Transform the float3(...) literal at f3pos through oklch by the knob values,
// then write it back via SubFloat3. No-op when no token sits there.
static void TuneColorAt(std::string& line, size_t f3pos, const int* t)
{
    if (f3pos == std::string::npos) return;
    size_t open = line.find('(', f3pos);
    if (open == std::string::npos) return;
    float c[3];
    if (sscanf(line.c_str() + open + 1, "%f%*[, ]%f%*[, ]%f", &c[0], &c[1], &c[2]) != 3)
        return;

    float L, a, b2;
    LicSrgbToOklab(c[0], c[1], c[2], &L, &a, &b2);
    float C = sqrtf(a * a + b2 * b2);
    float h = atan2f(b2, a);

    h += t[0] * (3.14159265f / 180.0f);               // hue: degrees
    C *= (1.0f + t[1] / 100.0f);                       // saturation: -100..100 -> x0..x2
    if (C < 0.0f) C = 0.0f;
    L  = 0.5f + (L - 0.5f) * (1.0f + t[3] / 100.0f);  // contrast about mid
    L += (t[2] / 100.0f) * 0.30f;                     // light: +-100 -> +-0.30 L
    if (L < 0.0f) L = 0.0f;
    if (L > 1.0f) L = 1.0f;

    float r, g, b;
    LicOklchToSrgbClipped(L, C, h, &r, &g, &b);
    float o[3] = { r, g, b };
    SubFloat3(line, f3pos, o, ", ");
}

// Like RecolorRollFile, but transforms the EXISTING color literals in place
// rather than replacing them from a palette. Same target lines, so it stays in
// sync with the recolor path. Assumes t is non-neutral.
static bool TuneColorsInFile(const wchar_t* src, const wchar_t* dst, const int* t)
{
    FILE* f = _wfopen(src, L"rb");
    if (!f) return false;
    std::string data;
    { char b[4096]; size_t r; while ((r = fread(b, 1, sizeof(b), f)) > 0) data.append(b, r); }
    fclose(f);

    std::string out;
    out.reserve(data.size() + 64);
    size_t i = 0;
    while (i < data.size())
    {
        size_t nl  = data.find('\n', i);
        size_t end = (nl == std::string::npos) ? data.size() : nl;
        std::string line = data.substr(i, end - i);

        if (line.find("= prev*") != std::string::npos &&
            line.find("* f *")   != std::string::npos)
        {
            TuneColorAt(line, line.rfind("float3(", line.find("* f *")), t);
        }
        else if (line.find("float3 ramp = lerp(") != std::string::npos)
        {
            size_t p1 = line.find("float3(");
            size_t p2 = line.find("float3(", p1 + 1);
            TuneColorAt(line, p2, t);   // later token first (SubFloat3 shifts length)
            TuneColorAt(line, p1, t);
        }
        else if (line.find("ramp = lerp(ramp,") != std::string::npos)
        {
            TuneColorAt(line, line.find("float3("), t);
        }
        else if (line.find("ret = ramp + float3(") != std::string::npos)
        {
            TuneColorAt(line, line.find("float3("), t);
            if (t[4] != 0)
            {
                // Cycle: rotate ret's hue around the grey axis by an angle that
                // grows with the engine's `time` -> the colors continuously
                // cycle (a rainbow appears to move). Rodrigues rotation about
                // k=(1,1,1)/sqrt3 preserves luma. sign of t[4] = direction.
                float f = (t[4] / 100.0f) * 1.2f;   // rad/sec at full deflection
                char inj[320];
                _snprintf(inj, sizeof(inj) - 1,
                    " float _hA=%.5f*time; float _hc=cos(_hA);"
                    " float3 _hk=float3(0.5773503,0.5773503,0.5773503);"
                    " ret=ret*_hc+cross(_hk,ret)*sin(_hA)+_hk*dot(_hk,ret)*(1.0-_hc);",
                    f);
                inj[sizeof(inj) - 1] = 0;
                size_t cr = line.find('\r');
                if (cr == std::string::npos) line += inj;
                else                          line.insert(cr, inj);
            }
        }

        out += line;
        if (nl != std::string::npos) out += '\n';
        i = end + 1;
    }

    FILE* g = _wfopen(dst, L"wb");
    if (!g) return false;
    fwrite(out.data(), 1, out.size(), g);
    fclose(g);
    return true;
}

// latent_tunings.txt: "<id> hue sat light contrast" lines, later-wins. Ids are
// ascii (LXn-XXXXXXXX), so a narrow parse is safe.
static bool LoadTuneFor(const wchar_t* wid, int out[TUNE_N])
{
    for (int i = 0; i < TUNE_N; i++) out[i] = 0;
    char id[LATENT_ID_LEN];
    int n = 0; for (; wid[n] && n < LATENT_ID_LEN - 1; n++) id[n] = (char)wid[n];
    id[n] = 0;

    FILE* f = _wfopen(g_tuningsPath, L"rb");
    if (!f) return false;
    bool found = false;
    char line[160], lid[LATENT_ID_LEN]; int a, b, c, d, e;
    while (fgets(line, sizeof(line), f))
    {
        e = 0;   // cycle: absent in old 4-value lines -> 0
        int got = sscanf(line, "%19s %d %d %d %d %d", lid, &a, &b, &c, &d, &e);
        if (got < 5) continue;                    // need at least id + 4 ints
        if (strcmp(lid, id)) continue;
        out[0] = a; out[1] = b; out[2] = c; out[3] = d; out[4] = (got >= 6) ? e : 0;
        found = true;                             // later wins
    }
    fclose(f);
    return found;
}

static void SaveTuneFor(const wchar_t* wid, const int* t)
{
    char id[LATENT_ID_LEN];
    int n = 0; for (; wid[n] && n < LATENT_ID_LEN - 1; n++) id[n] = (char)wid[n];
    id[n] = 0;
    FILE* f = _wfopen(g_tuningsPath, L"ab");
    if (!f) return;
    fprintf(f, "%s %d %d %d %d %d\n", id, t[0], t[1], t[2], t[3], t[4]);
    fclose(f);
}

// Load the current selection through g_curTune (neutral = the raw roll).
static void ApplyCurTuneLive(void)
{
    if (g_count <= 0) return;
    LatentRoll* r = &g_rolls[g_sel];
    if (TuneNeutral(g_curTune)) { g_plugin.LoadPreset(r->file, 0.0f); return; }
    wchar_t temp[MAX_PATH];
    swprintf(temp, L"%s_tunecolor.milk", g_rollsDir);   // not latent_* -> never scanned
    if (TuneColorsInFile(r->file, temp, g_curTune))
        g_plugin.LoadPreset(temp, 0.0f);
    else
        g_plugin.LoadPreset(r->file, 0.0f);
}

// ---- the modeless Color dialog ----------------------------------------------

static HWND g_tuneDlg  = NULL;
static bool g_tuneSync = false;   // guard reciprocal slider<->edit updates

static const int TUNE_TB[TUNE_N]  = { IDC_TUNE_HUE, IDC_TUNE_SAT, IDC_TUNE_LIGHT, IDC_TUNE_CONTRAST, IDC_TUNE_CYCLE };
static const int TUNE_ED[TUNE_N]  = { IDC_TUNE_HUEVAL, IDC_TUNE_SATVAL, IDC_TUNE_LIGHTVAL, IDC_TUNE_CONVAL, IDC_TUNE_CYCVAL };
static const int TUNE_MIN[TUNE_N] = { -180, -100, -100, -100, -100 };
static const int TUNE_MAX[TUNE_N] = {  180,  100,  100,  100,  100 };

// Push g_curTune into an open dialog's controls (after a browse move).
static void TuneRefreshDlg(void)
{
    if (!g_tuneDlg) return;
    g_tuneSync = true;
    for (int i = 0; i < TUNE_N; i++)
    {
        SendMessageW(GetDlgItem(g_tuneDlg, TUNE_TB[i]), TBM_SETPOS, TRUE, g_curTune[i]);
        SetDlgItemInt(g_tuneDlg, TUNE_ED[i], (UINT)g_curTune[i], TRUE);
    }
    g_tuneSync = false;
}

static int TuneRowFromTB(HWND h, HWND dlg)
{
    for (int i = 0; i < TUNE_N; i++) if (h == GetDlgItem(dlg, TUNE_TB[i])) return i;
    return -1;
}

// Apply-and-persist the four current slider values to the selected roll.
static void TuneCommitFromDlg(HWND dlg)
{
    for (int i = 0; i < TUNE_N; i++)
        g_curTune[i] = (int)SendMessageW(GetDlgItem(dlg, TUNE_TB[i]), TBM_GETPOS, 0, 0);
    ApplyCurTuneLive();
    if (g_count > 0) SaveTuneFor(g_rolls[g_sel].id, g_curTune);
}

static INT_PTR CALLBACK TuneColorDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);
        g_tuneSync = true;
        for (int i = 0; i < TUNE_N; i++)
        {
            HWND tb = GetDlgItem(dlg, TUNE_TB[i]);
            SendMessageW(tb, TBM_SETRANGE,   TRUE, MAKELONG(TUNE_MIN[i], TUNE_MAX[i]));
            SendMessageW(tb, TBM_SETTICFREQ, (i == 0) ? 90 : 50, 0);
            SendMessageW(tb, TBM_SETPOS,     TRUE, g_curTune[i]);
            SetDlgItemInt(dlg, TUNE_ED[i], (UINT)g_curTune[i], TRUE);
        }
        g_tuneSync = false;
        return TRUE;
    }

    case WM_HSCROLL:
    {
        int row = TuneRowFromTB((HWND)lp, dlg);
        if (row < 0) break;
        int pos = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
        g_tuneSync = true;
        SetDlgItemInt(dlg, TUNE_ED[row], (UINT)pos, TRUE);   // number tracks the slider
        g_tuneSync = false;
        SetTimer(dlg, 1, 140, NULL);                          // debounce the recolor
        return TRUE;
    }

    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && !g_tuneSync)
        {
            for (int i = 0; i < TUNE_N; i++)
                if (LOWORD(wp) == (WORD)TUNE_ED[i])
                {
                    BOOL ok = FALSE;
                    int v = (int)GetDlgItemInt(dlg, TUNE_ED[i], &ok, TRUE);
                    if (!ok) return TRUE;                     // mid-typing "-" / empty
                    if (v < TUNE_MIN[i]) v = TUNE_MIN[i];
                    if (v > TUNE_MAX[i]) v = TUNE_MAX[i];
                    g_tuneSync = true;
                    SendMessageW(GetDlgItem(dlg, TUNE_TB[i]), TBM_SETPOS, TRUE, v);  // slider snaps as you type
                    g_tuneSync = false;
                    SetTimer(dlg, 1, 140, NULL);
                    return TRUE;
                }
            break;
        }
        switch (LOWORD(wp))
        {
        case IDC_TUNE_RESET:
            g_tuneSync = true;
            for (int i = 0; i < TUNE_N; i++)
            {
                SendMessageW(GetDlgItem(dlg, TUNE_TB[i]), TBM_SETPOS, TRUE, 0);
                SetDlgItemInt(dlg, TUNE_ED[i], 0, TRUE);
            }
            g_tuneSync = false;
            KillTimer(dlg, 1);
            TuneCommitFromDlg(dlg);
            return TRUE;
        case IDCANCEL:
            DestroyWindow(dlg);
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wp == 1) { KillTimer(dlg, 1); TuneCommitFromDlg(dlg); }
        return TRUE;

    case WM_DESTROY:
        g_tuneDlg = NULL;
        return TRUE;
    }
    return FALSE;
}

static void LiciumTuneColorOpen(HWND parent)
{
    if (g_tuneDlg) { SetForegroundWindow(g_tuneDlg); return; }
    g_tuneDlg = CreateDialogParamW(GetModuleHandleW(NULL),
                                   MAKEINTRESOURCEW(IDD_TUNE_COLOR),
                                   parent, TuneColorDlgProc, 0);
    if (g_tuneDlg) ShowWindow(g_tuneDlg, SW_SHOW);
}

extern "C" HWND LatentTuneActiveDlg(void) { return g_tuneDlg; }

extern "C" int LatentBrowserHandleChar(int ch)
{
    switch (g_state)
    {
    case LB_CLOSED:
        return 0;

    case LB_DESCRIBE:
    case LB_FILTER:
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
            VoteAndAdvance(1);
            return 1;
        case 'n': case 'N':
            VoteAndAdvance(0);
            return 1;
        case 'u': case 'U':
            CastVote(&g_rolls[g_sel], -1);
            return 1;
        case 'h': case 'H':
            ToggleHideVoted();
            if (g_count && RollVisible(g_sel)) LoadSelection();
            return 1;
        case '1': case '2': case '3': case '4': case '5': case '6':
            ToggleTag(&g_rolls[g_sel], ch - '1');
            return 1;
        case 'p': case 'P':
            CyclePalette();
            return 1;
        case 'c': case 'C':
            LiciumTuneColorOpen(HostGetMainWnd());   // floating Color knobs (modeless)
            return 1;
        case 'd': case 'D':
            BeginDescribe();
            return 1;
        case '/':
            BeginFilter();
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

    case LB_FILTER:
        // Enter applies the typed prefix, Esc cancels; everything else edits.
        if (vk == VK_RETURN) { ApplyFilter(); return 1; }
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
        case VK_UP:   { int n = StepSel(g_sel, -1); if (n >= 0) { g_sel = n; LoadSelection(); } } return 1;
        case VK_DOWN: { int n = StepSel(g_sel, +1); if (n >= 0) { g_sel = n; LoadSelection(); } } return 1;
        case VK_PRIOR:
            { int n = g_sel; for (int k = 0; k < 16; k++) { int m = StepSel(n, -1); if (m < 0) break; n = m; }
              if (n != g_sel) { g_sel = n; LoadSelection(); } } return 1;
        case VK_NEXT:
            { int n = g_sel; for (int k = 0; k < 16; k++) { int m = StepSel(n, +1); if (m < 0) break; n = m; }
              if (n != g_sel) { g_sel = n; LoadSelection(); } } return 1;
        case VK_HOME: { int n = FirstVisible(); if (n >= 0 && n != g_sel) { g_sel = n; LoadSelection(); } } return 1;
        case VK_END:  { int n = LastVisible();  if (n >= 0 && n != g_sel) { g_sel = n; LoadSelection(); } } return 1;
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

// Pixel width of a string in SIMPLE_FONT — GDI-exact via the engine, so the
// panel hugs the lettering and follows the live UI text scale. (The D3DX
// DT_CALCRECT path left the rect at its input width here, hence this route.)
static int LatTextW(const wchar_t* s)
{
    return g_plugin.HostMeasureTextW(s, SIMPLE_FONT);
}

// The window is the porthole canvas (a fixed square, often 512px) — every
// line here must fit ~60 chars at SIMPLE_FONT size or it clips at the edge
// (DT_WORD_ELLIPSIS doesn't truncate through m_text). Plain '-' only: this
// font has no em-dash glyph.
static void RenderLanding(IDirect3DDevice9* dev, int w, int h)
{
    LatRect(dev, w, h, 0, 0, (float)w, (float)h, 0xE6000000);

    int fh = g_plugin.GetFontHeight(SIMPLE_FONT);
    int line = h / 24;
    if (line < fh + fh / 4) line = fh + fh / 4;   // grow with the UI text scale
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
    LatText(L"D / Enter describe   X / Esc close",                  x0, y, x1, y + line, 0xFFAACCEE); y += line;
    LatText(L"p recolor (liked palettes)   h hide voted rolls",     x0, y, x1, y + line, 0xFFAACCEE); y += line * 2;

    LatText(L"Enter continues \x00b7 N = don't show this again",    x0, y, x1, y + line, 0xFFFFFFFF); y += line;
    LatText(L"Esc leaves",                                          x0, y, x1, y + line, 0xFFFFFFFF);
}

extern "C" void LatentBrowserRender(IDirect3DDevice9* dev, int w, int h)
{
    if (g_state == LB_CLOSED || g_state == LB_DESCRIBE || g_state == LB_FILTER || !dev || w <= 0 || h <= 0)
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
        // hugs the left edge and the rest of the screen stays preset. WIDTH IS
        // DATA-DRIVEN (Gary, 2026-07-21): a fixed 200px clipped the vote-count
        // line, so measure the header lines in SIMPLE_FONT and set the divider
        // a few pixels past the widest lettering. SIMPLE_FONT is fixed-pixel, so
        // this holds across resolutions without the old %-of-width overflow.
        // Row pitch follows the (scalable) font height so rows never overlap
        // when the UI text scale is raised; still at least the old h/30 rhythm.
        int fh      = g_plugin.GetFontHeight(SIMPLE_FONT);
        int line    = h / 30;
        if (line < fh + fh / 4) line = fh + fh / 4;
        if (line < 15) line = 15;
        int headerH = line * 5 + line / 2;
        int rowH    = line + 2;
        const int x0 = 10;

        wchar_t buf[160];
        int nVote = 0, nLike = 0, nNo = 0;
        for (int i = 0; i < g_count; i++)
        {
            int g = RollGroup(&g_rolls[i]);
            if      (g == 0) nVote++;
            else if (g == 1) nLike++;
            else             nNo++;
        }
        swprintf(buf, L"%d liked \x00b7 %d no", nLike, nNo);

        int maxw = LatTextW(buf);
        {
            int t;
            if ((t = LatTextW(L"Y like  N pass  U clr  / find")) > maxw) maxw = t;
            if ((t = LatTextW(L"1-6 tag  D descr"))           > maxw) maxw = t;
            if ((t = LatTextW(L"p pal  h hide voted  X close")) > maxw) maxw = t;
            if ((t = LatTextW(L"LATENT   \x00b7 000 to vote   pal 00/00")) > maxw) maxw = t;
        }

        float panelW = (float)(x0 + maxw + 10);          // ~10px right of the text
        if (panelW < 150.0f)      panelW = 150.0f;
        if (panelW > w - 20.0f)   panelW = (float)((w > 40) ? (w - 20) : w);
        LatRect(dev, w, h, 0, 0, panelW, (float)h, 0xD8000000);
        LatRect(dev, w, h, panelW, 0, panelW + 2.0f, (float)h, 0xFF303030);

        int x1 = (int)panelW - 10;

        // header
        {
            // The prominent count is the UNTOUCHED pile, not g_count (Gary,
            // 2026-07-21): total rolls only mattered for the shelved ML-guided
            // sampling; for cold "what's left to look at" browsing the meaningful
            // number is how many rolls are still to vote on. Position is dropped
            // (the cursor highlight already shows where you are). palCycle state
            // still rides the title line when re-dressing this roll.
            if (g_filter[0])
            {
                int nShown = 0;
                for (int i = 0; i < g_count; i++) if (RollVisible(i)) nShown++;
                swprintf(buf, L"LATENT   \x00b7 [%s]  %d shown", g_filter, nShown);
            }
            else if (g_palCycle > 0)
                swprintf(buf, L"LATENT   \x00b7 %d to vote   pal %d/%d",
                         nVote, g_palCycle, g_likedCount);
            else
                swprintf(buf, L"LATENT   \x00b7 %d to vote", nVote);
            LatText(buf, x0, line / 2, x1, line / 2 + line,
                    g_filter[0] ? 0xFFFFD070
                                : (g_palCycle > 0) ? 0xFF70C0FF
                                : (g_hideVoted ? 0xFF70C0FF : 0xFFFFFFFF));
            swprintf(buf, L"%d liked \x00b7 %d no", nLike, nNo);
            LatText(buf, x0, line / 2 + line, x1, line / 2 + line * 2, 0xFF9999AA);
            LatText(L"Y like  N pass  U clr  / find",
                    x0, line / 2 + line * 2, x1, line / 2 + line * 3, 0xFF9999AA);
            LatText(L"1-6 tag  D descr  c color",
                    x0, line / 2 + line * 3, x1, line / 2 + line * 4, 0xFF9999AA);
            LatText(g_hideVoted ? L"p pal  h show all  X close"
                                : L"p pal  h hide voted  X close",
                    x0, line / 2 + line * 4, x1, line / 2 + line * 5, 0xFF9999AA);
        }

        if (g_count == 0)
        {
            LatText(L"No rolls yet - run tools\\latent_prospect, then latent_triage.ps1.",
                    x0, headerH + rowH, x1, headerH + rowH * 2, 0xFFCCCCCC);
        }
        else if (g_dispCount == 0)   // nothing visible: filter matched none, or all voted+hidden
        {
            if (g_filter[0])
            {
                swprintf(buf, L"No rolls match [%s].", g_filter);
                LatText(buf, x0, headerH + rowH, x1, headerH + rowH * 2, 0xFFCCCCCC);
                LatText(L"Press / then Enter on empty to clear the filter.",
                        x0, headerH + rowH * 2, x1, headerH + rowH * 3, 0xFFCCCCCC);
            }
            else
            {
                LatText(L"All rolls voted.",
                        x0, headerH + rowH, x1, headerH + rowH * 2, 0xFFCCCCCC);
                LatText(L"Press h to show liked + passed again.",
                        x0, headerH + rowH * 2, x1, headerH + rowH * 3, 0xFFCCCCCC);
            }
        }
        else
        {
            // The list scrolls in DISPLAY-row space (rolls + section headers);
            // g_scroll is a display-row offset. Keep the SELECTED roll visible
            // by mapping it through g_rollToDisp.
            int visible = (h - headerH - 4) / rowH;
            if (visible < 1) visible = 1;
            int selDisp = (g_rollToDisp && g_sel < g_count) ? g_rollToDisp[g_sel] : 0;
            if (selDisp < g_scroll)            g_scroll = selDisp;
            if (selDisp >= g_scroll + visible) g_scroll = selDisp - visible + 1;
            if (g_scroll < 0) g_scroll = 0;

            for (int row = 0; row < visible && g_scroll + row < g_dispCount; row++)
            {
                DispEntry* e = &g_disp[g_scroll + row];
                float ry0 = (float)(headerH + row * rowH);
                float ry1 = ry0 + rowH - 1;

                if (e->isHeader)
                {
                    // Section divider: a thin rule + the shelf label, dim.
                    LatRect(dev, w, h, 6.0f, ry1 - 1.0f, panelW - 6.0f, ry1, 0xFF3A3A46);
                    DWORD hcol = (e->group == 1) ? 0xFF70A070    // liked = green-ish
                               : (e->group == 2) ? 0xFF906060    // passed = dim red
                               :                   0xFF8888AA;   // to vote
                    LatText(SECTION_LABEL[e->group], x0, (int)ry0, x1, (int)ry1, hcol);
                    continue;
                }

                LatentRoll* r = &g_rolls[e->rollIdx];
                int idx = e->rollIdx;

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
                        int noHarvest, const wchar_t* gradeWhen)
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
                  L"\"& '%s' -Start %llu -Count %llu -Emit %d -Threads %d%s%s%s%s -GradeWhen '%s' *>> '%s'\"",
             script, start, count, emit, threads,
             swarm     ? L" -Swarm"        : L"",
             seek      ? L" -Seek threads" : L"",
             refresh   ? L""               : L" -NoRefresh",
             noHarvest ? L" -NoHarvest"    : L"",
             gradeWhen,
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
        // Grading schedule: default to grading right away; the "At" field holds a
        // sensible off-hours time in case the user switches to it.
        CheckRadioButton(dlg, IDC_LS_GRADE_NOW, IDC_LS_GRADE_AT, IDC_LS_GRADE_NOW);
        SetDlgItemTextW(dlg, IDC_LS_GRADE_TIME, L"02:00");
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
            // When to grade (Phase B): now / when idle / at a clock time. The
            // value is passed straight to the script's -GradeWhen.
            wchar_t gradeWhen[16] = L"now";
            if (IsDlgButtonChecked(dlg, IDC_LS_GRADE_IDLE) == BST_CHECKED)
                wcscpy_s(gradeWhen, L"idle");
            else if (IsDlgButtonChecked(dlg, IDC_LS_GRADE_AT) == BST_CHECKED)
            {
                wchar_t t[16];
                GetDlgItemTextW(dlg, IDC_LS_GRADE_TIME, t, 16);
                int hh = -1, mm = -1;
                if (swscanf(t, L"%d:%d", &hh, &mm) != 2 ||
                    hh < 0 || hh > 23 || mm < 0 || mm > 59)
                {
                    MessageBoxW(dlg, L"Enter the grading time as HH:MM (24-hour), e.g. 02:00.",
                                L"Run Latent Search", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                swprintf(gradeWhen, L"%02d:%02d", hh, mm);
            }
            if (!LaunchSearch(start, count, emit, threads,
                              IsDlgButtonChecked(dlg, IDC_LS_SWARM)   == BST_CHECKED,
                              IsDlgButtonChecked(dlg, IDC_LS_SEEK)    == BST_CHECKED,
                              IsDlgButtonChecked(dlg, IDC_LS_REFRESH) == BST_CHECKED,
                              IsDlgButtonChecked(dlg, IDC_LS_NOHARVEST) == BST_CHECKED,
                              gradeWhen))
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
    // Deferred grading: the scan is done but Phase B is waiting (idle / a set
    // time). No progress to show, so borrow the title-only triage phase. Checked
    // AFTER Phase B (once grading starts that wins) and BEFORE the scan-tick line
    // (leftover ~100% ticks must not override the wait).
    const wchar_t* defer = LastOccur(txt, L"DEFER-WAIT");
    if (defer)
    {
        g_searchPhase = LS_TRIAGE;
        int hh = 0, mm = 0;
        const wchar_t* at = wcsstr(defer, L"at ");
        if (wcsstr(defer, L"idle"))
            wcscpy_s(g_searchText, L"search: waiting to grade (idle)");
        else if (at && swscanf(at, L"at %d:%d", &hh, &mm) == 2)
            swprintf(g_searchText, L"search: waiting to grade at %02d:%02d", hh, mm);
        else
            wcscpy_s(g_searchText, L"search: waiting to grade");
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
