# VirtualProductionSplat — Claude Project Context

## Daily Workflow
- **Build:**  `Tools\build.bat`   (runs preflight → Build.bat, target `VirtualProductionSplatEditor Win64 Development`)
- **Launch:** `Tools\launch.bat`  (runs preflight → editor `-dx12`; DX12/SM5 required for NanoGS compute splat render)

## Pre-flight Checks (`Tools/preflight.py`)
Run manually: `python Tools/preflight.py`
Exit 0 = all clear. Exit 1 = fix FAILs before proceeding.
16 checks: API keys, NanoGS plugin health, UnrealClaude submodule, source hygiene, Python deps, git state.

## Pipeline
Greybox scene in UE5 → **single 360° equirectangular PNG** (cube capture) → WorldLabs Marble (`world_prompt.type`: **`image`**) → Gaussian Splat (.ply) → **NanoGS** plugin in UE5 → Virtual Production / ICVFX stage

## Project Structure
- Content/Greybox/           — greybox scene assets and level
- Content/GaussianSplats/    — imported .ply splat assets / UGaussianSplatAsset
- Content/VirtualProduction/ — ICVFX and nDisplay configs
- Source/VirtualProductionSplat/ — C++ runtime source
- Source/VirtualProductionSplatEditor/ — C++ editor source
- Plugins/NanoGS/            — Gaussian Splat renderer (real-time 3DGS, Nanite-style LOD)
- Plugins/UnrealClaude/      — Claude Code CLI in-editor (submodule: github.com/Natfii/UnrealClaude; actual plugin at `Plugins/UnrealClaude/UnrealClaude/`)
- Saved/GreyboxExports/      — auto-generated **`panorama_360.png`** (equirectangular; gitignored)

## Key Systems

### 1. Greybox Scene (Phase 2)
- Level: Content/Greybox/GreyboxScene.umap
- **`APanoramicCapture360`** (header file `MultiAngleCameraRig.h`): `USceneCaptureComponentCube` → equirectangular PNG via cube readback + ImageWrapper
- **`UGreyboxExporter::ExportAllCameras`**: finds a panoramic actor in the editor world and calls **`Capture360()`**
- Default output: `[ProjectSavedDir]/GreyboxExports/panorama_360.png` (override via **`OutputPath`** on the actor)

### 2. WorldLabs API Integration (Phase 3)
- C++ class: UWorldLabsAPIClient (UObject)
- **Marble REST base:** `https://api.worldlabs.ai/marble/v1` (override via `[WorldLabsAPI] WorldsBaseURL=` if needed)
- **Auth header:** `WLT-Api-Key: <key>` (not Bearer). API key from `Config/DefaultGame.ini` `[WorldLabsAPI]` — never hardcoded
- **Flow (single panorama PNG):**
  1. `POST .../media-assets:prepare_upload` → `PUT` the equirect file to **GCS** `upload_url` (no WLT header on PUT)
  2. `POST .../worlds:generate` with `world_prompt.type` = **`image`**, one `media_asset_id`, and **`image_prompt`** (text)
  3. `GET .../operations/{operation_id}` until `done` (poll every **10s**)
  4. `GET .../worlds/{world_id}` → parse **.ply** download URL from JSON
- Delegates: `OnWorldReady(FString PLYDownloadURL)`, `OnWorldFailed`, `OnPollTick(OperationID, Status)`

### 3. NanoGS Plugin (splat rendering)
NanoGS ships **real public headers** (`NANOGS_API`) — project code links it directly (no reflection).
- **Module dependency:** `NanoGS` in `VirtualProductionSplat.Build.cs` (`PublicDependencyModuleNames`); `NanoGSEditor` in `VirtualProductionSplatEditor.Build.cs` inside `if (Target.bBuildEditor)`.
- **Asset model:** a `.ply` becomes a **`UGaussianSplatAsset`**. An **`AGaussianSplatActor`** (holds a **`UGaussianSplatComponent`**) renders it via `SetSplatAsset(Asset)`; splat size via component `SplatScale` (0.1–10).
- **Runtime / transient import:** `FPLYFileReader::ReadPLYFile` → `NewObject<UGaussianSplatAsset>` → `InitializeFromSplatData(Splats, EGaussianQualityLevel::VeryHigh)`. (`InitializeFromSplatData` is runtime-safe.)
- **Editor / persistent import:** `UGaussianSplatAssetFactory::FactoryCreateFile` (module `NanoGSEditor`, `#if WITH_EDITOR`) → saved asset under `/Game/GaussianSplats`, visible in the Content Browser.
- **Project integration surface:** all splat loading goes through **`FMLSGaussianSplatInterop`** / **`UGaussianSplatImportHelper`** / **`AGaussianSplatImportRunner`** — do not call NanoGS internals from new pipeline code.
  - (`FMLSGaussianSplatInterop` keeps its legacy name for source-compat; it now wraps NanoGS, not the old MLSLabsRenderer.)
