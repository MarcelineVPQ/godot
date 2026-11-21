# NVIDIA NGX (DLSS on Linux)

On Windows this fork reaches DLSS through NVIDIA Streamline (`drivers/streamline`). Streamline
does not support Linux, so on Linux DLSS talks to the NVIDIA NGX SDK directly, through Vulkan:
DLSS Super Resolution upscales rasterized frames, and DLSS Ray Reconstruction denoises and
upscales the path tracer's. Windows builds are unchanged.

## How it works

- NGX is opt-in: `scons platform=linuxbsd use_ngx=yes` (x86_64, Vulkan). SConstruct then
  downloads the files it needs from NVIDIA's DLSS SDK at a pinned commit (tag `v310.9.16`,
  commit `9981c6350dc58c0ce21f024fd65e31e851c31689` of https://github.com/NVIDIA/DLSS) into
  `thirdparty/ngx/`, which git ignores, and checks each file's SHA-256. If the download fails,
  the build stops and says so. `scons -c` and `--help` never download. For offline builds, put
  the files in `thirdparty/ngx/` yourself and add `fetch_ngx=no`.
- The build links `libnvsdk_ngx.a` and installs `libnvidia-ngx-dlss.so.310.9.16` (Super
  Resolution), `libnvidia-ngx-dlssd.so.310.9.16` (Ray Reconstruction) and NVIDIA's licence, as
  `NVIDIA_RTX_SDK_LICENSE.txt`, into `bin/` as build targets (`scons -c` removes them, and an
  SDK bump replaces them). It also writes a marker `<binary>.ngx` next to each binary built with
  NGX. At runtime NGX loads the libraries from the folder of the Godot executable.
- Exporting for Linux with a template that has its marker (`<template>.ngx`) copies every
  `libnvidia-ngx-dlss*.so*` found next to the template, with `NVIDIA_RTX_SDK_LICENSE.txt`, next
  to the game, and writes `NVIDIA_NOTICE.txt`. A template without the marker exports none of
  them. So keep those files, and the marker renamed after the template, next to export templates
  built with NGX; if the libraries are missing, the export warns and the game runs without DLSS.
- `ngx_context.cpp` adds the Vulkan extensions NGX asks for, starts NGX on NVIDIA GPUs and checks
  that each feature is available. If one is not (another GPU vendor, an old driver, or its
  library missing), it stays off and a warning says why: without Ray Reconstruction the path
  tracer runs without a denoiser.
- `servers/rendering/renderer_rd/effects/dlss_ngx.cpp` feeds NGX the same inputs the Streamline
  path feeds Streamline: colour, depth, motion vectors and jitter, plus for Ray Reconstruction
  the four guide buffers and the camera matrices. DLSS works out the exposure itself.

To use it, set the project's 3D scaling mode to DLSS. For the path tracer, also set the
Environment's path tracing denoiser to DLSS Ray Reconstruction. The presets come from the same
project settings as on Windows; Linux offers DLSS presets F, J, K, L and M and Ray
Reconstruction presets D, E and F, the ones NGX 310.9.16 doesn't mark "do not use".

## Why the SDK is not in this repository

The NVIDIA RTX SDK licence does not allow the SDK to be made subject to an open-source licence,
and this repository is MIT. So no NGX header or library is ever committed; the build fetches them
from NVIDIA's own repository instead. NVIDIA's licence (`thirdparty/ngx/LICENSE.txt` after a
build) lets an application ship the DLSS library with it under the licence's conditions, which
include notifying NVIDIA before a commercial release and listing the library's third-party notices.

## Licence and releases

A binary built with NGX is not MIT only: it links `libnvsdk_ngx.a`, contains code from the SDK's
headers and loads NVIDIA's DLSS libraries, all under the NVIDIA RTX SDKs licence (see the
`thirdparty/ngx/*` entry in `COPYRIGHT.txt`). `drivers/ngx/*.cpp` and `dlss_ngx.cpp` hold no
NVIDIA source code; they call the SDK's API.

Decided for this fork: its public binaries (the CI build and every release zip) are built
without NGX (`use_ngx=no`), so they are MIT and NVIDIA-free. A build with NGX is made only to
ship a product, and its release checklist is:

1. Ship `thirdparty/ngx/LICENSE.txt` and the notice "This software contains source code provided
   by NVIDIA Corporation." with it (the Linux export does both).
2. Notify NVIDIA before a commercial release (supplement section 4 of the licence:
   https://developer.nvidia.com/sw-notification, with company, publisher and developer, SDK,
   application name, platform, ship date and a link).
3. Attribute DLSS and show the NVIDIA marks on the splash screen, the about box and the credits
   (supplement section 7.1(b)).

## Known validation messages inside NGX

With the Khronos validation layer and synchronization validation on (driver 615.71.09, DLSS-D
310.9.16, layer 1.4.341), every frame that evaluates DLSS Ray Reconstruction reports two messages on
an image NGX creates and names itself, `nv.ngx.dlssd.resource`. The engine never creates, binds or
transitions that image, so they cannot be fixed here; nothing visible goes wrong.

- `SYNC-HAZARD-WRITE-AFTER-WRITE`: inside Evaluate, NGX clears the image right after its own layout
  barrier, and the barrier's scope does not include the clear's transfer write.
- `VUID-vkCmdDraw-None-09600`: an NGX draw expects the image in `VK_IMAGE_LAYOUT_GENERAL` while it
  is still `VK_IMAGE_LAYOUT_UNDEFINED`.

Everything else around Evaluate (the inputs, the output, the barriers) validates clean.
