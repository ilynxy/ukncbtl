/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// SoundGen.cpp

#if 1

#include "SoundGen.h"

#define MINIAUDIO_IMPLEMENTATION
//#define MA_DEBUG_OUTPUT

#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MA_ENABLE_DSOUND
#define MA_ENABLE_WINMM

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
//#define MA_NO_GENERATION

#include "miniaudio.h"

#define DEVICE_FORMAT       ma_format_s16 // ma_format_unknown
#define DEVICE_CHANNELS     0             // device depended
#define DEVICE_SAMPLE_RATE  0             // device depended

#define DEBUG_SINE_WAVE 0

#define INPUT_SAMPLE_RATE       520833
#define INPUT_FRAMES_THRESHOLD  100

struct audio_s {
    bool                initialized;
    ma_device_config    device_config;
    ma_device           device;

    ma_resampler_config resampler_config;
    ma_resampler        resampler;

    ma_pcm_rb           ringbuffer;

    unsigned int        in_frames_count;
    signed short        in_frames[INPUT_FRAMES_THRESHOLD][2];

#if DEBUG_SINE_WAVE
    ma_waveform_config  sine_config;
    ma_waveform         sine;
#endif
};

static struct audio_s   s_audio = {
    .initialized = false
};

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput;

#if DEBUG_SINE_WAVE
    ma_waveform_read_pcm_frames(&s_audio.sine, pOutput, frameCount, NULL);
#else
    ma_result result;

    ma_uint32   size_in_frames;
    ma_uint32   size_in_bytes;

    void        *pin;
    char        *pout = static_cast<char *>(pOutput);

    size_in_frames = frameCount;
    result = ma_pcm_rb_acquire_read(&s_audio.ringbuffer, &size_in_frames, &pin);

    size_in_bytes = 2 * sizeof(signed short) * size_in_frames;
    ::memcpy(pout, pin, size_in_bytes);
    pout += size_in_bytes;

    result = ma_pcm_rb_commit_read(&s_audio.ringbuffer, size_in_frames);
    frameCount -= size_in_frames;

    // do once more if ring buffer at wrapped state
    if (frameCount != 0) {
        size_in_frames = frameCount;
        result = ma_pcm_rb_acquire_read(&s_audio.ringbuffer, &size_in_frames, &pin);

        size_in_bytes = 2 * sizeof(signed short) * size_in_frames;
        ::memcpy(pout, pin, size_in_bytes);
        pout += size_in_bytes;

        result = ma_pcm_rb_commit_read(&s_audio.ringbuffer, size_in_frames);
        frameCount -= size_in_frames;
    }

#endif
    //(void)pDevice;
    //(void)pOutput;
    //(void)frameCount;
}

void SoundGen_Initialize(WORD volume)
{
    if (s_audio.initialized == false) {
        s_audio.device_config= ma_device_config_init(ma_device_type_playback);
        s_audio.device_config.playback.format   = DEVICE_FORMAT;
        s_audio.device_config.playback.channels = DEVICE_CHANNELS;
        s_audio.device_config.sampleRate        = DEVICE_SAMPLE_RATE;
        s_audio.device_config.dataCallback      = data_callback;
        s_audio.device_config.pUserData         = NULL;

        s_audio.device_config.playback.shareMode = ma_share_mode_shared;
        //s_audio.device_config.noPreSilencedOutputBuffer = MA_TRUE;
        s_audio.device_config.noClip = MA_TRUE;

        ma_result result;
        result = ma_device_init(NULL, &s_audio.device_config, &s_audio.device);
        if (result != MA_SUCCESS) {
            printf("Failed to open playback device.\n");
            return;
        }

        s_audio.resampler_config = ma_resampler_config_init(
            s_audio.device.playback.format,
            s_audio.device.playback.channels,
            INPUT_SAMPLE_RATE,
            s_audio.device.sampleRate,
            ma_resample_algorithm_linear
        );

        result = ma_resampler_init(&s_audio.resampler_config, NULL, &s_audio.resampler);
        if (result != MA_SUCCESS) {
            printf("Failed to initialize resampler.\n");
            return;
        }

        ma_uint32 rb_size = s_audio.device.playback.internalPeriodSizeInFrames * 2; // * s_audio.device.playback.internalPeriods;
        result = ma_pcm_rb_init(
            s_audio.device.playback.format,
            s_audio.device.playback.channels,
            rb_size,
            NULL, NULL,
            &s_audio.ringbuffer
        );
        if (result != MA_SUCCESS) {
            printf("Failed to initialize ringbuffer.\n");
            return;
        }

#if DEBUG_SINE_WAVE
        s_audio.sine_config = ma_waveform_config_init(
            s_audio.device.playback.format,
            s_audio.device.playback.channels,
            s_audio.device.sampleRate,
            ma_waveform_type_sine, 0.2, 440);
        ma_waveform_init(&s_audio.sine_config, &s_audio.sine);
#endif

        s_audio.in_frames_count = 0;
        ::memset(&s_audio.in_frames, 0, sizeof(s_audio.in_frames));

        result = ma_device_start(&s_audio.device);
        if (result != MA_SUCCESS) {
            printf("Failed to start playback device.\n");
            ma_device_uninit(&s_audio.device);
            return;
        }

        s_audio.initialized = true;
    }
}

