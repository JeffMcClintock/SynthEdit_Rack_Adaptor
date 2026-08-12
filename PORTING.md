# Porting a module, step by step

A worked example, start to finish, using VCV Fundamental's **Unity** — a
6-into-1 mixer with two channels, VU lights, a switch per channel and an on/off
context-menu option. It was ported in two build attempts and needed no changes
to its own source.

The whole idea: **you never edit the module.** Its `.cpp` goes in byte-for-byte
and stays that way, so re-syncing with upstream is a copy. When something does
not compile, you add it to the mock, and the next module that needs it gets it
free.

---

## 1. Copy the module's source and panel in, unchanged

```bash
mkdir -p modules/Unity/vcv modules/Unity/res
cp <Fundamental>/src/Unity.cpp  modules/Unity/vcv/
cp <Fundamental>/src/plugin.hpp modules/Unity/vcv/
cp <Fundamental>/res/Unity.svg  modules/Unity/res/
```

Do not edit `vcv/Unity.cpp`. Ever. If it does not compile, that is a gap in
the mock (step 5).

**`src/plugin.hpp` comes too, and it is not the same file as Rack's.** Every
Rack plugin has its own project header of that name, sitting between Rack and
its modules: it does `#include <rack.hpp>`, then declares that plugin's shared
widgets and its `extern Model*` list. Fundamental's declares `DigitalDisplay`,
`ChannelDisplay`, `YellowBlueLight`, `VCVBezelBig` and friends. That header is
module source — it belongs to the plugin, not to Rack — so it is copied in
alongside the modules and included from the plugin's own `vcv/` directory.

The adaptor deliberately does **not** define any of those types. Guessing at a
`YellowBlueLight` here would shadow the real one and silently diverge from what
the module actually draws.

## 2. Write the module source file — two lines

`modules/Unity/Unity.cpp`:

```cpp
#include "RackModule.h"
#include "vcv/Unity.cpp"
```

That is the entire file. `RackModule.h` must come first — it declares the
registration machinery that the module's own `createModel<>()` line calls.

## 3. Add the CMakeLists

`modules/Unity/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.30)

project(Unity)

gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_DSP HAS_GUI
    FORMATS_LIST GMPI
    SOURCE_FILES
        Unity.cpp
        res/Unity.svg
)

source_group(vcv FILES vcv/Unity.cpp)

foreach(tgt ${PROJECT_NAME})
    if(TARGET ${tgt})
        # So the module's own `#include "plugin.hpp"` finds the plugin's
        # project header sitting next to it in vcv/.
        target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vcv)
        target_link_libraries(${tgt} PRIVATE gmpi_tinyxml2 SynthEditRackAdaptor)
        rack_module_resources(${tgt} ${CMAKE_CURRENT_SOURCE_DIR}/res)
    endif()
endforeach()
```

`vcv/Unity.cpp` is deliberately **not** in `SOURCE_FILES` — `Unity.cpp`
`#include`s it. Listing it as well compiles it twice, and MSBuild cannot keep
two objects of the same basename apart: it emits MSB8074 and links whichever it
feels like.

## 4. Register it and build

```cmake
add_subdirectory(Unity)     # in modules/CMakeLists.txt
```

```bash
cmake -B build -DGMPI_SDK_FOLDER_OVERRIDE=... -DRACK_ADAPTOR_FOLDER_OVERRIDE=...
cmake --build build --config Debug
```

**Close SynthEdit first.** A running instance holds the `.gmpi` open and the
post-build copy fails — and a failed copy still leaves a perfectly good binary
in `build/`, so you will happily go on testing the old one. Check the two
timestamps match before believing anything.

---

## 5. When it does not compile

This is the interesting part, and it is normal. Unity's first build produced
about a hundred errors, all cascading from one missing macro. Work from the
**first** error, not the count.

Everything below is what Unity actually needed, and it is now in the mock:

