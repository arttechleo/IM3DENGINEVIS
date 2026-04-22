# VirtualProductionSplat — Claude Project Context

## Pipeline
Greybox scene in UE5 → **single 360° equirectangular PNG** (cube capture) → WorldLabs Marble (`world_prompt.type`: **`image`**) → Gaussian Splat (.ply) → SplatRenderer Plugin in UE5 → Virtual Production / ICVFX stage

## Project Structure
- Content/Greybox/           — greybox scene assets and level
- Content/GaussianSplats/    — imported .ply splat assets
- Content/VirtualProduction/ — ICVFX and nDisplay configs
- Source/VirtualProductionSplat/ — C++ source
- Plugins/SplatRenderer/     — Gaussian Splat .ply renderer (DazaiStudio)
- Plugins/UnrealClaude/      — Claude Code CLI in-editor integration (Natfii)
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

### 3. SplatRenderer Plugin (Phase 4)
- Plugin path: Plugins/SplatRenderer/
- UBT module name: **SplatRenderer** (see `Plugins/SplatRenderer/SplatRenderer.uplugin`)
- Primary actor: **`ACSGaussianActor`** — `LoadPLY(FString)`, `PLYFilePath`, `SplatScale`, optional crop/FX
- Project helpers: `UGaussianSplatImportHelper::SpawnGaussianSplatAt`, `AGaussianSplatImportRunner` (CallInEditor)
- Imported / downloaded `.ply` files typically under `Content/GaussianSplats/` (e.g. after `AWorldLabsRunner::DownloadSplat`)
- Low-level PLY parsing (`CSGaussian::FCSPLYLoader`) stays inside the plugin; project code uses **`ACSGaussianActor` only**

### 4. Virtual Production / ICVFX (Phase 5)
- **`AVPPipelineOrchestrator`**: wires **SceneBuilder, `APanoramicCapture360` (property `CameraRig`), `APanoramicExportRunner` (`ExportRunner`), WorldLabsRunner, SplatImporter, StageSetup**; **Run Full Pipeline** calls **`CapturePanorama()`** / **`Capture360()`** (not multi-camera export)
- **`AVPStageSetup`**: **Step1–4** — find cine camera, add fill lights, post process volume, log summary
- Level: `Content/VirtualProduction/VP_Stage.umap` (manual create)
- nDisplay / LED: Epic Quick Start; **Mac** lacks full nDisplay — use **Win64/Linux** for cluster
- **`ACSGaussianActor`** as background plate; **`BP_CameraTracker`**: manual Blueprint (not in C++ repo)

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
- Primary build: macOS (Apple Silicon) for development
- nDisplay and full ICVFX output targets: Windows / Linux (nDisplay SupportedTargetPlatforms excludes Mac)
- SplatRenderer compiled with deprecation warnings in CSGaussianBuffers.h — upstream issue, do not modify
- Node.js required for UnrealClaude MCP bridge:
    cd "Plugins/UnrealClaude/Resources/mcp-bridge" && npm install

## Config — DefaultGame.ini
[WorldLabsAPI]
APIKey=YOUR_KEY_HERE
; Optional override (must be full https URL):
; WorldsBaseURL=https://api.worldlabs.ai/marble/v1

## External References
- WorldLabs Marble API host: https://api.worldlabs.ai
- SplatRenderer Plugin: https://github.com/DazaiStudio/SplatRenderer-UEPlugin
- UnrealClaude Plugin: https://github.com/Natfii/UnrealClaude
- UE5 HTTP Module docs: https://docs.unrealengine.com/5.0/en-US/API/Runtime/HTTP/

## End-to-End Usage (Editor Workflow)

### One-time setup

1. Add `APIKey` to `Config/DefaultGame.ini` under `[WorldLabsAPI]`
2. `npm install` in `Plugins/UnrealClaude/Resources/mcp-bridge/` (requires Node.js)
3. Open UE5 editor and open `Content/Greybox/GreyboxScene.umap` (create/save the level if it does not exist yet)

### Per-session pipeline

