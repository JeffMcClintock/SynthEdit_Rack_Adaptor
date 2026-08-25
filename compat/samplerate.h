// SPDX-License-Identifier: ISC OR GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// libsamplerate's API, implemented rather than mocked.
//
// Delay includes <samplerate.h> directly and drives a variable-rate conversion
// with it: it reads its history buffer faster or slower than realtime so the
// read index converges on the delay time the knob asks for. Stub that out and
// Delay does not delay — so this is a REAL resampler, not a placeholder.
//
// WHAT IS DIFFERENT FROM libsamplerate: the interpolation is linear. Delay asks
// for SRC_SINC_FASTEST, and libsamplerate would give it a windowed-sinc kernel.
// Linear interpolation is one of libsamplerate's own modes (SRC_LINEAR), so the
// behaviour is right and the contract is honoured; what you lose is stopband
// rejection while the delay time is sweeping, which is audible as a little
// extra grit on fast knob moves and inaudible once the time settles (at a ratio
// of exactly 1 the interpolation is a pass-through).
//
// The converter argument is accepted and ignored, which is why every quality
// setting behaves the same here.
//
// Only 1 channel is implemented, which is all Delay uses. src_new() refuses
// anything else rather than quietly interleaving wrongly.

#pragma once

#include <cmath>
#include <cstdlib>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	SRC_SINC_BEST_QUALITY   = 0,
	SRC_SINC_MEDIUM_QUALITY = 1,
	SRC_SINC_FASTEST        = 2,
	SRC_ZERO_ORDER_HOLD     = 3,
	SRC_LINEAR              = 4,
};

typedef struct SRC_STATE_tag
{
	int    channels;
	int    converter;
	double ratio;

	// Fractional read position carried between calls, so a conversion that
	// stops mid-sample resumes exactly there. Without this, every block would
	// restart at a sample boundary and the output would jitter.
	double pos;
} SRC_STATE;

typedef struct
{
	const float* data_in;
	float*       data_out;

	long input_frames, output_frames;
	long input_frames_used, output_frames_gen;

	int    end_of_input;
	double src_ratio;
} SRC_DATA;

static inline SRC_STATE* src_new(int converter, int channels, int* error)
{
	if (error)
		*error = 0;

	if (channels != 1)
	{
		if (error)
			*error = 1;   // unsupported: see the note above

		return 0;
	}

	SRC_STATE* st = (SRC_STATE*)std::calloc(1, sizeof(SRC_STATE));
	if (!st)
	{
		if (error)
			*error = 2;

		return 0;
	}

	st->channels  = channels;
	st->converter = converter;
	st->ratio     = 1.0;
	st->pos       = 0.0;
	return st;
}

static inline SRC_STATE* src_delete(SRC_STATE* st)
{
	std::free(st);
	return 0;
}

static inline int src_reset(SRC_STATE* st)
{
	if (st)
		st->pos = 0.0;

	return 0;
}

static inline int src_set_ratio(SRC_STATE* st, double ratio)
{
	if (st)
		st->ratio = ratio;

	return 0;
}

// Output frame n reads input at position pos + n/ratio, linearly interpolated.
// Generation stops at whichever runs out first — the output buffer, or the
// input samples needed to interpolate. Whatever is left unconsumed stays in the
// caller's buffer, which is exactly the contract Delay relies on: it advances
// its history buffer by input_frames_used, not by input_frames.
static inline int src_process(SRC_STATE* st, SRC_DATA* d)
{
	if (!st || !d)
		return 1;

	d->input_frames_used = 0;
	d->output_frames_gen = 0;

	if (d->input_frames <= 0 || d->output_frames <= 0 || !d->data_in || !d->data_out)
		return 0;

	const double ratio = (d->src_ratio > 1e-9) ? d->src_ratio : st->ratio;
	if (!(ratio > 1e-9))
		return 0;

	st->ratio = ratio;

	const double step = 1.0 / ratio;   // input frames advanced per output frame
	double pos = st->pos;
	long   gen = 0;

	while (gen < d->output_frames)
	{
		const long i = (long)pos;

		// Needs i and i+1 to interpolate. At end_of_input there is no next
		// block to wait for, so hold the last sample instead of stalling.
		if (i + 1 >= d->input_frames)
		{
			if (!d->end_of_input || i >= d->input_frames)
				break;

			d->data_out[gen++] = d->data_in[d->input_frames - 1];
			pos += step;
			continue;
		}

		const double frac = pos - (double)i;
		d->data_out[gen++] = (float)(d->data_in[i] * (1.0 - frac) + d->data_in[i + 1] * frac);
		pos += step;
	}

	long used = (long)pos;
	if (used > d->input_frames)
		used = d->input_frames;

	st->pos = pos - (double)used;

	d->input_frames_used = used;
	d->output_frames_gen = gen;
	return 0;
}

static inline const char* src_strerror(int error)
{
	switch (error)
	{
	case 0:  return "No error.";
	case 1:  return "Only one channel is implemented.";
	case 2:  return "Out of memory.";
	default: return "Unknown error.";
	}
}

static inline int src_is_valid_ratio(double ratio)
{
	return ratio > 1.0 / 256.0 && ratio < 256.0;
}

#ifdef __cplusplus
}   // extern "C"
#endif