- **GPU requirement:** SM5 / DX12 (compute-shader sort + cluster cull). The runner aborts with a toast if feature level < SM5 or shaders are still compiling.

### 4. Virtual Production / ICVFX (Phase 5)
- **`AVPPipelineOrchestrator`**: wires **SceneBuilder, `APanoramicCapture360` (property `CameraRig`), `APanoramicExportRunner` (`ExportRunner`), WorldLabsRunner, SplatImporter, StageSetup**; **Run Full Pipeline** calls **`CapturePanorama()`** / **`Capture360()`**
- **`AVPStageSetup`**: **Step1–4** — find cine camera, add fill lights, post process volume, log summary
- Level: `Content/VirtualProduction/VP_Stage.umap` (manual create)
- nDisplay / LED: Epic Quick Start; **Mac** lacks full nDisplay — use **Win64/Linux** for cluster
- **`AGaussianSplatActor`** as background plate; **`BP_CameraTracker`**: manual Blueprint (not in C++ repo)

### 5. Pipeline Control Panel (Phase 3–4)
- EditorUtilityWidget: EUW_PipelineControl
- Step 1: Export panorama (equirect PNG)
- Step 2: Submit to WorldLabs (upload chain + operation polling every 10s)
- Step 3: Download .PLY (enabled when job complete)
- Step 4: Import Splat into level

## Coding Standards
- Use UPROPERTY / UFUNCTION for all Blueprint-exposed members
- Async HTTP via UE5 HTTP module (no blocking calls on game thread)
- API keys and config via GConfig / DefaultGame.ini only — never hardcoded
- Custom log category: DECLARE_LOG_CATEGORY_EXTERN(LogVPSplat, Log, All)
- Use UE_LOG(LogVPSplat, ...) throughout for pipeline tracing
- TObjectPtr<> preferred over raw pointers for UObject members
- Prefix interfaces with I (e.g. IGreyboxExporter)
- Error handling: retry logic on WorldLabs polling, HTTP timeout handling

## Platform Notes
- Primary target: **Win64** (NanoGS compute render + nDisplay)
- nDisplay / full ICVFX output: Windows / Linux (nDisplay SupportedTargetPlatforms excludes Mac)
- Node.js required for UnrealClaude MCP bridge:
    `cd "Plugins/UnrealClaude/UnrealClaude/Resources/mcp-bridge" && npm install`

## Config — DefaultGame.ini  (gitignored — never commit real keys)
```
[WorldLabsAPI]
APIKey=YOUR_KEY_HERE
; Optional override (must be full https URL):
; WorldsBaseURL=https://api.worldlabs.ai/marble/v1
```

## External References
- WorldLabs Marble API host: https://api.worldlabs.ai
- UnrealClaude Plugin: https://github.com/Natfii/UnrealClaude
- UE5 HTTP Module docs: https://docs.unrealengine.com/5.0/en-US/API/Runtime/HTTP/

## End-to-End Usage (Editor Workflow)

### One-time setup
1. After clone: `git submodule update --init --recursive`
2. Add `APIKey` to `Config/DefaultGame.ini` under `[WorldLabsAPI]`
3. `npm install` in `Plugins/UnrealClaude/UnrealClaude/Resources/mcp-bridge/` (requires Node.js; note double `UnrealClaude` in path)
4. Build (`Tools\build.bat`), then open `Content/Greybox/GreyboxScene.umap`

### Per-session pipeline
Place these actors in the level (or use **`AVPPipelineOrchestrator`**, assign references, then **Run Full Pipeline** / **Log Pipeline Status**):

**Step 1 — Build greybox** — place **`AGreyboxSceneBuilder`** → **Build Greybox Scene**

**Step 2 — Panorama capture + export** — place **`APanoramicCapture360`** → **Capture360**; place **`APanoramicExportRunner`** → **CapturePanorama**
→ **`panorama_360.png`** under `Saved/GreyboxExports/` (unless **`OutputPath`** is set)

**Step 3 — WorldLabs** — place **`AWorldLabsRunner`** → set **World Prompt** → **Submit To World Labs** → **Check Job Status** (auto-poll) → **Download Splat**
→ `.ply` saved under `Content/GaussianSplats/`

