# Project foundations

This project follows INHA Template's native plug-in structure, standalone DSP
engine, product configuration, unique VST3 identities, version declaration,
Core attachment, and Windows packaging checklist.

- INHA Template: `a91624ed025dc6954479f88b68ddff6d08c25fa1`
- INHA Core 0.1.2: `7ca596eb56fb3d369371c4dc2ee1d4476cfc0ab7`
- INHA Tear native editor and automation reference:
  `d5d7488ea44c45ffe7b28c752ef9eac1977f2dc3`
- INHA Ryoji reference: `1c16f7cf4e2acc8d0d7654b90df743421ba3d0e6`
- INHA Stutter reference: `30c96af9ae32a417c1aca3c6507e07fdeb98f730`

Core is vendored unchanged at Dependencies/INHA-Core, excluding its own
GitHub workflow. The shared license manager, interaction contract, parameter
curves, theme colors, Windows resource helpers, and package script remain
the authoritative shared implementation.

The native editor adapts the shipping Tear editor's controls, typography,
header/footer layout, settings icons and appearance preferences. Grate's logo
is the artwork supplied by the product owner. Font and wood textures are the
existing INHA resources. The audio engine is a new implementation for Grate.

Steinberg's SDK is fetched at build time at tag `v3.7.14_build_55`, including
submodules, and is not committed in this repository. Its upstream license
applies to the SDK.

# Behavioral research

- https://www.deltasoundlabs.com/product/stream/
- https://www.deltasoundlabs.com/wp-content/uploads/2020/11/Delta-Sound-Labs-Stream-Version-1.1-Manual.pdf
- https://www.kvraudio.com/product/stream-by-delta-sound-labs

The linked listings and supplied screenshot describe legacy STREAM 1.3.1.
The official legacy manual explains that REC writes incoming audio into a
circular buffer, OVDUB layers input over buffered material, and feedback below
one gradually attenuates previous material on overdub passes. Voices are
simultaneous grains, spread their overlap/phase relationship, and LIN/JIT
randomizes voice playback positions. The exact internal DSP is unpublished.

Grate follows the requested new design, rather than reproducing STREAM's
controls, artwork, code, feedback path or proprietary algorithms.