Place these actors in the level (or use **`AVPPipelineOrchestrator`**, assign references to each, then **Run Full Pipeline** / **Log Pipeline Status**):

**Step 1 — Build greybox**  
Place **`AGreyboxSceneBuilder`** → Details → **Build Greybox Scene**

**Step 2 — Panorama capture + export**  
Place **`APanoramicCapture360`** → **Capture360** (or orchestrator **Run Full Pipeline**)  
Place **`APanoramicExportRunner`** → **CapturePanorama** (delegates to the rig’s **`Capture360`**)  
→ **`panorama_360.png`** under `Saved/GreyboxExports/` (unless **`OutputPath`** is set)

**Step 3 — WorldLabs**  
Place **`AWorldLabsRunner`** → set **World Prompt** → **Submit To World Labs**  
→ Poll with **Check Job Status** (auto-poll every 5s after submit)  
→ When **Current Status** indicates complete → **Download Splat**  
→ `.ply` saved under `Content/GaussianSplats/`

**Step 4 — Import splat**  
Place **`AGaussianSplatImportRunner`** → set **PLY File Path** → **Import PLY Into Level**  
→ **`ACSGaussianActor`** spawned at the runner’s transform (or fixed spawn location)

**Step 5 — VP stage**  
Place **`AVPStageSetup`** → assign **Gaussian Splat Actor** ref  
→ **Step1 Find Camera** → **Step2 Add Fill Lights** → **Step3 Add Post Process** → **Step4 Log Stage Summary**

**Step 6 — Camera tracking (manual)**  
**`BP_CameraTracker`** is not shipped as C++ in this project — create a Blueprint (e.g. Pawn or actor with spring arm + camera) for in-editor fly navigation, or use editor viewport navigation.

### nDisplay / LED volume (Windows / Linux only)

See Epic’s nDisplay Quick Start. Use `Content/VirtualProduction/VP_Stage.umap` (create as needed) as inner-frustum content. A **`CineCameraActor`** labeled **`PrimaryCamera`** (from the greybox builder) is a natural ICVFX camera candidate.

## Phase 2 — C++ files added

| File | Purpose |
|------|---------|
| `Source/VirtualProductionSplat/MultiAngleCameraRig.h/.cpp` | **`APanoramicCapture360`** — cube capture, `Capture360()`, equirect PNG |
| `Source/VirtualProductionSplat/GreyboxExporter.h/.cpp` | `UGreyboxExporter::ExportAllCameras` → finds **`APanoramicCapture360`**, calls **`Capture360`** |
| `Source/VirtualProductionSplatEditor/GreyboxSceneBuilder.h/.cpp` | `AGreyboxSceneBuilder::BuildGreyboxScene` |
| `Source/VirtualProductionSplatEditor/GreyboxExportRunner.h/.cpp` | **`APanoramicExportRunner::CapturePanorama`** |
| `Source/VirtualProductionSplatEditor/VirtualProductionSplatEditor.*` | Editor module |
| `.gitignore` | Ignores `Saved/GreyboxExports/` |

## Phase 3 Complete — Files Added

- `Source/VirtualProductionSplat/WorldLabsImageHelper.h` — header-only API (implementation in `.cpp`)
- `Source/VirtualProductionSplat/WorldLabsImageHelper.cpp`
- `Source/VirtualProductionSplat/WorldLabsAPIClient.h/.cpp` — `UWorldLabsAPIClient` (HTTP JSON submit/poll/download; `DECLARE_DELEGATE_*` callbacks)
- `Source/VirtualProductionSplatEditor/WorldLabsRunner.h/.cpp` — `AWorldLabsRunner` (CallInEditor: submit / poll / download)

### WorldLabs Marble API (verify against live API)

- **`world_prompt.type`:** use **`"image"`** for panoramic equirectangular PNGs submitted via `media_asset_id`. Valid API values are **`text`**, **`image`**, **`multi-image`**, **`video`** — there is **no** `panorama` type (422 if used).
- **prepare_upload** → `media_asset.media_asset_id`, `upload_info.upload_url` (+ optional `required_headers` for GCS PUT)
- **worlds:generate** → `operation_id` (or nested under `operation`)
- **operations/{id}** → `done`, `response.world_id` when finished
- **GET worlds/{world_id}** → recursive JSON scan for an `https` URL referencing `.ply` (also checks common string fields)

