# SynthEdit Rack Adaptor

Run [VCV Rack](https://vcvrack.com/) modules as SynthEdit / GMPI plugins, by
compiling each module's **own source, unmodified**, against a mock of Rack's
API. No DSP is rewritten.

A ported module's entire source file is two lines:

```cpp
#include "RackModule.h"
#include "vcv/Fade.cpp"   // the module's own source, byte-for-byte
```

The module's own registration line — `createModel<Fade, FadeWidget>("Fade")` —
is what puts a GMPI plugin in the factory. Its `config()` declarations become
the plugin XML; its `ModuleWidget` becomes the patch points and the editor's
knob layout; and the panel it names in `createPanel()` resolves against the
art your build staged.

**This repository contains no VCV Rack code and no VCV artwork.** You supply
the module sources you want to port.

---

## Licensing — read this before you build anything

**The adaptor is GPL-3.0-or-later** (see [LICENSE](LICENSE)), and so is
anything you build with it.

That is deliberate. VCV Rack modules are typically GPL-3.0-or-later, so a
binary combining one with this adaptor is a combined work that must be
distributed under GPL-3.0-or-later with corresponding source. Licensing the
adaptor permissively would not have changed that obligation by one word — it
would only have obscured where it lands. Matching the licence of the code this
is designed to be combined with keeps the answer visible.

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

From the module's own declarations — you write none of this:

- **Pins**, named as the module named its ports, ordered
  inputs → outputs → parameters
- **Parameters**, with the ranges and defaults from `configParam()`
- **Patch points**, positioned from the module's `ModuleWidget`
- **The panel**, drawn from the SVG the module names in `createPanel()`
- **Knobs**, hit-tested and dragged, from the same widget

Not yet supported: context menus, preset save/load (`dataToJson` is an inert
stub), lights, custom widgets, and polyphony beyond mono.

## Pieces

- **`rack/plugin.hpp`** — a mock of Rack's `plugin.hpp`: the smallest set of
  declarations that lets module sources compile unmodified. Written against
  the API surface those sources use; nothing is copied from Rack. Real where
  it matters (`simd::float_4`, `Param`/`Port` storage, the `config*()`
  metadata capture); inert stubs marked `MOCK` for everything host-side.
- **`RackPanelLayout.h`** — reads every control's position, size, id and range
  out of the module's own `ModuleWidget`.
- **`RackAdaptor.h`** — `generatePluginXml()` and `RackProcessor`, the generic
  DSP bridge.
- **`RackEditor.h`** — `RackEditor`, the generic GUI: draws the panel, works
  the knobs.
- **`RackAutoRegister.h`** — the hook that makes a module register itself.
- **`RackModule.h`** — the single header a ported module includes.

## Configuration

All optional, all set once per plugin (CMake is the natural place), never per
module:

| Macro | Default | |
| ----- | ------- | - |
| `RACK_MODULE_ID_PREFIX` | `"VCV: "` | plugin id is prefix + slug |
| `RACK_MODULE_CATEGORY`  | `"VCV"`   | |
| `RACK_MODULE_VENDOR`    | `"VCV (ported)"` | |
| `RACK_ADAPTOR_NO_GUI`   | unset | DSP only; skips the editor and its gmpi_ui and tinyxml2 dependencies |
| `RACK_NO_AUTO_REGISTER` | unset | per translation unit: register by hand with `rack_adaptor::registerModule()` |

Pass strings with spaces via a generated header, not `-D` — MSBuild splits its
define list on semicolons and mangles parentheses.

## Contracts worth knowing

- **Pin order**: inputs in the module's order, then outputs, then parameter
  pins. `generatePluginXml()` and `RackProcessor`'s pin construction both
  follow it; they must never diverge.
- **Patch-point `pinId` is NOT that order.** It indexes SynthEdit's *document*
  pin list, where a module's GUI pins are numbered ahead of its audio pins. So
  with N parameters, audio pin *k* is document pin *N + k*. This bites
  silently: adding an editor renumbers every jack.
- **Include order**: GMPI and gmpi_ui headers come before `plugin.hpp`, which
  ends with `using namespace rack;` as Rack's own does. Afterwards,
  `rack::Rect` makes every `Rect` in `SvgParser.h` ambiguous. `RackModule.h`
  handles this; it matters if you assemble the headers yourself.
- **Static init**: anything written during static initialization and read
  later must be a **function-local** static. An `inline static std::string`
  has its own dynamic initialization, unordered against the write, so the
  assignment lands first and the constructor then wipes it.
- **Volts**: GMPI 1.0 = 10V. Audio pins scale ×10 in / ÷10 out so module
  idioms like `cv / 10.f` see real volts. Parameter pins are raw.
- **`isConnected()`**: the adaptor drives every port, so a module never sees an
  unconnected input; one that normalises to a fallback will read 0V instead.

First consumer:
[VCV_Fundamental_gmpi](https://github.com/JeffMcClintock/VCV_Fundamental_gmpi).
