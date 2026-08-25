// SPDX-License-Identifier: ISC OR GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// Shim for upstream's "dsp/digital.hpp" include path. The mock is one header;
// upstream's SDK is a tree, and modules include its subpaths directly.
// Declarations live in rack.hpp -- this only forwards.
#pragma once
#include "../rack.hpp"
