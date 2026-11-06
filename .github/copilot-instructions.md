# helios-base-denoise — Project Instructions

## TL;DR
helios-base: sample RoboCup 2D team (player, coach, trainer) built on librcsc.
- Entry points: `src/player/main_player.cpp`, `src/coach/main_coach.cpp`, `src/trainer/main_trainer.cpp`.
- Per-cycle decisions: `SamplePlayer::actionImpl` -> Strategy/roles/behaviors/planner.
- Has its own `basic_actions/`; does not use `rcsc/action`. Helios code never sends `(gaussian_see)`; librcsc sends it only if `gaussian_see : on` is set in `src/player.conf` (off by default).
- **Open the full file when:** you need build/run steps or the right scoped instruction.

## Project Overview
Reference client that connects to rcssserver (`../rcssserver-denoise`) through librcsc (`../librcsc-denoise`, must be installed first). Protocol docs: `../manual/source/`.

## Architecture and Entry Points
- `src/player/` agent, roles, `bhv_*`, `planner/`, `setplay/`, `basic_actions/`; `src/coach`, `src/trainer`; formations in `src/formations-{dt,keeper,taker}/`; `src/obsolete/` legacy.
- `eval/`, `ssil/`: helper scripts/templates (see build-run-scripts).

## Build, Test, Run
- Autotools: `./bootstrap && ./configure [--with-librcsc=PREFIX] && make` (README.md).
- CMake: finds librcsc via `LIBRCSC_INSTALL_DIR` (default probes `$HOME/.local`, `$HOME/local`).
- Run with `src/start.sh` (host/port/team options; `--denoise-log-dir DIR --fullstate reference` enables the librcsc dataset logger). No tests, no CI.

## Conventions
Add roles in `Strategy` constructor map (not `autoReg`). Keep CMake and `Makefile.am` source lists in sync.

## Instruction Index
Open the entries relevant to the files you touch (on demand, not auto-loaded). For denoising work start with the observation/noise entries.

| Instruction | Covers (applyTo) | Summary | Path |
|---|---|---|---|
| HELIOS Build, Run Scripts, eval/ and ssil/ | `CMakeLists.txt,**/CMakeLists.txt,configure.ac,**/Makefile.am,src/*.sh*,eval/**,ssil/**` | helios-base builds with either CMake or autotools against an installed librcsc, producing `sample_player`, `sample_coach`, and `sample_trainer`; the start scrip | [instructions/build-run-scripts.instructions.md](instructions/build-run-scripts.instructions.md) |
| HELIOS Coach and Trainer (src/coach, src/trainer) | `src/coach/**,src/trainer/**` | Coach and trainer are two thin rcsc agents: `SampleCoach` substitutes player types and broadcasts opponent player types, while `SampleTrainer` does almost nothi | [instructions/coach-trainer.instructions.md](instructions/coach-trainer.instructions.md) |
| Communication (say / hear / attentionto) in helios-base | `src/player/{sample_communication,keepaway_communication,sample_player}*,src/player/*say*,src/coach/**` | Every cycle `SampleCommunication::execute` builds at most one compact say string (ball + up to 3 players, or goalie/stamina extras) within the 10-char budget, w | [instructions/communication-say-hear.instructions.md](instructions/communication-say-hear.instructions.md) |
| HELIOS Formations and Config (src/formations-*, src/*.conf) | `src/formations-*/**,src/*.conf` | Formations are JSON files, one set per directory (`formations-dt`, `formations-keeper`, `formations-taker`), each loaded as a whole by `Strategy::read`; `player | [instructions/formations-config.instructions.md](instructions/formations-config.instructions.md) |
| Noise Handling in Helios Decisions (src/player) | `src/player/**` | Helios has no observation-noise model: it takes librcsc's fused `pos()/vel()` as truth and compensates only with (a) server MOVEMENT noise margins (`playerRand` | [instructions/noise-handling-in-decisions.instructions.md](instructions/noise-handling-in-decisions.instructions.md) |
| Observation, View & Neck Control (helios-base) | `src/player/{neck_*,view_*,bhv_scan_field,bhv_basic_move,sample_player,body_*}*,src/player/basic_actions/{neck,view}*` | Helios never asks for better sensor data; it only steers WHERE and HOW WIDE the player looks, using librcsc age counters (`posCount`, `seenPosCount`, `dirCount` | [instructions/observation-view-control.instructions.md](instructions/observation-view-control.instructions.md) |
| HELIOS Player Behaviors (src/player) | `src/player/**` | `sample_player` is one `rcsc::PlayerAgent` subclass whose every cycle runs a fixed priority chain (preprocess, shoot/force-kick/pass-receive checks, planner gra | [instructions/player-behaviors.instructions.md](instructions/player-behaviors.instructions.md) |

## Related Areas
- Library: `../librcsc-denoise/.github/copilot-instructions.md` (separate repo)
- Server: `../rcssserver-denoise/.github/copilot-instructions.md` (separate repo)

## PR Review — Instruction Maintenance
GitHub Copilot PR reviewers: use the Instruction Index to read scoped guidance
relevant to the changed paths, respecting each repository/submodule's ownership.
When substantive changes to architecture, interfaces, workflows, build or
configuration behavior, or safety constraints make relevant instructions stale
or incomplete, report the maintenance need as a review finding/recommendation.
Identify the affected instruction and the claim or gap, with change evidence;
judge significance semantically, not by a changed-line threshold. This note does
not authorize reviewers to edit instructions and does not guarantee Copilot loads
all instruction files; follow relevant links on demand.
