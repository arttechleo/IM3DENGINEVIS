# MLSLabsRenderer survival notes (VirtualProductionSplat)

This project uses **MLSLabsRenderer** with a **local libtorch** tree. Unreal loads native torch DLLs from the plugin and staged folders — **not** from conda, venv, or `PATH`.

## Expected layout

```
Plugins/MLSLabsRenderer/
  MLSLabsRenderer.uplugin
  Source/MLSLabsRenderer/MLSLabsRenderer.Build.cs
  libtorch/
    include/          ← headers
    include/torch/csrc/api/include
    lib/              ← required: torch DLLs + .lib (Win64)
    bin/              ← optional extra DLLs from the same libtorch zip
  Source/ThirdParty/GaussianSplatingRenderer/Bin/Win64/   ← vendor native renderer DLLs (if shipped)
```

**libtorch** must stay inside `Plugins/MLSLabsRenderer/libtorch`. Use the official **libtorch-win-shared-with-deps** archive that matches your CUDA build (e.g. 2.7 + cu128), and copy the `libtorch` folder contents so `lib/` contains `torch_cuda.dll`, `torch_cpu.dll`, `c10.dll`, etc.

Python **torch** is used only for **SPZ → PLY** conversion scripts outside the plugin runtime; the editor plugin does not use your Python environment for libtorch.

## “MLSLabs Plugin Environment Setup” says *File is not a zip file*

That dialog downloads **`Saved/libtorch_tmp.zip`**. The error means the bytes on disk are **not** a real PyTorch archive — usually:

- a **proxy or firewall** returned an **HTML** page instead of the zip;
- a **failed or partial** download;
- an old **`libtorch_tmp.zip`** left over from a previous attempt (the installer used to skip re-download for any file larger than 1 GB even if it was corrupt).

**What to do**

1. Close the editor, delete **`Saved/libtorch_tmp.zip`**, reopen, and run the setup again on a network that can reach **`download.pytorch.org`** (try without VPN if you use one).
2. Or **skip the wizard**: download **`libtorch-win-shared-with-deps-2.7.0+cu128`** for **cu128** from [https://pytorch.org/get-started/locally/](https://pytorch.org/get-started/locally/) (LibTorch, Windows, CUDA 12.8), extract it, and place the resulting **`libtorch`** folder under **`Plugins/MLSLabsRenderer/libtorch`** (with **`lib/`** full of DLLs), then run **`Tools\fix_mlslabs.bat`**.

The in-editor installer script **`Plugins/MLSLabsRenderer/Content/Python/Lib/site-packages/install_deps.py`** was patched to validate the ZIP header and size before extract and to remove bogus temp files so the next run re-downloads cleanly.

**Note:** The vendor wizard extracts into **`Plugins/MLSLabsRenderer/Source/ThirdParty/`** (as shipped). This project’s **`MLSLabsRenderer.Build.cs`** prefers **`Plugins/MLSLabsRenderer/libtorch`** and still understands legacy **`Source/ThirdParty/libTorch/lib`** DLL paths. For the simplest layout, keep **`libtorch`** at the plugin root as in the tree above.

## One-shot: download and install libtorch (automated)

From PowerShell (editor closed), from the project root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\bootstrap_libtorch.ps1
```

This downloads the official **cu128** shared libtorch (~3 GiB) with **`curl.exe`**, extracts with **`tar.exe`**, replaces **`Plugins/MLSLabsRenderer/libtorch`**, then runs **`Tools\fix_mlslabs.bat /FORCE`**. The zip is kept at **`Saved/libtorch_cu128.zip`** for resume if you re-run.

## When Unreal crashes on startup or “libtorch not found”

1. Close the editor (Task Manager if needed).
2. Run **`Tools\fix_mlslabs.bat`** from Explorer or a `cmd` prompt (double-click is fine if Unreal is closed).
3. Optional force-close Unreal: **`Tools\fix_mlslabs.bat /FORCE`**
4. Re-open **`VirtualProductionSplat.uproject`**.

`fix_mlslabs.bat` deletes **only** build artifacts:

- Project `Binaries`, `Intermediate`, `Saved\Logs`
- `Plugins\MLSLabsRenderer\Binaries`, `Plugins\MLSLabsRenderer\Intermediate`

It **does not** delete `Content`, `Config`, `Source`, plugin `Source`, or `libtorch`.

It copies all `*.dll` from `libtorch\lib` and `libtorch\bin` into:

- `Plugins\MLSLabsRenderer\Binaries\Win64`
- `Binaries\Win64`

so the Windows loader can resolve dependencies next to binaries.

### Optional: regenerate IDE project files

If `UE_ROOT` is set to your engine root (folder that contains `Engine\`), the script runs UBT `-projectfiles`. Otherwise, right-click **`VirtualProductionSplat.uproject`** → **Generate Visual Studio project files**.

## Verify without touching files

From PowerShell in the repo:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\Tools\verify_mlslabs.ps1
```

Green/yellow/red lines summarize DLL counts and folder presence.

## Editor self-check

**Tools → VirtualProductionSplat → Verify MLSLabs** writes the same style of report to the **Output Log** (category `LogVPSplat`).

## What not to delete

- **`Plugins/MLSLabsRenderer/libtorch`** — your torch runtime.
- **`Content`**, **`Config`**, **`Source`**
- Plugin **`Source`** and **`MLSLabsRenderer.uplugin`**

## Remaining risks

- A bad or mixed CUDA/libtorch build can still crash inside vendor DLLs before your game code runs.
- **PostConfigInit** loads the runtime module early; keep libtorch complete to reduce load failures.
- Non-Windows targets are unsupported for this plugin (`PlatformAllowList`: Win64).
