// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// dr_wav's API — STUBBED, deliberately and visibly.
//
// dr_wav is David Reid's public-domain single-header WAV reader/writer. Rack
// vendors it; WTVCO and WTLFO use it to read a .wav into a wavetable and to
// write one back out.
//
// Nothing reaches it here: the only route to drwav_init_file() is through a
// file dialog, and compat/osdialog.h reports every dialog as cancelled. So this
// header exists to satisfy the include and the call sites, and each entry point
// reports failure — which is the branch the modules take for an unreadable
// file.
//
// TO MAKE IT REAL: drop the genuine dr_wav.h in beside this file (it is one
// header, public domain, from mackron/dr_libs) and delete this. Only then does
// implementing osdialog_file() buy you anything.

#pragma once

#include <cstddef>
#include <cstdint>

typedef int          drwav_bool32;
typedef uint64_t     drwav_uint64;
typedef uint32_t     drwav_uint32;
typedef int16_t      drwav_int16;
typedef unsigned int drwav_result;

#define DRWAV_TRUE  1
#define DRWAV_FALSE 0

#define DR_WAVE_FORMAT_PCM        0x1
#define DR_WAVE_FORMAT_IEEE_FLOAT 0x3

typedef enum { drwav_container_riff, drwav_container_w64, drwav_container_rf64 } drwav_container;

typedef struct
{
	drwav_container container;
	drwav_uint32 format;
	drwav_uint32 channels;
	drwav_uint32 sampleRate;
	drwav_uint32 bitsPerSample;
} drwav_data_format;

typedef struct
{
	drwav_uint32 channels;
	drwav_uint32 sampleRate;
	drwav_uint64 totalPCMFrameCount;
} drwav;

// DRWAV_FALSE is "could not open", which every call site already handles.
static inline drwav_bool32 drwav_init_file(drwav*, const char*, const void*) { return DRWAV_FALSE; }        // STUB
static inline drwav_bool32 drwav_init_file_w(drwav*, const wchar_t*, const void*) { return DRWAV_FALSE; }   // STUB
static inline drwav_bool32 drwav_init_file_write(drwav*, const char*, const drwav_data_format*, const void*) { return DRWAV_FALSE; }   // STUB
static inline drwav_bool32 drwav_init_file_write_w(drwav*, const wchar_t*, const drwav_data_format*, const void*) { return DRWAV_FALSE; }   // STUB

static inline drwav_uint64 drwav_read_pcm_frames_f32(drwav*, drwav_uint64, float*) { return 0; }            // STUB
static inline drwav_uint64 drwav_write_pcm_frames(drwav*, drwav_uint64, const void*) { return 0; }          // STUB
static inline drwav_result drwav_uninit(drwav*) { return 0; }                                               // STUB

static inline void drwav_f32_to_s16(drwav_int16* out, const float* in, size_t count)
{
	// The one function here that does its job: it is pure arithmetic with no
	// file behind it, so there is nothing to stub.
	for (size_t i = 0; i < count; ++i)
	{
		float v = in[i];
		v = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
		out[i] = (drwav_int16)(v * 32767.f);
	}
}
