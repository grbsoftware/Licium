// wasapi_audio.cpp
//
// See wasapi_audio.h. Clean-room WASAPI loopback capture.
//
// Design:
//   * A background thread does COM init (MTA), opens the default *render*
//     endpoint (eRender/eConsole) in shared LOOPBACK mode, and polls the capture
//     client, appending decoded float samples to a small per-channel ring buffer.
//   * The main render thread calls WasapiAudioGetWaveform() once per frame; it
//     copies the most-recent 576 samples per channel out of the ring (under a
//     lock) and converts float [-1,1] -> unsigned char [0,255], 128 = zero.
//   * The most-recent-576-samples window is the direct analog of the 576-sample
//     PCM slice Winamp hands a vis plugin, so no resampling is required.

#ifdef MILKDROP_STANDALONE

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>
#include <initguid.h>                    // define the PKEY below in this TU
#include <functiondiscoverykeys_devpkey.h> // PKEY_Device_FriendlyName
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "wasapi_audio.h"

#pragma comment(lib, "ole32.lib")

#define WAVE_SAMPLES  576         // samples per channel the engine wants
#define RING_SAMPLES  2048        // per-channel ring capacity (>= WAVE_SAMPLES)

static CRITICAL_SECTION g_lock;
static bool   g_lockInit   = false;
static HANDLE g_thread     = NULL;
static volatile bool g_run = false;
static volatile bool g_haveData = false;

// Per-channel ring of recent samples (mono is duplicated into both).
static float  g_ringL[RING_SAMPLES];
static float  g_ringR[RING_SAMPLES];
static int    g_writePos = 0;      // next write index (wraps)

// Rolling peak amplitude for the once-a-second level log (diagnostics: proves
// real audio is flowing vs. a silent endpoint). Written under g_lock.
static float  g_peak = 0.0f;

// --- Current capture target --------------------------------------------------
// g_useDefault: true => default render endpoint (loopback), the "Auto" source.
// Otherwise open g_targetId; loopback unless g_captureMode (a real mic input).
static bool    g_useDefault  = true;
static bool    g_captureMode = false;
static wchar_t g_targetId[512] = { 0 };

// Device-id caches, filled by WasapiAudioListDevices, read by WasapiAudioSelect.
static wchar_t g_renderIds [WASAPI_MAX_DEVICES][512];
static wchar_t g_captureIds[WASAPI_MAX_DEVICES][512];
static int     g_renderCount  = 0;
static int     g_captureCount = 0;

static void RingReset(void)
{
    memset(g_ringL, 0, sizeof(g_ringL));
    memset(g_ringR, 0, sizeof(g_ringR));
    g_writePos = 0;
    g_haveData = false;
}

// Append one stereo frame to the ring (already lock-held by caller).
static inline void RingPush(float l, float r)
{
    g_ringL[g_writePos] = l;
    g_ringR[g_writePos] = r;
    g_writePos = (g_writePos + 1) % RING_SAMPLES;
}

static void AudioLog(const char *msg)
{
    FILE *lf = fopen("host_audio.log", "a");
    if (lf) { fprintf(lf, "%s\n", msg); fclose(lf); }
}

