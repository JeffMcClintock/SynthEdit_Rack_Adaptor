// SPDX-License-Identifier: ISC OR GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// osdialog's API — STUBBED, deliberately and visibly.
//
// Rack ships osdialog for native file and message dialogs. WTVCO and WTLFO use
// it to let you load a .wav as a wavetable. There is no equivalent plumbed
// through the adaptor yet, and a dialog that opened from the audio thread would
// be worse than none.
//
// So every call here answers the way a cancelled dialog does: osdialog_file()
// returns NULL, osdialog_prompt() returns NULL, osdialog_message() returns 0.
// The modules handle that already — it is the same path as a user pressing
// Escape — so WTVCO and WTLFO run on their built-in default wavetable and
// "Load wavetable" does nothing.
//
// TO MAKE IT REAL: implement osdialog_file() against a GMPI file dialog and
// delete the stub. Nothing else in this header is on a path that matters.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { OSDIALOG_INFO, OSDIALOG_WARNING, OSDIALOG_ERROR } osdialog_message_level;
typedef enum { OSDIALOG_OK, OSDIALOG_OK_CANCEL, OSDIALOG_YES_NO } osdialog_message_buttons;
typedef enum { OSDIALOG_OPEN, OSDIALOG_OPEN_DIR, OSDIALOG_SAVE } osdialog_file_action;

typedef struct osdialog_filter_patterns
{
	char* pattern;
	struct osdialog_filter_patterns* next;
} osdialog_filter_patterns;

typedef struct osdialog_filters
{
	char* name;
	osdialog_filter_patterns* patterns;
	struct osdialog_filters* next;
} osdialog_filters;

typedef struct { unsigned char r, g, b, a; } osdialog_color;

static inline osdialog_filters* osdialog_filters_parse(const char*) { return 0; }   // STUB
static inline void osdialog_filters_free(osdialog_filters*) {}                      // STUB

// NULL is "the user cancelled", which is the branch the modules already take.
static inline char* osdialog_file(osdialog_file_action, const char*, const char*, osdialog_filters*)
{ return 0; }                                                                       // STUB

static inline int  osdialog_message(osdialog_message_level, osdialog_message_buttons, const char*) { return 0; }   // STUB
static inline char* osdialog_prompt(osdialog_message_level, const char*, const char*) { return 0; }               // STUB
static inline int  osdialog_color_picker(osdialog_color*, int) { return 0; }                                      // STUB

#ifdef __cplusplus
}   // extern "C"
#endif
