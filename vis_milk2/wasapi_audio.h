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

// --- Device selection (M1c UI: Audio > Speakers / Microphones pickers) -------

#define WASAPI_MAX_DEVICES 16
#define WASAPI_NAME_LEN    128

// Enumerate active audio endpoints of one kind: isCapture=0 -> render endpoints
// ("Speakers", captured via loopback); isCapture=1 -> capture endpoints
// ("Microphones"). Fills `names` (wide friendly names) up to maxN and caches the
// matching device ids internally so WasapiAudioSelect() can reopen them. Returns
// the count found. Safe to call from the UI thread (does its own COM init).
int  WasapiAudioListDevices(int isCapture, wchar_t names[][WASAPI_NAME_LEN], int maxN);

// Switch the capture source and restart the thread. index < 0 selects the
// DEFAULT render endpoint in loopback mode ("Auto"); otherwise selects the
// (isCapture, index) endpoint from the most recent WasapiAudioListDevices call
// of that kind. Returns 1 on success.
int  WasapiAudioSelect(int isCapture, int index);

#ifdef __cplusplus
}
#endif

#endif // MILKDROP_STANDALONE
#endif // WASAPI_AUDIO_H
