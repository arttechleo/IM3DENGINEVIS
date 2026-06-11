# VirtualProductionSplat — Production Readiness Plan

## Current State (June 10 2026)
- Pipeline end-to-end: WORKING (greybox → capture → WorldLabs API → SPZ → PLY → NanoGS → actor spawned)
- Repo: clean history (40MB), NanoGS rewired, dead MLSLabs removed
- Prompt passthrough: WORKING
- Stitch orientation: WORKING (FILE_REMAP workaround active)
- Known hacks: 2 coupled source hacks (cpp pitch rotation + Python FILE_REMAP) — must be reconciled before release
- Uncommitted changes: 9 modified files, untracked assets, dirty submodule

## Blockers Before High-Quality Model Testing

### P0 — Must fix before any paid generation
1. **Panorama quality** — verify current capture produces clean neutral-grey panorama with no purple, no sky bleed, no pipeline actors visible. Screenshot required.
2. **Commit working state** — 9 modified files uncommitted. Commit or discard before proceeding.
3. **Cleanup GaussianSplats/** — 22 .spz / 7 .ply accumulating with no gitignore. Add to .gitignore, delete test artifacts (test_output.ply, manual_test_output.ply).
4. **PosZ/NegZ orientation** — RESOLVED. Single fix: FILE_REMAP in StitchEquirectangular.py. cpp rotations are stock. NOTE comment in MultiAngleCameraRig.cpp documents the coupling.

### P1 — Apple-grade pipeline quality
5. **Greybox material** — M_Greybox_Neutral (unlit 50% grey) must visually verify as flat grey in capture, not purple. Python overpaint with BasicShapeMaterial as fallback.
6. **Capture point placement** — rig must be inside scene at centroid, not floating above. Verify with each new greybox.
7. **NanoGS SplatScale calibration** — determine correct scale for WorldLabs SPZ output. Currently renders at wrong scale/distance.
8. **Splat actor cleanup on re-run** — old GaussianSplatActors must be removed before new capture to prevent them appearing in panorama.
9. **Wall_south missing from keeper list** — open south wall exposes sky. Either add a south wall to greybox or accept open-horizon design.

### P2 — Production UX (before public release)
10. **Single toolbar entry point** — remove orchestrator actor, toolbar is the only UI
11. **Auto-chain verified** — submit → poll → download → import with zero manual steps confirmed working reliably
12. **Progress feedback** — status visible during API poll (not just logs)
13. **Default GreyboxScene.umap** — ship a reference scene artists can open immediately
14. **Python dependency removed** from Setup VP Level step
15. **Plugin packaging** — move from game project structure to standalone .uplugin distributable

### P3 — AI-assisted prompting (post-quality baseline)
16. **Lightweight Claude refinement** — text-only prompt expansion via Anthropic API (no base64 image, no panorama in body). Re-enable ClaudePromptRefiner with this approach.
17. **Semantic capture shader** — custom unlit material with per-surface-type color coding (floor/wall/prop) for better AI scene understanding

## Definition of Done (v1.0 public release)
- Artist opens project, builds greybox, clicks Submit, sees a production-quality Gaussian splat in their level within WorldLabs job time
- Splat quality is good enough that a director says "I want to stand in this"
- Zero manual steps, zero log-reading required
- Pipeline works on WorldLabs marble-1.0 full model or better

## Out of Scope for v1
- Multi-room / multi-capture
- Non-WorldLabs backends
- Runtime (non-editor) pipeline
- Documentation
