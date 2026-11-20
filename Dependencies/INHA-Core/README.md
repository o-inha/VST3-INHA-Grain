# VST3 INHA Core

Shared native C++ infrastructure for the Windows-only INHA VST3 plug-ins.

The current shipping plug-ins—not the former Figma files—define the visual and behavioural
baseline. Core turns that baseline into named, tested source code so new and existing
plug-ins can adopt later UI, interaction and activation changes consistently.

## Included in 0.1.2

- The common INHA colour palette.
- Normal, fine, translated and symmetric drag calculations.
- Shift = fine, Ctrl = translate a range, Alt = resize a range symmetrically.
- Normalized zero/restore actions for clickable control labels and values.
- Linear, logarithmic and power/progressive parameter curves.
- Windows VST3 bundle resource lookup.
- The common offline-tolerant licence manager, configured by product metadata.
- CMake targets `INHA::Core` and `INHA::License`.
- A tested Windows release-packaging script with a consistent artifact layout.
- Dependency-free behavioural tests.

Core is compiled into each `.vst3`; it is not a runtime DLL.

## Consuming Core

```cmake
add_subdirectory(Dependencies/INHA-Core)
include(Dependencies/INHA-Core/cmake/InhaPlugin.cmake)

add_library(MyPlugin MODULE ...)
inha_attach_core(MyPlugin)
```

Existing private repositories vendor a pinned snapshot so their GitHub Actions builds do
not depend on a shared private-repository token. See `docs/MIGRATION.md`.

## Boundaries

Core owns brand-wide infrastructure and behaviour. A plug-in continues to own its DSP,
layout, parameters, state migrations, CIDs, version, product ID, logo and unique assets.