// Decode one packet of raw endpoint bytes into the ring. Handles the two mix
// formats WASAPI shared mode actually hands out: 32-bit IEEE float and 16-bit
// PCM. Silent packets are pushed as zeros so the scope flatlines on silence.
static void DecodePacket(const BYTE *data, UINT32 frames, DWORD flags,
                         const WAVEFORMATEX *wfx)
{
    const int   ch     = wfx->nChannels;
    const int   bits   = wfx->wBitsPerSample;
    const bool  silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

    // Detect float vs PCM. EXTENSIBLE carries the real subformat; treat 32-bit
    // extensible as float (the standard shared-mode mix format) and everything
    // else by bit depth.
    bool isFloat = false;
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) isFloat = true;
    else if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE *we = (const WAVEFORMATEXTENSIBLE *)wfx;
        if (we->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) isFloat = true;
    }

    EnterCriticalSection(&g_lock);
    for (UINT32 i = 0; i < frames; i++)
    {
        float l = 0.0f, r = 0.0f;
        if (!silent && data)
        {
            if (isFloat && bits == 32)
            {
                const float *f = (const float *)(data + (size_t)i * ch * 4);
                l = f[0];
                r = (ch > 1) ? f[1] : f[0];
            }
            else if (bits == 16)
            {
                const short *s = (const short *)(data + (size_t)i * ch * 2);
                l = s[0] * (1.0f / 32768.0f);
                r = (ch > 1) ? s[1] * (1.0f / 32768.0f) : l;
            }
            else if (bits == 32) // 32-bit PCM int fallback
            {
                const int *p = (const int *)(data + (size_t)i * ch * 4);
                l = (float)(p[0] * (1.0 / 2147483648.0));
                r = (ch > 1) ? (float)(p[1] * (1.0 / 2147483648.0)) : l;
            }
        }
        RingPush(l, r);
        float a = l < 0 ? -l : l, b = r < 0 ? -r : r;
        if (a > g_peak) g_peak = a;
        if (b > g_peak) g_peak = b;
    }
    g_haveData = true;
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI CaptureThread(LPVOID)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInited = SUCCEEDED(hr);

    IMMDeviceEnumerator *pEnum = NULL;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void **)&pEnum);
    if (FAILED(hr)) { AudioLog("CoCreateInstance(MMDeviceEnumerator) failed"); goto done; }

    // Outer loop: (re)open the endpoint and capture until it goes away, or -- for
    // "Auto" -- until the DEFAULT OUTPUT changes (e.g. speakers -> headphones).
    // The old code opened once and looped forever on that first endpoint, so
    // switching outputs left the visuals reacting to a now-silent device (and an
    // unplug just killed the thread). Now a change breaks the inner loop and we
    // reopen on whatever is current.
    while (g_run)
    {
        IMMDevice           *pDev    = NULL;
        IAudioClient        *pClient = NULL;
        IAudioCaptureClient *pCap    = NULL;
        WAVEFORMATEX        *wfx     = NULL;
        bool     started  = false;
        wchar_t  openedId[512] = { 0 };   // id of the endpoint we opened (Auto-follow)
        DWORD    lastLog = 0, lastDevChk = 0;

        // Pick the endpoint: "Auto" = default render (loopback); otherwise the
        // explicitly selected device (a specific speaker = still loopback, a mic
        // = direct capture).
        if (g_useDefault)
            hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDev);
        else
            hr = pEnum->GetDevice(g_targetId, &pDev);
        if (FAILED(hr) || !pDev) { AudioLog("open endpoint failed - retrying"); goto reopen; }

        // Remember its id so an "Auto" capture can notice a later default switch.
        {
            LPWSTR idStr = NULL;
            if (SUCCEEDED(pDev->GetId(&idStr)) && idStr)
            {
                wcsncpy_s(openedId, 512, idStr, _TRUNCATE);
                CoTaskMemFree(idStr);
            }
        }

        hr = pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&pClient);
        if (FAILED(hr)) { AudioLog("Activate(IAudioClient) failed"); goto reopen; }

        hr = pClient->GetMixFormat(&wfx);
        if (FAILED(hr) || !wfx) { AudioLog("GetMixFormat failed"); goto reopen; }

        // Shared mode. Loopback flag for render endpoints (taps what plays); no
        // flag for a real mic (direct capture). 200ms buffer, poll-driven
        // (loopback event mode is finicky; polling a shared buffer is robust).
        {
            DWORD streamFlags = g_captureMode ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK;
            hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags,
                                     2000000 /*200ms in 100ns units*/, 0, wfx, NULL);
        }
        if (FAILED(hr)) { AudioLog("IAudioClient::Initialize failed"); goto reopen; }

        hr = pClient->GetService(__uuidof(IAudioCaptureClient), (void **)&pCap);
        if (FAILED(hr)) { AudioLog("GetService(IAudioCaptureClient) failed"); goto reopen; }

        hr = pClient->Start();
        if (FAILED(hr)) { AudioLog("IAudioClient::Start failed"); goto reopen; }
        started = true;
        {
            char m[128];
            _snprintf_s(m, sizeof(m), _TRUNCATE,
                "WASAPI loopback started: %uHz %uch %ubit",
                wfx->nSamplesPerSec, wfx->nChannels, wfx->wBitsPerSample);
            AudioLog(m);
        }

        lastLog = lastDevChk = GetTickCount();
        while (g_run)
        {
            // Once a second, report the peak level so the log shows real audio is
            // being captured (peak > 0 => music is flowing; ~0 => silent endpoint).
            DWORD now = GetTickCount();
            if (now - lastLog >= 1000)
            {
                EnterCriticalSection(&g_lock);
                float peak = g_peak; g_peak = 0.0f;
                LeaveCriticalSection(&g_lock);
                char m[64];
                _snprintf_s(m, sizeof(m), _TRUNCATE, "level peak=%.3f", peak);
                AudioLog(m);
                lastLog = now;
            }

            // Auto: follow the default output. If the default render endpoint is
            // now a different device than the one we opened, break out and reopen
            // on it -- this is what makes a speakers->headphones switch take.
            if (g_useDefault && now - lastDevChk >= 1000)
            {
                lastDevChk = now;
                IMMDevice *pCur = NULL;
                if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pCur)) && pCur)
                {
                    LPWSTR curId = NULL;
                    if (SUCCEEDED(pCur->GetId(&curId)) && curId)
                    {
                        bool changed = (openedId[0] && wcscmp(curId, openedId) != 0);
                        CoTaskMemFree(curId);
                        if (changed)
                        {
                            AudioLog("default output changed - reopening loopback on it");
                            pCur->Release();
                            break;
                        }
                    }
                    pCur->Release();
                }
            }

            UINT32 packet = 0;
            hr = pCap->GetNextPacketSize(&packet);
            if (FAILED(hr)) break;   // device invalidated (unplug/switch) -> reopen

            if (packet == 0) { Sleep(5); continue; }

            while (packet != 0 && g_run)
            {
                BYTE  *data = NULL;
                UINT32 frames = 0;
                DWORD  flags = 0;
                hr = pCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
                if (FAILED(hr)) { packet = 0; break; }

                DecodePacket(data, frames, flags, wfx);
                pCap->ReleaseBuffer(frames);

                hr = pCap->GetNextPacketSize(&packet);
                if (FAILED(hr)) break;
            }
        }

    reopen:
        if (started && pClient) pClient->Stop();
        if (wfx)     CoTaskMemFree(wfx);
        if (pCap)    pCap->Release();
        if (pClient) pClient->Release();
        if (pDev)    pDev->Release();
        // Pause before reopening so a mid-transition device (unplug/replug, or a
        // default switch still settling) doesn't spin the loop.
        if (g_run) Sleep(400);
    }