| What the module used | What was added |
| -------------------- | -------------- |
| `ENUMS(IN_INPUTS, 2*6)` | the `ENUMS` macro — declares the name plus a `_LAST` entry so the next enumerator lands after the block |
| `configSwitch(id, min, max, def, name, {labels})` | forwards to `configParam`; labels not surfaced yet |
| `dsp::ClockDivider` | real — 6 lines, and a stub would make lights never update |
| `dsp::VuMeter2` | real — peak detector with exponential decay |
| `string::f("Channel %d #%d", ...)` | printf helper |
| `lights[n].setBrightness(v)` | added to `Light` |
| `createLight<MediumLight<RedLight>>(...)` | light widgets, size classes wrapping colour classes — and they draw, see below |
| `createParam<CKSS>(...)`, `createInput<...>` | the **non-centred** `create*` forms |
| `json_boolean` / `json_boolean_value` | inert, like the rest of the json stubs |
| `void onReset() override` | the no-argument form; the base now forwards to it |

Two rules for what you add:

- **Implement the small exact things.** `ClockDivider` is six lines and
  `VuMeter2` about fifteen. Stubbing them would not be less code, and it would
  give the module *wrong* behaviour rather than none.
- **Stub the host-side things and mark them `MOCK`.** Widgets, menus, json,
  asset paths. They compile and do nothing.

### One subtlety worth knowing

Rack has both centred and top-left placement — `createInputCentered(pos, ...)`
versus `createInput(pos, ...)`. This mock stores the **centre** in `box.pos`
throughout, because that is what patch points, jack drawing and knob
hit-testing all need. So the non-centred forms convert on the way in.

Get that wrong and nothing fails to build: every control a module places that
way is simply drawn and hit-tested half a widget off.

---

## 6. Check it

Rescan in SynthEdit, then:

- Does the module appear?
- Does the panel draw, at the right size?
- Are the jacks in the right places, and do they take patch cables?
- Do the knobs turn *and change the sound*?
- Does the right-click menu show the module's options, with the current one
  ticked?

Then check what was generated, which is the quickest way to spot a mismatch —
the plugin cache (`Plugin-Cache-16.xml`) holds the XML the adaptor produced.
Unity's:

```xml
<Parameter id="0" datatype="float" name="Channel 1 mode" default="0"/>
<Parameter id="1" datatype="float" name="Channel 2 mode" default="0"/>
<Parameter id="2" datatype="bool"  name="Merge channels 1 &amp; 2" default="0"/>
```

with 16 patch points and 19 audio pins — 12 inputs, 4 outputs, 3 parameter
pins. Every one of those came from Unity's own `config*()` calls and its
`ModuleWidget`. Nothing about Unity is written anywhere in the port.

**Verify DSP by rendering audio, not by listening once.** Feed two *different*
signals in and compare sample-by-sample against what the maths predicts — a
gain and a genuine mix look identical with one input.

---

## What you cannot fix in the mock

Some things are not gaps but design limits, listed in the README: mono only,
`dataToJson`/`dataFromJson` are inert (module state that is not a parameter
does not persist), and pointer events do not reach a custom widget.

The one that catches people is **what a custom display can see**. Its `draw()`
does run — there is a nanovg shim over gmpi_ui — but it runs on the EDITOR's
module instance, which never calls `process()`. Anything drawn from a knob is
live, because parameters cross. Anything drawn from DSP state is not, because
nothing carries it across yet. Scope shows this exactly: panel, graticule and
trigger marker all correct and tracking the knobs, no trace.

Two things worth knowing if you add a control type to the mock:

- **Say whether it is a knob.** `ParamWidget::isKnob` defaults to false, and
  only a knob gets a body and pointer drawn. Scope's `1x2` and `TRIG` are
  latches, and a pointer on a latch reads as a tiny broken knob.
- **A lit button's lamp is not the button.** `VCVLightLatch` is 9mm across and
  the `MediumSimpleLight` inside it is 3.176mm. Paint the lamp at the button's
  size and the whole bezel lights up. The lamp's size and its halo flag come
  from the `TLight` template argument — see `lightAppearanceOf()`. Rack's
  "Simple" variants are the same lamp with no glow.

