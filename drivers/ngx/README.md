# NVIDIA NGX (DLSS Ray Reconstruction on Linux)

On Windows this fork reaches DLSS through NVIDIA Streamline (`drivers/streamline`). Streamline
does not support Linux, so on Linux the path tracer's DLSS Ray Reconstruction denoiser talks to
the NVIDIA NGX SDK directly, through Vulkan. Windows builds are unchanged.

## How it works

- `scons platform=linuxbsd` (x86_64, Vulkan) has `use_ngx=yes` by default. SConstruct downloads
  the files it needs from NVIDIA's DLSS SDK at a pinned commit (tag `v310.9.16`, commit
  `9981c6350dc58c0ce21f024fd65e31e851c31689` of https://github.com/NVIDIA/DLSS) into
  `thirdparty/ngx/`, which git ignores. If the download fails, the engine builds without NGX,
  as it does for Aftermath. `use_ngx=no` turns it off.
- The build links `libnvsdk_ngx.a` and copies `libnvidia-ngx-dlssd.so.310.9.16` (the Ray
  Reconstruction library) into `bin/`. At runtime NGX loads it from the folder of the Godot
  executable, so ship it next to the executable.
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
