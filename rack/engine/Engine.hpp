// SPDX-License-Identifier: ISC OR GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// Shim for upstream's "engine/Engine.hpp" include path.
//
// The mock is one header (rack/rack.hpp); upstream's is a tree. Modules that
// include a Rack SDK subpath directly -- HetrickCV's HetrickUtilities.hpp and
// DSP/HCVTiming.h both include this one -- need the path to resolve. The
// declarations themselves live in rack.hpp, so this only forwards.
#pragma once
#include "../rack.hpp"
