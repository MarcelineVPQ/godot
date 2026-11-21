# NVIDIA NGX (DLSS Ray Reconstruction on Linux)

On Windows this fork reaches DLSS through NVIDIA Streamline (`drivers/streamline`). Streamline
does not support Linux, so on Linux the path tracer's DLSS Ray Reconstruction denoiser talks to
the NVIDIA NGX SDK directly, through Vulkan. Windows builds are unchanged.

## How it works

- NGX is opt-in: `scons platform=linuxbsd use_ngx=yes` (x86_64, Vulkan). SConstruct then
  downloads the files it needs from NVIDIA's DLSS SDK at a pinned commit (tag `v310.9.16`,
  commit `9981c6350dc58c0ce21f024fd65e31e851c31689` of https://github.com/NVIDIA/DLSS) into
  `thirdparty/ngx/`, which git ignores, and checks each file's SHA-256. If the download fails,
  the build stops and says so. `scons -c` and `--help` never download. For offline builds, put
  the files in `thirdparty/ngx/` yourself and add `fetch_ngx=no`.
- The build links `libnvsdk_ngx.a` and copies `libnvidia-ngx-dlssd.so.310.9.16` (the Ray
  Reconstruction library) and NVIDIA's licence, as `NVIDIA_RTX_SDK_LICENSE.txt`, into `bin/`. At
  runtime NGX loads the library from the folder of the Godot executable.
- Exporting for Linux copies every `libnvidia-ngx-dlss*.so*` found next to the export template,
  with `NVIDIA_RTX_SDK_LICENSE.txt`, next to the game, and writes `NVIDIA_NOTICE.txt`. So keep
  those files next to export templates built with NGX; without the library, exported games run
  without DLSS and say why.
- `ngx_context.cpp` adds the Vulkan extensions NGX asks for, starts NGX on NVIDIA GPUs and checks
  that Ray Reconstruction is available. If it is not (another GPU vendor, an old driver, or the
  library missing), the path tracer runs without a denoiser and a warning says why.
- `servers/rendering/renderer_rd/effects/dlss_ngx.cpp` feeds NGX the same inputs the Streamline
  path feeds Streamline (colour, depth, motion vectors, the four Ray Reconstruction guide buffers,
  jitter and camera matrices).

To use it, set the project's 3D scaling mode to DLSS and the Environment's path tracing denoiser
to DLSS Ray Reconstruction. Only Ray Reconstruction is offered on Linux, not plain DLSS upscaling.

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