void SoundGen_Finalize()
{
    if (s_audio.initialized == true) {

        ma_result result;
        result = ma_device_stop(&s_audio.device);

        ma_resampler_uninit(&s_audio.resampler, NULL);

        ma_pcm_rb_uninit(&s_audio.ringbuffer);

        ma_device_uninit(&s_audio.device);
#if DEBUG_SINE_WAVE
        result = ma_waveform_uninit(&s_audio.sine);
#endif

        s_audio.initialized = false;
    }
}

void SoundGen_SetVolume(WORD volume)
{

}

void SoundGen_SetSpeed(WORD speedpercent)
{
    if (s_audio.initialized == false)
        return;

    unsigned int speedprc = speedpercent;
    if (speedprc < 25)    speedprc = 25;
    if (speedprc > 1000)  speedprc = 1000;

    ma_uint32 in_sample_rate = (INPUT_SAMPLE_RATE * speedprc) / 100;

    ma_result result;
    result = ma_resampler_set_rate(&s_audio.resampler, in_sample_rate, s_audio.device.sampleRate);
}

void CALLBACK SoundGen_FeedDAC(unsigned short L, unsigned short R)
{
    if (s_audio.initialized == false)
        return;

    s_audio.in_frames[s_audio.in_frames_count][0] = L;
    s_audio.in_frames[s_audio.in_frames_count][1] = R;
    ++ s_audio.in_frames_count;

    const ma_uint32 bytes_per_frame = 2 * sizeof(signed short);

    if (s_audio.in_frames_count >= INPUT_FRAMES_THRESHOLD) {
        ma_result   result;
        ma_uint64   in_frames_avail = s_audio.in_frames_count;
        ma_uint64   in_frames_processed;
        ma_uint64   out_frames_avail;
        ma_uint64   out_frames_processed;

        char        *pin = reinterpret_cast<char *>(s_audio.in_frames);
        void        *pout;

        for (;;) {
            ma_uint32   rb_frames_avail = -1;
            result = ma_pcm_rb_acquire_write(&s_audio.ringbuffer, &rb_frames_avail, &pout);
            out_frames_avail = rb_frames_avail;

            in_frames_processed = in_frames_avail;
            out_frames_processed = out_frames_avail;
            result = ma_resampler_process_pcm_frames(&s_audio.resampler, pin, &in_frames_processed, pout, &out_frames_processed);

            pin += in_frames_processed * bytes_per_frame;
            in_frames_avail -= in_frames_processed;

            ma_uint32   rb_frames_commit = out_frames_processed;
            result = ma_pcm_rb_commit_write(&s_audio.ringbuffer, rb_frames_commit);
            if (in_frames_avail == 0)
                break;

            ::Sleep(1);
        }

        s_audio.in_frames_count = in_frames_avail;
    }
}

#else
#include "stdafx.h"
#include "emubase\Emubase.h"
#include "SoundGen.h"
#include "Mmsystem.h"

//////////////////////////////////////////////////////////////////////


static void CALLBACK waveOutProc(HWAVEOUT, UINT, DWORD, DWORD, DWORD);

static CRITICAL_SECTION waveCriticalSection;
static WAVEHDR*         waveBlocks;
static volatile int     waveFreeBlockCount;
static int              waveCurrentBlock;

static bool m_SoundGenInitialized = false;

HWAVEOUT hWaveOut;

WAVEFORMATEX wfx;
char buffer[BUFSIZE];

int bufcurpos;


//////////////////////////////////////////////////////////////////////