---

## Lights, and why they are not just another widget

You get them for free — nothing in a port has to mention them — but the shape
is worth understanding, because anything else the DSP wants to show the GUI
has to follow it.

The editor and the processor own **separate module instances**. A light's
brightness is computed in `process()`, which only ever runs on the processor's
copy, so the editor cannot simply read `module->lights[i]`: its own copy is
sitting there untouched. Only parameter values cross between the two.

So each displayed light becomes a private, non-persistent parameter, written
by a DSP out-pin and read by a GUI pin:

```xml
<Parameter id="3" datatype="float" name="Light 2"
           private="true" ignorePatchChange="true" persistant="false"/>
<Audio> <Pin name="Light 2" direction="out" datatype="float" private="true" parameterId="3"/> </Audio>
<GUI>   <Pin name="Light 2"                  datatype="float" private="true" parameterId="3"/> </GUI>
```

`private` keeps it out of the host's automation list; `ignorePatchChange` and
`persistant="false"` stop a blinking LED marking the patch dirty or being
saved into it. It is the same shape SynthEdit's own scope and meters use — see
`modules/Controls/Scope4Gui.cpp` in SynthEditLib.

Three things that are easy to get wrong:

- **An output pin needs an explicit block position inside `subProcess()`.**
  The default is only legal when the processor sits exactly on an event
  boundary, which is true in `onSetPins()` and false throughout `subProcess()`.
  Debug builds assert the moment audio starts. `getBlockPosition()` is the
  offset of the chunk you were handed, not of where you are inside it.
- **Send once per chunk and quantise.** A light is a GUI value at 60Hz, not an
  audio signal. A brightness computed with a smoothing filter never settles
  exactly, so without quantising it resends forever.
- **Invalidate the light, not the panel.** A blinking LED that dirties the
  whole surface repaints the SVG, every jack and every knob several times a
  second.

The colours need no table anywhere: a Rack light widget already states them —
`RedLight` adds `SCHEME_RED`, Fundamental's `YellowBlueLight` adds yellow then
blue — one base colour per consecutive light id from `firstLightId`. That is
the complete description of a bi-colour LED, so the mock's light classes carry
their colours the way Rack's do and the editor reads them straight off.

---

## When a module includes something that is not Rack

A few modules pull in a third-party header directly — Fundamental's Delay does
`#include <samplerate.h>`, and its wavetable oscillators want `<osdialog.h>`
and `"dr_wav.h"`. You have three options, and the choice is worth making
deliberately rather than reaching for the stub every time:

1. **Add the real dependency**, if it is small and permissively licensed.
2. **Implement the API** where the module genuinely depends on the behaviour.
   `compat/samplerate.h` is a real linear-interpolation resampler, because
   Delay reads its history buffer through it — stub that and Delay does not
   delay. Linear rather than sinc is a documented quality difference, not a
   silent hole.
3. **Stub it**, where the feature is out of scope anyway. `compat/osdialog.h`
   reports every dialog as cancelled, which is a path the modules already
   handle, so the wavetable oscillators run on their default table and
   "Load wavetable" does nothing.

Whichever you pick, say so at the top of the header and in the README. The
failure mode to avoid is a stub that looks like an implementation: a module
that builds, loads, and quietly does the wrong thing is worse than one that
does not build.

---

## Doing a whole set

Porting all 38 Fundamental modules took the same loop, repeated: scaffold every
module, build them all, tally the *distinct* compiler errors across the whole
set, fix the most common one, rebuild. Two things that loop teaches:

- **Tally, do not chase.** A hundred errors in one module is usually one
  missing declaration cascading. Sorting `error C####: <message>` by frequency
  across every module points at the real gap far better than reading any single
  log top to bottom.
- **Declaration order inside the mock bites twice.** A member function's return
  type is parsed where it is written, unlike its body — so `PortInfo*
  configInput(...)` has to come *after* `struct PortInfo`. And a namespace of
  `using` aliases has to come after the things it aliases. Both produced
  hundreds of errors that looked like something else entirely.
