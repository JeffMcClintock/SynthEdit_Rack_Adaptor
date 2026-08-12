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
cp <Fundamental>/res/Unity.svg  modules/Unity/res/
```

Do not edit `vcv/Unity.cpp`. Ever. If it does not compile, that is a gap in
the mock (step 5).

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
| `createLight<MediumLight<RedLight>>(...)` | light widgets, size classes wrapping colour classes |
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

Some things are not gaps but design limits, listed in the README: no lights
rendering, no custom widgets, mono only, and `dataToJson`/`dataFromJson` are
inert (module state that is not a parameter does not persist). If a module
leans on those, it will compile and run but behave incompletely — which is
worth knowing before you spend an afternoon on why an LED never lights.
