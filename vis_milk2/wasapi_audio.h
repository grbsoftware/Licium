// wasapi_audio.h
//
// Clean-room WASAPI loopback audio capture for the standalone MD2 host. Grabs
// whatever is playing on the default render endpoint (system output) and exposes
// it as MilkDrop's Winamp-style waveform: unsigned char 0..255 per sample, 128 =
// zero line, 576 samples per channel. The engine runs its own FFT over this to
// derive bass/mid/treble, so a fresh scope snapshot each frame is all it needs.
//
// Written against the Windows Core Audio (WASAPI) API only -- no MilkDrop-derived
// audio code was consulted. Compiled ONLY into the standalone exe (guarded by
// MILKDROP_STANDALONE); the Winamp DLL build never sees it and stays pristine.

#ifndef WASAPI_AUDIO_H
#define WASAPI_AUDIO_H

#ifdef MILKDROP_STANDALONE

#ifdef __cplusplus
extern "C" {
#endif

// Start loopback capture of the default render endpoint on a background thread.
// Returns 1 on success, 0 on failure (caller should fall back to silence/synth).
int  WasapiAudioStart(void);

// Stop capture and join the thread. Safe to call even if start failed.
void WasapiAudioStop(void);

// Fill waveL/waveR (576 bytes each) with the most recent waveform snapshot.
// Returns 1 if real capture data was written, 0 if capture is not running (in
// which case the buffers are left untouched).
int  WasapiAudioGetWaveform(unsigned char *waveL, unsigned char *waveR);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // WASAPI_AUDIO_H