Adjust parsing in `OnGenerationResponse` / `OnPollOperationResponse` / `OnFetchWorldResponse` if the live API differs.

### Notes

- `UWorldLabsAPIClient` must be owned by an `AActor` (or other outer with a valid `UWorld`) so `GetTimerManager()` works for polling.
- Download uses a plain **GET** on the URL (typical for signed CDN links); add headers in `DownloadPLYFile` if your tenant requires authenticated download.

## Phase 4 — Step 4.0 (discovery)

| Finding | Detail |
|--------|--------|
| **Module name** | `SplatRenderer` (Runtime, `LoadingPhase`: PostConfigInit) |
| **Build.cs** | Required on **`VirtualProductionSplat`** once project code `#include "CSGaussianActor.h"` (or other plugin public headers). Listed under **`PublicDependencyModuleNames`**. |
| **Editor module** | **`VirtualProductionSplatEditor`** also lists **`SplatRenderer`** in **`PrivateDependencyModuleNames`** for direct includes in editor-only `.cpp` files. |
| **Integration surface** | **`ACSGaussianActor::LoadPLY`** — no need to link private `FCSPLYLoader` from game code. |
| **Prior builds** | Editor/game could compile without `SplatRenderer` in project **Build.cs** until no project translation unit included plugin headers. |
| **Upstream** | RHI deprecation warnings in `CSGaussianBuffers.h` — do not modify (plugin vendor). |

## Phase 4 — Implementation (4.1–4.4)

### 4.1 — Module dependency
- `Source/VirtualProductionSplat/VirtualProductionSplat.Build.cs`: **`SplatRenderer`** added to **`PublicDependencyModuleNames`**.
- `Source/VirtualProductionSplatEditor/VirtualProductionSplatEditor.Build.cs`: **`SplatRenderer`** added to **`PrivateDependencyModuleNames`**.

### 4.2 — `UGaussianSplatImportHelper`
- `Source/VirtualProductionSplat/GaussianSplatImportHelper.h/.cpp` — **`SpawnGaussianSplatAt`**: spawns **`ACSGaussianActor`**, sets **`SplatScale`**, calls **`LoadPLY`**; **`UE_LOG(LogVPSplat, ...)`**.

### 4.3 — `AGaussianSplatImportRunner`
- `Source/VirtualProductionSplatEditor/GaussianSplatImportRunner.h/.cpp` — editor actor: **`PLYFilePath`**, **`ImportPLYIntoLevel`** (CallInEditor), optional spawn-at-self vs fixed location; default path `Content/GaussianSplats/WorldLabs_export.ply`; logs via **`LogTemp`**.

### 4.4 — Build
- Confirmed: **`Build.sh VirtualProductionSplatEditor Mac Development`** succeeds after the above.

## Phase 5 — Files Added

| File | Purpose |
|------|---------|
| `Source/VirtualProductionSplatEditor/VPStageSetup.h/.cpp` | **`AVPStageSetup`** — VP stage steps 1–4 (camera, fills, post, log) |
| `Source/VirtualProductionSplatEditor/VPPipelineOrchestrator.h/.cpp` | **`AVPPipelineOrchestrator`** — full pipeline runner + status log |

### Remaining manual editor-only work (no further C++ required for this pipeline)

- Create/save **`GreyboxScene.umap`**, **`VP_Stage.umap`** if not present
- Optional Blueprints: **`BP_PanoramicCapture360`** (parent **`APanoramicCapture360`**), **`BP_CameraTracker`**, EUW widgets
- **WorldLabs**: real API field names / auth if different from assumptions
- **nDisplay / ICVFX** production setup on **Win64/Linux**; content packaging for LED
- **UnrealClaude** `npm install` when using MCP bridge
