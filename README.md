# SynthEdit Rack Adaptor

Run [VCV Rack](https://vcvrack.com/) modules as SynthEdit / GMPI plugins, by
compiling each module's **own source, unmodified**, against a mock of Rack's
API. No DSP is rewritten.

A ported module's extra source code is two lines:

```cpp
#include "RackModule.h"
#include "vcv/Fade.cpp"   // the module's own source, byte-for-byte
```

**This repository contains no VCV Rack code and no VCV artwork.** You supply
the module sources you want to port.

**[PORTING.md](PORTING.md)** walks a real module through end to end — what to
copy, what to write, and what to do when it does not compile.

---

## Licensing — read this before you build anything

**The adaptor is GPL-3.0-or-later** (see [LICENSE](LICENSE)), and so is
anything you build with it.

That is deliberate. VCV Rack modules are typically GPL-3.0-or-later, so a
binary combining one with this adaptor is a combined work that must be
distributed under GPL-3.0-or-later with corresponding source.

If you distribute a plugin built with this, you must:

- release it under GPL-3.0-or-later,
- provide the corresponding source, including your changes,
- keep the copyright and licence notices intact.

### Artwork is a separate question, and usually a stricter one

Panel graphics frequently carry different terms from the code they ship
beside, and they are commonly the more restrictive of the two. VCV's own
Fundamental panels are **CC BY-NC-ND 4.0** — no commercial use, no shared
derivatives — and carry VCV's logo and trademarked name.

[Cardinal](https://github.com/DISTRHO/Cardinal), which has done this properly
at scale, states the rule plainly:

> just because a plugin/module is open-source, it does not mean that it can be
> included in Cardinal. Many modules have very strict license terms on the use
> of its artwork, or the code can have a license not compatible with Cardinal.

Their working checklist, which applies equally here:

- `GPL-3.0-only` code cannot be combined into a GPLv3-or-later binary.
- Artwork marked "used and distributed with permission" means permission was
  granted to *that project*, not to you. Ask for your own.
- Artwork prohibiting derivatives cannot be recoloured or restyled — dark mode
  included — without explicit permission.

Check each module's own LICENSE, for code **and** artwork, before you
distribute anything.

### Private use

Both licences draw their line at *sharing*, not at making or using. GPL-3
§2: "You may make, run and propagate covered works that you do not convey,
without conditions". CC BY-NC-ND 4.0 §2(a)(1) expressly grants the right to
"produce and reproduce, but not Share, Adapted Material for NonCommercial
purposes only" — the NoDerivatives term forbids *sharing* adaptations, not
making them.

Building these for your own non-commercial use, artwork included, is fine.
"NonCommercial" is about purpose, so use inside a business toward a product
you intend to sell is not private use; and private ends at the first copy you
hand to anyone.

None of this is legal advice.

### Not affiliated with VCV

This is an independent compatibility layer. It is not a fork of VCV Rack, is
not endorsed by or affiliated with VCV, and claims no connection to it. "VCV"
and "VCV Rack" are VCV's marks, used here only to say what the adaptor is
compatible with. No VCV branding is shipped in this repository.

---

## What it generates for you

A working GMPI plugin:

- **Pins**, named as the module named its ports, ordered
  inputs → outputs → parameters
- **Parameters**, with the ranges and defaults from `configParam()`
- **Patch points**, positioned from the module's `ModuleWidget`
- **The panel**, drawn from the SVG the module names in `createPanel()`
- **Knobs**, drawn and dragged, from the same widget — cap, rim and pointer,
  because Fundamental's panel SVGs carry no knob artwork (VCA's has not one
  circle in it; Rack composites a component SVG at runtime)
- **Jacks**, drawn VCV-style and sized from the widget's declared size
- **The context menu**, from `appendContextMenu()` — each index-pointer option
  becomes an `enum` parameter and a right-click submenu
- **Lights**, drawn from the widget's own base colours and driven by the DSP
- **Whatever the module draws itself** — its `draw()` and `drawLayer()` run,
  through a nanovg shim over gmpi_ui (`RackNanoVg.h`)

Not yet supported: preset save/load beyond parameters (`dataToJson` is an inert
stub), polyphony beyond mono, and pointer events into custom widgets
(`onButton`, `onDragHover`, … are declared and overridable but not dispatched).

**A display fed by DSP state needs one line in the port.** The editor and the
processor own separate module instances and only parameter values cross between
them. A display fed by knobs therefore draws correctly on its own; one fed by
what `process()` computed needs its state shipped across, and the adaptor cannot
guess which members that is. `RACK_DISPLAY_STATE` names them:

```cpp
#include "RackModule.h"
#include "vcv/Scope.cpp"

RACK_DISPLAY_STATE(&Scope::pointBuffer, &Scope::channelsX,
                   &Scope::channelsY,   &Scope::bufferIndex)
```

The module type is deduced, every member is checked trivially copyable, and the
bytes travel on a private blob parameter at display rate. The pin is generated
only for a module that declares state. See `RackDisplayState.h`.

## Pieces

- **`rack/rack.hpp`** — a mock of Rack's `rack.hpp`: the smallest set of
  declarations that lets module sources compile unmodified. Written against
  the API surface those sources use; nothing is copied from Rack. Real where
  it matters (`simd::float_4`, `Param`/`Port` storage, the `config*()`
  metadata capture, `IIRFilter`, `dsp::convert`); inert stubs marked `MOCK`
  for everything host-side.
- **`compat/`** — third-party headers a Rack module may include directly and
  that this project does not otherwise depend on. `samplerate.h` is a real
  linear-interpolation resampler (libsamplerate's API, one channel);
  `osdialog.h` and `dr_wav.h` are stubs that report "cancelled" and "could not
  open", which is a path the modules already handle. Each header explains at
  the top what it does, what it does not, and what to do to make it real.
- **`RackDisplayState.h`** — `RACK_DISPLAY_STATE`, the one-line declaration that
  ships a module's DSP state to whatever draws it.
- **`RackPanelLayout.h`** — reads every control's position, size, id and range
  out of the module's own `ModuleWidget`.
- **`RackAdaptor.h`** — `generatePluginXml()` and `RackProcessor`, the generic
  DSP bridge.
- **`RackEditor.h`** — `RackEditor`, the generic GUI: draws the panel, works
  the knobs.
- **`RackAutoRegister.h`** - the hook that makes a module register itself. It
  registers LAZILY, through the adaptor's own factory: a module's
  `createModel()` line runs before anything the port declares after including
  it, so XML generated at that moment could never see a `RACK_DISPLAY_STATE`
  declaration. The factory asks for the XML during the host's plugin scan
  instead, when every static initializer in the DLL has run.
- **`RackFactory.h/.cpp`** - that factory. GMPI's own builds a plugin's XML at
  registration time, which is right for a plugin whose XML is a string literal
  and wrong for one that generates it. Rather than push a niche case into the
  SDK, the adaptor supplies its own and sets `GMPI_DISABLE_FACTORY` so GMPI's
  is compiled out - the same arrangement the VST3 wrapper uses. It is the one
  file here that is compiled rather than included.
- **`RackModule.h`** — the single header a ported module includes.
- **`RackFactoryStatic.cpp`** - the STATIC-HOST replacement for
  RackFactory.cpp, for a SynthEdit-based host that links the ported modules
  into its own binary (TIDE Rack). Such a host already defines the factory
  entry points - SynthEditLib routes `gmpi::RegisterPluginWithXml` into its
  `ModuleFactory()` - so this file only QUEUES each deferred registration;
  the host flushes the queue after static init with
  `rack_adaptor::registerDeferredModules()`. Configure with
  `RACK_ADAPTOR_STATIC_HOST=ON` to get the `SynthEditRackAdaptorStatic` +
  `RackAdaptorStaticRegistration` targets; a binary compiles exactly ONE of
  the two factory files. Linking statically does not change the licensing:
  the host binary becomes GPL-3.0-or-later.

## Configuration

All optional, all set once per plugin (CMake is the natural place), never per
module:

| Macro | Default | |
| ----- | ------- | - |
| `RACK_MODULE_ID_PREFIX` | `"VCV: "` | plugin id is prefix + slug |
| `RACK_MODULE_CATEGORY`  | `"Rack/VCV"` | a category starting `Rack` is what SynthEdit's rack browser scope (`ModuleScope::RackOnly`) lists as rack-compatible |
| `RACK_MODULE_VENDOR`    | `"VCV (ported)"` | |
| `RACK_ADAPTOR_NO_GUI`   | unset | DSP only; skips the editor and its gmpi_ui and tinyxml2 dependencies |
| `RACK_NO_AUTO_REGISTER` | unset | per translation unit: register by hand with `rack_adaptor::registerModule()` |

Pass strings with spaces via a generated header, not `-D` — MSBuild splits its
define list on semicolons and mangles parentheses.

## Contracts worth knowing

- **Volts**: GMPI 1.0 = 10V. Audio pins scale ×10 in / ÷10 out so module
  idioms like `cv / 10.f` see real volts. Parameter pins are raw.
- **`isConnected()` reports what is really patched**, via the host's
  `synthedit::IPinConnection` extension, asked once in `open()` and cached.
  This matters more than it sounds: the adaptor gives every port a buffer, so
  before the extension existed every input read as connected, and a module that
  normalises an unpatched input silently changed behaviour. VCA-1 multiplies its
  level by its CV input whenever that input is "connected", so it passed
  *silence* until something was patched into CV. Seventeen of Fundamental's
  thirty-eight read `isConnected()`.

  `isStreaming()` looks like the answer and is not: it reports time-varying
  audio, so a connected input holding a steady CV reads false and a real patch
  would look unpatched. The extension asks the graph instead. Caching is safe
  because SynthEdit rebuilds the DSP graph when a cable changes, constructing a
  fresh processor — connections cannot move under a live instance. A host that
  does not offer the extension gets the old everything-connected behaviour.
- **Lights travel DSP -> GUI as parameters.** A light's brightness is computed
  in `process()`, on the processor's module instance, and the editor owns a
  different one. Each displayed light therefore gets a private, non-persistent
  parameter written by a DSP out-pin and read by a GUI pin — the same shape
  SynthEdit's own scope and meters use. `private` keeps them out of the host's
  automation list; `ignorePatchChange` and `persistant="false"` stop a blinking
  LED marking the patch dirty or being saved into it. Only lights the panel
  actually shows get one.
- **Context-menu options are per-instance.** The editor and the processor own
  separate module instances, so a module's `&module->panLaw` pointer is only
  valid for the one that produced it. Each side calls `appendContextMenu()` on
  its own module and binds its own pointer; only the parameter value crosses
  between them. `readPanelLayout(model)` therefore leaves `MenuOption::target`
  null — use `readPanelLayout(model, liveModule)` when you need to write.

