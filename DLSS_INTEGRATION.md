# Integrating DLSS 4.x (Ray Reconstruction) into Mangetsu Engine

This covers everything outside the build system. The build side is already done
(see [What's already set up](#0-whats-already-set-up)). The rest of this document
describes the source and shader changes, in the order you'd make them.

SDK: **NVIDIA DLSS SDK v310.9.1** (DLSS 4.5 generation). The PDFs in
`third-party/DLSS/doc/` are the source of truth. This document maps them onto
your engine. The two you'll actually need are `DLSS-RR Integration Guide.pdf`
and `DLSS_Programming_Guide_Release.pdf` (RR is defined as a diff on top of the
second one).

---

## 0. What's already set up

| Piece | Where | Notes |
|---|---|---|
| Submodule `third-party/DLSS` @ `v310.9.1` | `.gitmodules` | Marked `shallow = true`. The repo is ~1.4 GB (every platform's DLLs). |
| `option(MANGETSU_DLSS ... ON)` | root `CMakeLists.txt` | `-DMANGETSU_DLSS=OFF` drops the dependency entirely. |
| `dlss` INTERFACE target | `third-party/CMakeLists.txt` | Headers (as SYSTEM, so your `/W4` stays quiet), `nvsdk_ngx_d_dbg.lib` for Debug and `nvsdk_ngx_d.lib` otherwise, and `Vulkan::Vulkan`. |
| `MANGETSU_DLSS` compile definition | via `dlss` target | Guard all DLSS code with `#ifdef MANGETSU_DLSS`. |
| Post-build DLL copy | root `CMakeLists.txt` | Copies `nvngx_dlssd.dll` (RR) and `nvngx_dlss.dll` (SR) into `build/bin/` next to the exe. NGX looks there, **not** in the debugger working directory (repo root). |
| `MANGETSU_DLSS_DLL_FLAVOR` = `dev` / `rel` | CMake cache | `dev` has the watermark and debug overlays. Switch to `rel` for anything you hand to other people. |

The NGX import libs are stamped with `RuntimeLibrary` and `_ITERATOR_DEBUG_LEVEL`
(`/FAILIFMISMATCH`). The target picks the variant that matches CMake's MSVC
defaults (`/MDd` + IDL 2 in Debug, `/MD` + IDL 0 otherwise). I verified this by
linking real NGX calls in Debug, Release and RelWithDebInfo. If you ever set
`MSVC_RUNTIME_LIBRARY` to the static CRT, switch to the `nvsdk_ngx_s*.lib`
variants.

**Re-run CMake** (or just build; `ZERO_CHECK` will do it) so your `build/` picks
this up.

---

## 1. Which DLSS feature: Ray Reconstruction, not Super Resolution

DLSS 4.x contains three separate features:

| Feature | DLL | Use here? |
|---|---|---|
| **Ray Reconstruction (DLSS-RR, "DLSSD")** | `nvngx_dlssd.dll` | **Yes.** It denoises and upscales in one network, and it's built for raw path-traced input. |
| Super Resolution (DLSS-SR) | `nvngx_dlss.dll` | No. It expects an image that's already mostly noise-free. At 1 spp it would treat the noise as detail. NVIDIA's guidance is that an RR integration *is* an SR-style integration with RR switched on, so it's copied anyway. |
| Frame Generation (DLSS-FG, MFG) | `nvngx_dlssg.dll` | Not now. Your app would have to pace presentation itself (or use Streamline). Not copied. |

RR presets as of this SDK: **Default** (recommended), **D**, **E** and **F**
(F is the newest one, and the only one that supports the responsivity mask).
Presets A–C were removed in 310.4. Leave it on Default unless you're
experimenting.

**Requirements:** an RTX GPU (your 5070 Mobile is fine) and driver ≥ 580.00 for
DLSS 4.5 RR. On a desktop RTX 5070, NVIDIA's table puts RR at about 1.5–1.6 ms
for 1080p output in Performance mode, so expect somewhat more on the mobile
part. It uses about 150 MB of VRAM at 1080p.

---

## 2. What changes conceptually

Right now each frame does this:

```
traceRays @ swapchain res ──► radianceImage (running average over _frameNum)
                             └─► post.slang (Reinhard + gamma) ──► swapchain ──► ImGui
```

With RR it becomes:

```
traceRays @ RENDER res  ──► noisy 1-spp radiance   ┐
  (primary hit also writes) diffuse albedo         │
                            specular albedo        ├─► NGX DLSSD evaluate ──► dlssOutput @ OUTPUT res
                            normals + roughness    │                              │
                            linear view depth      │                              ▼
                            motion vectors         ┘                     post.slang (tonemap) ──► swapchain ──► ImGui
```

Three things change in nature, not just in plumbing:

1. **No accumulation in DLSS mode.** RR does its own temporal accumulation and
   needs *this frame's* independent sample. Your running average
   (`radianceImage * frameNum/(frameNum+1) + ...`) has to be bypassed. Keep it
   as a "reference" mode for when DLSS is off. It's still the ground truth to
   compare RR against.
2. **Render resolution ≠ output resolution.** Everything the ray tracer writes
   is at render resolution (for example 960×540 in Performance mode for 1080p).
   Only the DLSS output, post and the swapchain are at output resolution.
3. **The camera must describe itself to DLSS every frame**: a known sub-pixel
   jitter, motion vectors, depth, and surface guides. DLSS can't infer these.

---

## 3. Step by step

### 3.1 Vulkan instance and device extensions (`Engine::initVulkan`)

NGX needs extra Vulkan extensions, and it tells you which ones at runtime. Don't
hard-code them.

1. Fill an `NVSDK_NGX_FeatureDiscoveryInfo`:
   - `SDKVersion = NVSDK_NGX_Version_API`
   - `FeatureID = NVSDK_NGX_Feature_RayReconstruction`
   - `Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id`
   - `Identifier.v.ProjectDesc = { "<a GUID you generate once>", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0.1.0" }`
     (you don't have an NVIDIA-issued app ID, which is what Project ID + CUSTOM is for)
   - `ApplicationDataPath` = a writable directory (NGX puts logs here), e.g. `L"./build/ngx"`
2. **Before** `builder.build()`: call
   `NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &props)`
   and pass each name to `vkb::InstanceBuilder::enable_extension()`.
3. **After** `selector.select_devices()` (you need a `VkPhysicalDevice`), and
   before `vkb::DeviceBuilder`: call
   `NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(_instance, physicalDevice, &info, &count, &props)`
   and enable them with `_vkbData.physicalDevice.enable_extensions_if_present(...)`.
   That keeps a non-NVIDIA or non-RTX device usable with DLSS disabled, as long
   as you remember the result and treat DLSS as unavailable when it returns
   false.

Both query functions use the usual Vulkan two-call pattern (NULL array to get the
count first).

### 3.2 NGX init, capability check, shutdown

Once the device exists (end of `initVulkan`):

- `NVSDK_NGX_VULKAN_Init_with_ProjectID(projectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0.1.0", appDataPath, _instance, _physicalDevice, _device, vkGetInstanceProcAddr, vkGetDeviceProcAddr)`.
  Pass the proc-address functions explicitly. You link the loader directly, so
  they're available.
- `NVSDK_NGX_VULKAN_GetCapabilityParameters(&_ngxParams)`. Use **this** (not
  `AllocateParameters`) as the parameter map for everything, because the
  optimal-settings callback only exists on capability maps.
- Read `NVSDK_NGX_Parameter_SuperSamplingDenoising_Available` (int). If it's 0,
  also read `..._NeedsUpdatedDriver` / `..._MinDriverVersionMajor/Minor` /
  `..._FeatureInitResult` so you can print *why*. Then run with DLSS disabled
  instead of crashing.
- Shutdown order (push onto `_deletionQueue` **after** device creation, so it
  flushes before `vkb::destroy_device`): `NVSDK_NGX_VULKAN_ReleaseFeature(handle)`,
  then `NVSDK_NGX_VULKAN_DestroyParameters(_ngxParams)`, then
  `NVSDK_NGX_VULKAN_Shutdown1(_device)`. Do a `vkDeviceWaitIdle` first. NGX
  keeps GPU resources alive inside the feature.

NGX is not thread-safe, but you're single-threaded, so this doesn't matter now.

Headers to include: `<vulkan/vulkan.h>` **first** (the NGX `_vk` headers use
Vulkan types without including it), then `nvsdk_ngx_vk.h`,
`nvsdk_ngx_helpers_vk.h` and `nvsdk_ngx_helpers_dlssd_vk.h`. For the
optimal-settings helper you also need `nvsdk_ngx_helpers_dlssd_d3d.h`. Despite
the name, `NGX_DLSSD_GET_OPTIMAL_SETTINGS` is API-agnostic (it only touches the
parameter map), and on Windows the D3D headers it pulls in come from the Windows
SDK.

### 3.3 Render resolution vs output resolution

Add `_renderExtent` and `_outputExtent` (the latter is today's
`_swapChainExtent`). Get the render size from

```
NGX_DLSSD_GET_OPTIMAL_SETTINGS(_ngxParams, outW, outH, perfQuality,
                               &renderW, &renderH, &maxW, &maxH, &minW, &minH, &sharpness /*ignore*/)
```

with `perfQuality` ∈ `NVSDK_NGX_PerfQuality_Value_{MaxPerf (Performance), Balanced, MaxQuality (Quality), UltraPerformance, DLAA}`.
RR does **not** support dynamic resolution, so use the optimal size as-is.

Then:
- `cmdTraceRaysKHR(..., _renderExtent.width, _renderExtent.height, 1)` (currently `_swapChainExtent`)
- every ray-tracer output image is created at `_renderExtent`
- the post dispatch stays at `_outputExtent`

### 3.4 New images, heap slots, bindings

Your heap layout is `[ENGINE_IMAGES][swapchain × MAX_SWAPCHAIN_SIZE][HDR][scene images…]`,
and every offset is derived from `ENGINE_IMAGES`. So you bump that constant and
everything after it shifts on its own (including the `sceneImages` mapping in
`initPipelinePathtracer` and the swapchain mapping in `initPipelinePost`). Also
update the `//TODO` comment on `sceneImages` in `pathtracer.slang`.

| Slot | Image | Res | Format | Usage | Written by |
|---|---|---|---|---|---|
| 0 | `radiance` (noisy color) | render | `R16G16B16A16_SFLOAT` (or keep RGBA32F) | STORAGE \| SAMPLED | raygen |
| 1 | `diffuseAlbedo` | render | `R8G8B8A8_UNORM` (**not** sRGB) | STORAGE \| SAMPLED | primary hit |
| 2 | `specularAlbedo` | render | `R8G8B8A8_UNORM` or RGBA16F | STORAGE \| SAMPLED | primary hit |
| 3 | `normalRoughness` | render | `R16G16B16A16_SFLOAT` (xyz = world normal, w = roughness) | STORAGE \| SAMPLED | primary hit |
| 4 | `viewDepth` | render | `R32_SFLOAT` | STORAGE \| SAMPLED | primary hit |
| 5 | `motionVectors` | render | `R16G16_SFLOAT` | STORAGE \| SAMPLED | raygen |
| 6 | `specHitDist` *(phase 2)* | render | `R16_SFLOAT` | STORAGE \| SAMPLED | raygen |
| 7 | `dlssOutput` | **output** | `R16G16B16A16_SFLOAT` | STORAGE \| SAMPLED | NGX |
| 8 | `postInput` | n/a | n/a | n/a | rewritten on toggle (see 3.8) |

→ `ENGINE_IMAGES = 9`.

The NGX resource wrapper needs real `VkImageView` + `VkImage` handles. Your
`Allocator::createImage` already creates `AllocatedImage::imageView`, so you're
covered. Wrap each one with `NVSDK_NGX_Create_ImageView_Resource_VK(view, image, subresourceRange, format, w, h, readWrite)`
(`readWrite = true` only for `dlssOutput`).

On the Slang side, each guide is another `[vk::binding(n, 0)] RWTexture2D<...>`
plus a matching `heapMapping(n, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, slot * imageStride, 0)`.
Give the non-RGBA32F ones an explicit `[vk::image_format("rgba16f" / "rgba8" / "r32f" / "rg16f" / "r16f")]`,
like `post.slang` already does for the swapchain, so you don't depend on
`shaderStorageImageWriteWithoutFormat`.

### 3.5 Shader changes

#### 3.5.1 Push data

Add to `PushConstantsPathtracer` in **both** `src/pushconstants.h` and
`shaders/util.slang` (scalar layout, append at the end so `offsetof(tlas)` stays
put):

- `float2 jitter` (this frame's sub-pixel offset, see below)
- previous frame's `camPos`, `camForward`, `camRight`, `camUp` (for motion vectors)
- `uint rngFrame` (monotonic, see 3.5.3)
- `uint dlssEnabled`

That's about 148 bytes. Check it against
`_deviceProperties.dhProperties.maxPushDataSize`. If it doesn't fit, move the
camera state into a small per-frame buffer and push its address, the same way
`sceneData` works.

#### 3.5.2 Jitter (raygen)

Today, `samplePos += float2(rand(), rand())` gives every pixel its own random
offset. DLSS needs **one global offset per frame** that it's told about.

- CPU: `jitter = Halton(2,3)[frameIndex % N] - 0.5`, in the range [-0.5, 0.5).
  The RR guide asks for **at least 32 phases**. Base phase count 8 × (output/render)² gives
  32 for Performance mode at 2×. Using 64 or more costs nothing.
- GPU: `samplePos = float2(DispatchRaysIndex().xy) + 0.5 + pc.jitter`.
- Eval: `InJitterOffsetX/Y` in **render-resolution pixels**, same axes as the
  motion vectors (+x right, +y down).

The sign convention is the classic thing to get backwards. DLSS describes the
offset as "the jitter applied to the projection matrix", and shifting the
projection by +j moves the *sample point* by −j. So start with
`InJitterOffset = -pc.jitter`. If static geometry looks soft or shimmers, use
the dev DLL's **Ctrl+Alt+F9** (cycles sign/scale variants) and **Ctrl+Alt+F10**
(swaps X/Y) to find the right one empirically. The variants are listed in
`third-party/DLSS/utils/DLSS_Debug_Jitter_Configs.txt`. Then hard-code that one.

Compute the motion vectors from the **unjittered** projection (below) and leave
`NVSDK_NGX_DLSS_Feature_Flags_MVJittered` off.

#### 3.5.3 Random numbers (raygen)

The RR guide is unusually firm here: it wants independent, uncorrelated white
noise across pixels and frames, and it calls out hashes with few BigCrush
failures (xxhash32 and PCG are the examples). Two problems with the current code:

- `seed = DispatchRaysIndex().xy * uint2(1973, 9277) + pc.frameNum * uint2(26699, 7919)`
  is a linear function of (x, y, frame), so seeds can alias across pixels and
  frames. Hash it first, e.g. `seed = pcg2d(uint2(x, y) ^ pcg(rngFrame))` (or
  xxhash32 of `(x, y, frame)`), and keep your existing `rand()` stepping after that.
- `_frameNum` resets to 0 whenever the camera moves (for accumulation). In
  DLSS mode the noise has to keep changing, or RR sees the same noise pattern
  every frame and locks it in as detail. Seed from a separate `rngFrame` that
  only ever increments.

#### 3.5.4 Guide buffers (primary hit)

RR needs these for **every pixel, every frame**. Stale values from an earlier
frame cause artifacts, so every path through raygen/closestHit/miss has to write
them.

Write them **from `closestHit` itself** (it can write to UAVs at
`DispatchRaysIndex()`) instead of growing `HitPayload`. Your last few commits
were about cutting register pressure, and five more values in the payload would
undo that. Add a single "is primary" bit to the payload: raygen sets it before
the loop, and `closestHit` writes the guides and clears the bit. The **alpha-cut
pass-through** case (`pbr.color.a < .2f`) must *not* clear it, so that the first
surface that actually scatters becomes the primary.

Formulas, in terms of what `closestHit` already computes:

| Guide | Value |
|---|---|
| Diffuse albedo | `pbr.color.rgb * (1 - metallic) * (1 - transmissiveness)` |
| Specular albedo | `EnvBRDFApprox2(F0, roughness², NoV)` from the RR guide's appendix (copy it from the PDF), where `F0 = lerp(((ior-1)/(ior+1))², pbr.color.rgb, metallic)` and `NoV = abs(wo.z)` in your tangent frame |
| Normal | world-space **shading** normal after normal mapping, i.e. the `worldNor` you already compute |
| Roughness | `pbr.metallicRoughness.y` as-is. That's glTF perceptual roughness, which is what RR calls "linear roughness" (its own appendix squares it to get α). Pack it into `normalRoughness.w`. |
| View depth | `dot(hitPos - pc.camPos, pc.camForward)` |

For the **emissive early-out** in `closestHit`, still write the guides (normal,
depth, roughness = 1, and a sensible albedo such as the emission color clamped to
[0, 1]) before `return`.

In **`miss`** (and when the camera ray misses), use the guide's sky defaults:
diffuse albedo (0.5, 0.5, 0.5), specular albedo 0, normal 0, roughness 0, and
depth set to a large constant (for example 65504).

**Glass/transmission:** RR warns that noisy guides (e.g. from rough refraction)
make it grainy or ghosty. For transmissive primaries, write the guides of the
glass surface itself with constant values rather than anything sampled through
it.

#### 3.5.5 Motion vectors (raygen, after the loop)

Your scene is static, so motion is camera-only, and you can reproject the
primary hit point with the same basis-vector math raygen uses to *generate* rays
(no matrices needed):

```
P      = primary hit position (or camPos + dir * 1e6 for sky)
d      = P - prevCamPos
z      = dot(d, prevForward)
ndc    = float2(dot(d, prevRight) / (z * tanHalfFov * aspect),
                dot(d, prevUp)    / (z * tanHalfFov))
prevPx = float2((ndc.x + 1) * 0.5 * W, (1 - ndc.y) * 0.5 * H)     // W,H = render res
currPx = float2(DispatchRaysIndex().xy) + 0.5                     // UNjittered
mv     = prevPx - currPx                                          // points to where it WAS
```

That's in render-resolution pixels, so use `MVLowRes = 1` and
`InMVScaleX = InMVScaleY = 1` (never 0). The y flip matches the
`(imageSize.y/2 - samplePos.y)` in your ray generation. When animated objects
come along later, you'll need per-instance previous transforms and the
transformed hit point.

#### 3.5.6 Specular hit distance (phase 2, but don't skip it forever)

RR uses either specular motion vectors or a specular hit distance plus
`pInWorldToViewMatrix` / `pInViewToClipMatrix` to keep reflections stable under
motion. Start without it. Once the basics work, write the length of the second
path segment when the lobe sampled at the primary vertex was specular (metal,
dielectric reflection), and 65504 when that ray escapes. The matrices are
documented as row-major with left multiplication. That's the same memory layout
as a column-major glm matrix used as `M * v`, so `glm::value_ptr(view)` can be
passed through unchanged. Build `view`/`proj` with `glm::lookAt` /
`glm::perspective` from the same camera basis and FOV the shader uses, with no
jitter.

#### 3.5.7 Color output (raygen)

In DLSS mode, write `float4(accumulatedLight, 1)` straight to `radianceImage`
with no running average. Keep the NaN guards you already have, because a single
NaN pixel gets smeared by the temporal history.

### 3.6 Feature creation

On init and on every resize/mode change, inside `_allocator.immediateSubmit(...)`:

```
NVSDK_NGX_DLSSD_Create_Params c{};
c.InDenoiseMode        = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
c.InRoughnessMode      = NVSDK_NGX_DLSS_Roughness_Mode_Packed;   // roughness in normals.w
c.InUseHWDepth         = NVSDK_NGX_DLSS_Depth_Type_Linear;       // you have no raster depth
c.InWidth/InHeight     = render extent
c.InTargetWidth/Height = output extent
c.InPerfQualityValue   = the mode you queried with
c.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR      // linear HDR radiance, tonemapped after
                       | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
NGX_VULKAN_CREATE_DLSSD_EXT1(_device, cmd, 1, 1, &_dlssHandle, _ngxParams, &c);
```

A feature is fixed to its sizes. On resize or a quality change, `vkDeviceWaitIdle`,
`NVSDK_NGX_VULKAN_ReleaseFeature`, recreate the images, then recreate the
feature.

### 3.7 Per-frame evaluation (`Engine::draw`)

After `cmdTraceRaysKHR`, before the post dispatch:

1. **Barriers/layouts.** The guide and radiance images go from ray-tracing
   writes in `GENERAL` to compute-shader reads. The programming guide says
   inputs must be readable as sampled images and the output must be a storage
   image. It doesn't name the layouts, so start with inputs in
   `SHADER_READ_ONLY_OPTIMAL` and `dlssOutput` in `GENERAL`, and let the
   validation layer tell you if NGX disagrees. Transition the inputs back to
   `GENERAL` at the start of next frame's trace. NGX restores whatever states it
   changes before returning.
2. **Evaluate:** fill `NVSDK_NGX_VK_DLSSD_Eval_Params` (zero-init it; it's large):
   - `pInColor`, `pInDiffuseAlbedo`, `pInSpecularAlbedo`, `pInNormals` (the packed normalRoughness), `pInDepth`, `pInMotionVectors`, `pInOutput`
   - `pInRoughness` = null (it's packed)
   - `InJitterOffsetX/Y` (3.5.2), `InMVScaleX/Y = 1`
   - `InRenderSubrectDimensions = { renderW, renderH }`
   - `InReset = 1` on the first frame after scene load, HDR load, resize, mode
     toggle, or a camera teleport through the ImGui Origin/Theta/Phi fields.
     Leave it 0 for normal orbiting.
   - (phase 2) `pInSpecularHitDistance`, `pInWorldToViewMatrix`, `pInViewToClipMatrix`

   then `NGX_VULKAN_EVALUATE_DLSSD_EXT(commandBuffer, _dlssHandle, _ngxParams, &eval)`.
3. **Re-establish your state.** NGX leaves the command buffer's binding state
   undefined for Vulkan (programming guide §5.2.5). Re-bind both descriptor
   heaps (`cmdBindResourceHeapEXT` / `cmdBindSamplerHeapEXT`) before the post
   pipeline bind and push data you already do.

### 3.8 Post pass

`post.slang` binding 0 reads "the HDR image to tonemap". Point its heap mapping at
the `postInput` slot (8) and rewrite that one descriptor (`writeImageDescriptor`)
whenever DLSS is toggled: `dlssOutput`'s view when on, `radiance`'s when off.
Neither the pipeline nor the shader has to change, and `GetDimensions` returns
the output size in DLSS mode, so the bounds check stays correct. ImGui is
unchanged.

### 3.9 UI

Add a DLSS checkbox (disabled if unavailable) and a quality combo (DLAA /
Quality / Balanced / Performance / Ultra Performance). Either change runs the
resize path (3.6) and sets `InReset`. Force `lensRadius = 0` while DLSS is on
(see §4).

---

## 4. Engine-specific gotchas

- **Thin-lens depth of field.** RR's guide says DoF *must* happen after RR, or
  with preset F you can supply a DoF guide (`luminance(after - before)` of a
  blur pass). Your DoF is physically traced into the signal, and it makes every
  primary-hit guide noisy, which RR handles badly. Disable it in DLSS mode, and
  if you want DoF with DLSS later, do it as a post effect on `dlssOutput`.
- **`_frameNum` does two jobs today** (accumulation weight and RNG seed).
  Split it. See 3.5.3.
- **`VK_EXT_descriptor_heap` + NGX is the one real unknown.** NGX predates the
  extension and records its own pipelines with classic descriptor sets into
  your command buffer. Re-binding the heaps afterwards (3.7) should be enough.
  If you see validation errors or garbage output, put NGX in its own command
  buffer: submit trace, then NGX, then post+ImGui, chained with semaphores. Then
  heap and descriptor-set state never share a command buffer.
- **Register pressure.** Write guides from `closestHit` behind a payload bit,
  not through the payload (3.5.4).
- **Device selection** picks `physicalDevices[0]`. If NGX init fails, keep going
  with DLSS disabled rather than throwing.
- **Validation layers in Debug** may report things inside NGX's own work. Check
  the NGX log before assuming it's your bug.

## 5. Debugging tools

- **Ctrl+Alt+F12** (dev DLL): cycles through the inputs RR actually received
  (color, MVs, depth, jitter history, normals, roughness, albedos, spec hit
  dist). **Ctrl+Alt+F11** toggles it fullscreen. Use this first, every time.
- **Ctrl+Alt+F6**: accumulation/jitter debug modes. **Ctrl+Alt+F9 / F10**:
  jitter sign/swap (3.5.2). **Ctrl+Alt+O**: NaN visualization.
- `third-party/DLSS/utils/*.reg`: turn NGX logging (`ngx_log_on.reg`,
  `ngx_log_verbose.reg`), the log window, and the driver's on-screen indicator
  on and off. Logs go to the `ApplicationDataPath` you passed at init.
- Compare against your own accumulation mode at the same camera position. It's
  the ground truth RR is trying to approximate.

## 6. Shipping

- Set `MANGETSU_DLSS_DLL_FLAVOR=rel`. NVIDIA's programming guide says the
  **dev** DLL must never be distributed. The RR guide goes further and tells you
  to contact NVIDIA for a distributable DLL, so read §4 of both guides before
  publishing a build.
- The submodule is only a pointer to NVIDIA's repo, so your GitHub repo doesn't
  redistribute anything. Don't commit the DLLs or `.lib`s into it directly, and
  read `third-party/DLSS/LICENSE.txt` (notably clause 2(e) on open-source
  licensing) before you put a license on the engine.
- Add a line to the README's Third-Party Libraries section.

## 7. Suggested order of work

1. Extensions + NGX init + availability check, with `MANGETSU_DLSS` guards.
   Confirm with an NGX log that it initializes.
2. Split render/output extents, add the images and heap slots, and add the
   `postInput` slot. With DLSS off, everything should look exactly as before.
3. Halton jitter + hashed RNG + monotonic `rngFrame` + no accumulation in DLSS
   mode. Look at the raw noisy output.
4. Create and evaluate the feature with **just** color, depth (all zeros), MVs
   (all zeros) and output. Check that the image comes out upscaled at all.
5. Real depth and motion vectors. Orbit the camera, and fix the jitter sign with
   F9/F10.
6. Albedo/normal/roughness guides. Check each one in the Ctrl+Alt+F12 overlay.
7. Specular hit distance + matrices (phase 2).