static void CALLBACK WaveCallback(HWAVEOUT /*hwo*/, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR /*dwParam1*/, DWORD_PTR /*dwParam2*/)
{
    int* freeBlockCounter = (int*)dwInstance;
    if (uMsg != WOM_DONE)
        return;

    EnterCriticalSection(&waveCriticalSection);
    (*freeBlockCounter)++;

    LeaveCriticalSection(&waveCriticalSection);
}

void SoundGen_Initialize(WORD volume)
{
    if (m_SoundGenInitialized)
        return;

    unsigned char* mbuffer;

    size_t totalBufferSize = (BLOCK_SIZE + sizeof(WAVEHDR)) * BLOCK_COUNT;

    mbuffer = static_cast<unsigned char*>(calloc(totalBufferSize, 1));
    if (mbuffer == nullptr)
        return;

    waveBlocks = (WAVEHDR*)mbuffer;
    mbuffer += sizeof(WAVEHDR) * BLOCK_COUNT;
    for (int i = 0; i < BLOCK_COUNT; i++)
    {
        waveBlocks[i].dwBufferLength = BLOCK_SIZE;
        waveBlocks[i].lpData = (LPSTR)mbuffer;
        mbuffer += BLOCK_SIZE;
    }

    waveFreeBlockCount = BLOCK_COUNT;
    waveCurrentBlock   = 0;

    wfx.nSamplesPerSec  = SAMPLERATE;
    wfx.wBitsPerSample  = 16;
    wfx.nChannels       = 2;
    wfx.cbSize          = 0;
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nBlockAlign     = (wfx.wBitsPerSample * wfx.nChannels) >> 3;
    wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;

    MMRESULT result = waveOutOpen(
            &hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)WaveCallback, (DWORD_PTR)&waveFreeBlockCount, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
        return;
    }

    waveOutSetVolume(hWaveOut, ((DWORD)volume << 16) | ((DWORD)volume));

    InitializeCriticalSection(&waveCriticalSection);
    bufcurpos = 0;

    m_SoundGenInitialized = true;
    //waveOutSetPlaybackRate(hWaveOut,0x00008000);
}

void SoundGen_Finalize()
{
    if (!m_SoundGenInitialized)
        return;

    while (waveFreeBlockCount < BLOCK_COUNT)
        Sleep(3);

    for (int i = 0; i < waveFreeBlockCount; i++)
    {
        if (waveBlocks[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(hWaveOut, &waveBlocks[i], sizeof(WAVEHDR));
    }

    DeleteCriticalSection(&waveCriticalSection);
    waveOutClose(hWaveOut);

    ::free(waveBlocks);
    waveBlocks = nullptr;

    m_SoundGenInitialized = FALSE;
}

void SoundGen_SetVolume(WORD volume)
{
    if (!m_SoundGenInitialized)
        return;

    waveOutSetVolume(hWaveOut, ((DWORD)volume << 16) | ((DWORD)volume));
}

void SoundGen_SetSpeed(WORD speedpercent)
{
    DWORD dwRate = 0x00010000;
    if (speedpercent > 0 && speedpercent < 1000)
        dwRate = (((DWORD)speedpercent / 100) << 16) | ((speedpercent % 100) * 0x00010000 / 100);

    waveOutSetPlaybackRate(hWaveOut, dwRate);
}

void CALLBACK SoundGen_FeedDAC(unsigned short L, unsigned short R)
{
    if (!m_SoundGenInitialized)
        return;

    unsigned int word = ((unsigned int)R << 16) + L;
    memcpy(&buffer[bufcurpos], &word, 4);
    bufcurpos += 4;

    if (bufcurpos >= BUFSIZE)
    {
        WAVEHDR* current = &waveBlocks[waveCurrentBlock];

        if (current->dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(hWaveOut, current, sizeof(WAVEHDR));

        memcpy(current->lpData, buffer, BUFSIZE);
        current->dwBufferLength = BLOCK_SIZE;

        waveOutPrepareHeader(hWaveOut, current, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, current, sizeof(WAVEHDR));

        EnterCriticalSection(&waveCriticalSection);
        waveFreeBlockCount--;
        LeaveCriticalSection(&waveCriticalSection);

        while (!waveFreeBlockCount)
            Sleep(1);

        waveCurrentBlock++;
        if (waveCurrentBlock >= BLOCK_COUNT)
            waveCurrentBlock = 0;

        bufcurpos = 0;
    }
}


//////////////////////////////////////////////////////////////////////
#endif