done:
    if (pEnum)  pEnum->Release();
    if (comInited) CoUninitialize();
    return 0;
}

extern "C" int WasapiAudioStart(void)
{
    if (g_thread) return 1;   // already running
    if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
    RingReset();
    g_run = true;
    g_thread = CreateThread(NULL, 0, CaptureThread, NULL, 0, NULL);
    if (!g_thread) { g_run = false; return 0; }
    return 1;
}

extern "C" void WasapiAudioStop(void)
{
    g_run = false;
    if (g_thread)
    {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}

extern "C" int WasapiAudioGetWaveform(unsigned char *waveL, unsigned char *waveR)
{
    if (!g_thread || !g_haveData || !g_lockInit) return 0;

    EnterCriticalSection(&g_lock);
    // Copy the WAVE_SAMPLES most-recent samples ending at g_writePos.
    int start = g_writePos - WAVE_SAMPLES;
    while (start < 0) start += RING_SAMPLES;
    for (int i = 0; i < WAVE_SAMPLES; i++)
    {
        int idx = (start + i) % RING_SAMPLES;
        int vl = 128 + (int)(g_ringL[idx] * 127.0f);
        int vr = 128 + (int)(g_ringR[idx] * 127.0f);
        waveL[i] = (unsigned char)(vl < 0 ? 0 : vl > 255 ? 255 : vl);
        waveR[i] = (unsigned char)(vr < 0 ? 0 : vr > 255 ? 255 : vr);
    }
    LeaveCriticalSection(&g_lock);
    return 1;
}

extern "C" int WasapiAudioListDevices(int isCapture, wchar_t names[][WASAPI_NAME_LEN], int maxN)
{
    // Own COM init on the UI thread; tolerate an already-initialized apartment.
    HRESULT ci = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool doUninit = (ci == S_OK || ci == S_FALSE);

    IMMDeviceEnumerator *pEnum = NULL;
    IMMDeviceCollection *pColl = NULL;
    int count = 0;

    wchar_t (*idCache)[512] = isCapture ? g_captureIds : g_renderIds;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void **)&pEnum);
    if (FAILED(hr)) goto done;

    hr = pEnum->EnumAudioEndpoints(isCapture ? eCapture : eRender,
                                   DEVICE_STATE_ACTIVE, &pColl);
    if (FAILED(hr) || !pColl) goto done;

    UINT n = 0;
    pColl->GetCount(&n);
    for (UINT i = 0; i < n && count < maxN && count < WASAPI_MAX_DEVICES; i++)
    {
        IMMDevice *pDev = NULL;
        if (FAILED(pColl->Item(i, &pDev)) || !pDev) continue;

        LPWSTR id = NULL;
        if (SUCCEEDED(pDev->GetId(&id)) && id)
        {
            wcsncpy_s(idCache[count], 512, id, _TRUNCATE);
            CoTaskMemFree(id);

            // Friendly name via the device property store.
            names[count][0] = 0;
            IPropertyStore *pProps = NULL;
            if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pProps)) && pProps)
            {
                PROPVARIANT pv; PropVariantInit(&pv);
                if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &pv)) &&
                    pv.vt == VT_LPWSTR && pv.pwszVal)
                    wcsncpy_s(names[count], WASAPI_NAME_LEN, pv.pwszVal, _TRUNCATE);
                PropVariantClear(&pv);
                pProps->Release();
            }
            if (names[count][0] == 0)
                swprintf_s(names[count], WASAPI_NAME_LEN, L"Device %d", count + 1);
            count++;
        }
        pDev->Release();
    }

done:
    if (pColl) pColl->Release();
    if (pEnum) pEnum->Release();
    if (isCapture) g_captureCount = count; else g_renderCount = count;
    if (doUninit) CoUninitialize();
    return count;
}

extern "C" int WasapiAudioSelect(int isCapture, int index)
{
    WasapiAudioStop();

    if (index < 0)
    {
        // "Auto": default render endpoint, loopback.
        g_useDefault  = true;
        g_captureMode = false;
        g_targetId[0] = 0;
    }
    else
    {
        wchar_t (*idCache)[512] = isCapture ? g_captureIds : g_renderIds;
        int cnt = isCapture ? g_captureCount : g_renderCount;
        if (index >= cnt) { WasapiAudioStart(); return 0; }  // stale index
        g_useDefault  = false;
        g_captureMode = (isCapture != 0);
        wcsncpy_s(g_targetId, 512, idCache[index], _TRUNCATE);
    }
    return WasapiAudioStart();
}

#endif // MILKDROP_STANDALONE
