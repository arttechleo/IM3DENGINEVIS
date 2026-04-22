# Windows Handoff — VirtualProductionSplat

Use Windows for all Gaussian Splat rendering.  
The SplatRenderer plugin ships no Metal shader binary — the compute pass crashes on Mac.

---

## One-time setup

1. **Clone / copy the project** to a Windows machine.

2. **Generate Visual Studio files**  
   Right-click `AIProject.uproject` → *Generate Visual Studio project files*.

3. **Delete the local DDC before first launch** (avoids stale Mac shader entries):
   ```
   rmdir /s /q Saved\DerivedDataCache
   ```

4. **Open in UE 5.7** and wait for *"Compiling Shaders"* to complete in the bottom-right corner before doing anything with splats.

5. **Add your WorldLabs API key** to `Config/DefaultGame.ini`:
   ```ini
   [WorldLabsAPI]
   APIKey=YOUR_KEY_HERE
   ```

6. **Install Node.js** (for UnrealClaude MCP bridge, optional):
   ```
   cd Plugins\UnrealClaude\Resources\mcp-bridge
   npm install
   ```

---

## Per-session pipeline

| Step | Actor | Action |
|------|-------|--------|
| 1 | `AGreyboxSceneBuilder` | **Build Greybox Scene** |
| 2 | `APanoramicCapture360` | **Capture360** → writes `Saved/GreyboxExports/panorama_360.png` |
| 3 | `AWorldLabsRunner` | Set **World Prompt** → **Submit To World Labs** → auto-polls → auto-downloads `.ply` → auto-imports splat |
| 4 | *(auto)* | `ACSGaussianActor` "WorldLabs_Splat" spawned / updated in level |
| 5 | `AVPStageSetup` | Step 1–4 (camera, fill lights, post process, log) |

Or use **`AVPPipelineOrchestrator`** → **Run Full Pipeline** to run steps 1–4 in one click.

---

## Python / SPZ → PLY conversion

The bundled UE Python is used automatically:

```
Engine\Binaries\ThirdParty\Python3\Win64\python.exe
```

`numpy` is the only dependency and is auto-installed on first run if missing.  
No C compiler or external packages required.

---

## Rendering notes

- **DX12 / SM5** required — SplatRenderer uses compute UAV writes.
- Open `Edit → Project Settings → Platforms → Windows → Default RHI` and confirm **DirectX 12**.
- If you see a *"Compiling Shaders"* indicator after opening the level, wait for it to finish before placing / importing splats.

---

## nDisplay / LED volume

- Use `Content/VirtualProduction/VP_Stage.umap` as the inner-frustum level.
- Follow Epic's [nDisplay Quick Start](https://docs.unrealengine.com/5.0/en-US/ndisplay-quick-start-for-unreal-engine/).
- `ACSGaussianActor` ("WorldLabs_Splat") is the background plate; `PrimaryCamera` CineCameraActor from the greybox builder is the ICVFX camera candidate.
- nDisplay cluster rendering is **not supported on Mac** — Windows or Linux only.
