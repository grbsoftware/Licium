// licium_latent.h — the Latent pillar (docs\LATENT_DESIGN.md).
//
// M5a slice: only the headless load hook Phase B triage needs. The Phase C
// browser (X key, votes/tags/notes) lands in M5b and will grow this header.
// Standalone-only, like every Licium tool.

#ifndef LICIUM_LATENT_H
#define LICIUM_LATENT_H

// Latent generator lineage (IDs LX1-XXXXXXXX, 32-bit seed). The generator
// itself lives in tools\latent_prospect (offline Phase A); this genver is
// recorded here so engine-side code and the tool can't drift silently.
#define LATENT_GENVER 1

// Automation hook: SEND (not post — the payload is a pointer) a WM_COPYDATA
// to the main "Licium" window to hard-cut-load an exact preset file, so the
// Phase B triage script can drive verdicts headlessly:
//   COPYDATASTRUCT.dwData = LICIUM_CD_LOADPRESET
//   COPYDATASTRUCT.lpData = wide (UTF-16) full path, null-terminated
//   COPYDATASTRUCT.cbData = byte length INCLUDING the null
// Returns 1 from the SendMessage when the load was dispatched. Mirrors the
// spirit of WM_LICIUM_BANDIT_ROLL (0x51) / WM_LICIUM_WALL (0x52).
#define LICIUM_CD_LOADPRESET 0x4C4F4144   // 'LOAD'

#endif // LICIUM_LATENT_H