**Step 4 — Import splat** — place **`AGaussianSplatImportRunner`** → set **PLY File Path** → **Import PLY Into Level**
→ a **`UGaussianSplatAsset`** is imported (editor factory) and an **`AGaussianSplatActor`** is spawned at the greybox center / runner transform. (`.spz` inputs auto-convert via `ConvertSpzToPly.py`.)

**Step 5 — VP stage** — place **`AVPStageSetup`** → assign **Gaussian Splat Actor** ref → **Step1 Find Camera** → **Step2 Add Fill Lights** → **Step3 Add Post Process** → **Step4 Log Stage Summary**

**Step 6 — Camera tracking (manual)** — create a **`BP_CameraTracker`** Blueprint (pawn + spring arm + camera) or use editor viewport navigation.

### nDisplay / LED volume (Windows / Linux only)
See Epic’s nDisplay Quick Start. Use `Content/VirtualProduction/VP_Stage.umap` as inner-frustum content. A **`CineCameraActor`** labeled **`PrimaryCamera`** (from the greybox builder) is a natural ICVFX camera candidate.

## Key C++ files
| File | Purpose |
|------|---------|
| `Source/VirtualProductionSplat/MultiAngleCameraRig.h/.cpp` | **`APanoramicCapture360`** — cube capture, `Capture360()`, equirect PNG |
| `Source/VirtualProductionSplat/GreyboxExporter.h/.cpp` | `UGreyboxExporter::ExportAllCameras` |
| `Source/VirtualProductionSplat/WorldLabsAPIClient.h/.cpp` | **`UWorldLabsAPIClient`** — HTTP submit / poll / download |
| `Source/VirtualProductionSplat/MLSGaussianSplatInterop.h/.cpp` | **`FMLSGaussianSplatInterop`** — NanoGS bridge (ply→asset→actor) |
| `Source/VirtualProductionSplat/GaussianSplatImportHelper.h/.cpp` | **`UGaussianSplatImportHelper`** — Blueprint spawn wrapper |
| `Source/VirtualProductionSplatEditor/GreyboxSceneBuilder.h/.cpp` | **`AGreyboxSceneBuilder`** |
| `Source/VirtualProductionSplatEditor/GreyboxExportRunner.h/.cpp` | **`APanoramicExportRunner::CapturePanorama`** |
| `Source/VirtualProductionSplatEditor/WorldLabsRunner.h/.cpp` | **`AWorldLabsRunner`** (CallInEditor submit/poll/download) |
| `Source/VirtualProductionSplatEditor/GaussianSplatImportRunner.h/.cpp` | **`AGaussianSplatImportRunner::ImportPLYIntoLevel`** (NanoGS factory import + spawn) |
| `Source/VirtualProductionSplatEditor/VPStageSetup.h/.cpp` | **`AVPStageSetup`** |
| `Source/VirtualProductionSplatEditor/VPPipelineOrchestrator.h/.cpp` | **`AVPPipelineOrchestrator`** |

### WorldLabs Marble API (verify against live API)
- **`world_prompt.type`:** use **`"image"`** for panoramic equirectangular PNGs submitted via `media_asset_id`. Valid API values are **`text`**, **`image`**, **`multi-image`**, **`video`** — there is **no** `panorama` type (422 if used).
- **prepare_upload** → `media_asset.media_asset_id`, `upload_info.upload_url` (+ optional `required_headers` for GCS PUT)
- **worlds:generate** → `operation_id` (or nested under `operation`)
- **operations/{id}** → `done`, `response.world_id` when finished
- **GET worlds/{world_id}** → recursive JSON scan for an `https` URL referencing `.ply`
- Adjust parsing in `OnGenerationResponse` / `OnPollOperationResponse` / `OnFetchWorldResponse` if the live API differs.
- `UWorldLabsAPIClient` must be owned by an `AActor` (valid `UWorld`) so `GetTimerManager()` works for polling.
- Download uses a plain **GET** (signed CDN links); add headers in `DownloadPLYFile` if your tenant requires authenticated download.

## Remaining manual editor-only work
- Create/save **`GreyboxScene.umap`**, **`VP_Stage.umap`** if not present
- Optional Blueprints: **`BP_PanoramicCapture360`**, **`BP_CameraTracker`**, EUW widgets
- WorldLabs: confirm live API field names / auth
- nDisplay / ICVFX production setup on **Win64/Linux**; content packaging for LED
