---
applyTo: "src/formations-*/**,src/*.conf"
---
# HELIOS Formations and Config (src/formations-*, src/*.conf)

## TL;DR
Formations are JSON files, one set per directory (`formations-dt`, `formations-keeper`, `formations-taker`), each loaded as a whole by `Strategy::read`; `player.conf` and `coach.conf` hold the agent options.
- Each set has 14 files; `Strategy::read` loads 13, and `goalie-formation.conf` is never read.
- Keeper and taker sets use `KeepawayKeeper` / `KeepawayTaker` roles only in `normal-formation.conf`; their `defense-` and `offense-formation.conf` are symlinks to it.
- Role names in a file must match the role factory in `Strategy`'s constructor, and there must be exactly one goalie uniform number.
- Agents read the set from `--config_dir` (`start.sh -f`, default `formations-dt`).
- **Open the full file when:** adding or editing a formation, changing role assignments, or changing agent options in `player.conf` / `coach.conf`.

## Overview
- Three directories with 14 `.conf` files each (verified by parsing them):
  - `src/formations-dt/` — the full standard set (dt = Delaunay-triangulation positioning)
  - `src/formations-keeper/` — keepaway keepers
  - `src/formations-taker/` — keepaway takers
- Obsolete sets live under `src/obsolete/` (`formations-bpn`, `formations-uva`, plus `reconnect.sh.in` references to `formations-433`). Not built or installed.
- The `.conf` extension is misleading: the files are JSON (`Strategy::createFormation` calls `FormationParser::parse`, `strategy.cpp:289-298`; the parser lives in librcsc and was not read here).

## Architecture
### File format (JSON)
Top-level keys seen in every file:
- `"version"`, `"method"` — values `"Static"` or `"DelaunayTriangulation"`.
- `"role"` — array of 11 entries `{ "number", "name", "type", "side", "pair" }`; `number` is the uniform number (1-11), `name` is a role name, `type` is a position class (`G`, `DF`, `MF`, `FW`), `side` is `L`, `C`, or `R`, and `pair` links mirrored players.
- `"data"` — array of rows `{ "index", "ball": {x,y}, "1": {x,y}, … "11": {x,y} }`: each row gives every player's home position for one ball position.

Counts per file in `formations-dt` (verified):

| File | Method | Data rows |
|---|---|---|
| `normal-formation.conf`, `defense-formation.conf`, `offense-formation.conf` | DelaunayTriangulation | 115 each |
| `setplay-opp-formation.conf` | DelaunayTriangulation | 45 |
| `setplay-our-formation.conf` | DelaunayTriangulation | 46 |
| `goalie-formation.conf` | DelaunayTriangulation | 42 (not loaded) |
| `indirect-freekick-opp-formation.conf` | DelaunayTriangulation | 21 |
| `indirect-freekick-our-formation.conf` | DelaunayTriangulation | 25 |
| `kickin-our-formation.conf` | DelaunayTriangulation | 21 |
| `before-kick-off.conf`, `goal-kick-opp.conf`, `goal-kick-our.conf`, `goalie-catch-opp.conf`, `goalie-catch-our.conf` | Static | 1 each |

Every `formations-dt` file uses the same role set: Goalie 1, CenterBack 2, SideBack 2, DefensiveHalf 1, OffensiveHalf 2, SideForward 2, CenterForward 1 (= 11).

### Loading (`src/player/strategy.cpp`)
- `Strategy::read(config().configDir())` (`strategy.cpp:173-283`, called from `sample_player.cpp:197`) appends `/` to the directory and loads, in order: before-kick-off, normal, defense, offense, goal-kick-opp, goal-kick-our, goalie-catch-opp, goalie-catch-our, kickin-our, setplay-opp, setplay-our, indirect-freekick-opp, indirect-freekick-our. Missing or invalid files fail the read and the agent exits.
- `goalie-formation.conf` appears in no `.cpp`/`.h`. It is only listed in `src/Makefile.am:17, 31, 45` as distributed data.
- `Strategy::getFormation(wm)` (`strategy.cpp:714`) selects the set: PlayOn → defense/offense/normal by situation; kick-in or corner → `kickin-our` (own) or `setplay-opp` (theirs); indirect free kick and back pass → the indirect pair; foul → indirect or setplay-opp by ball position; other set plays → setplay-our/opp; goal kick and goalie catch → their own sets.
- `createFormation` (`strategy.cpp:289-352`) rejects a role name that is not in `M_role_factory`, and rejects a file whose Goalie/Savior uniform number differs from the first one it saw.

### Keepaway sets
- `formations-keeper/normal-formation.conf` and `formations-taker/normal-formation.conf` each have 4 data rows and 11 roles all named `KeepawayKeeper` or `KeepawayTaker`.
- `defense-formation.conf` and `offense-formation.conf` in both keepaway directories are git symlinks (mode 120000) to `normal-formation.conf`. On a Windows checkout without symlink support they become zero-byte files, which will fail to parse (see Important Notes).
- All other keepaway files reuse the standard dt role names (Goalie, CenterBack, …), so they will not use keepaway roles.

### Agent options
- `src/player.conf`: `team_name : HELIOS_base`, `version : 18`, `synch_see : on`, `host : localhost`, `port : 6000`, `use_communication : on`, `hear_opponent_audio : off`, `log_dir : /tmp`, `debug_log_ext : .log`, and `debug_*` switches (commented-out `offline_logging`, `debug_server_*`).
- `src/coach.conf`: `port : 6002`, `interval_msec : 50`, `use_eye : on`, `hear_say : on`; `use_team_graphic` is commented out.
- The file has no `gaussian_see` or `dist_noise_rate` entries, and nothing in helios reads those keys (see [player-behaviors](player-behaviors.instructions.md)).

## Patterns & Conventions
- Keep set-specific files parallel: the same filenames in each set directory, the same 11 role slots.
- In the rows of `normal-formation.conf` that were inspected, player 1 (the goalie) is at `x = -50.00`.
- Row `"index"` values start at 0 and are sequential in the inspected files.
- For a new formation, copy an existing file of the same name from `formations-dt` and edit the `data` rows; do not change `role` slots unless the role factory changes too.

## Integration Points
- `start.sh.in:34` defaults `config_dir` to `${DIR}/formations-dt`, and `:157` overrides it with `-f DIR`; `:308` passes `--config_dir` and `--player-config src/player.conf`.
- `keepaway.sh.in:40-41` hard-codes `formations-keeper` and `formations-taker` for the two teams (see Important Notes for the `-f` issue).
- `train.sh.in:23` expects `formations-train/`, which does not exist.
- Copied into the build output by `src/CMakeLists.txt:41-44` (`formations-dt formations-keeper formations-taker player.conf coach.conf start-debug.sh start-offline.sh`). Autotools distribution lists are in `src/Makefile.am:5-53`.

## Build & Test
- Nothing is compiled. Changes take effect at the next agent start.
- No automated validation exists. A quick check is parsing the file as JSON and confirming the role names match `Strategy`'s map (`strategy.cpp:102-115`).

## Important Notes
- **`goalie-formation.conf` is dead data.** It appears in each set, is distributed by `Makefile.am`, and is never loaded by `Strategy::read`.
- **Keepaway symlinks break on Windows checkouts.** `formations-keeper/defense-formation.conf`, `offense-formation.conf`, and the taker equivalents are symlinks in git. A zero-byte file (as seen in this working copy) cannot parse, so `Strategy::read` returns false at `strategy.cpp:207-212`. The expected parse failure is inferred from the code, not run.
- **`keepaway.sh -f DIR` has no effect.** It sets `config_dir` (`keepaway.sh.in:121-128`), but the agents use `keeper_config_dir` / `taker_config_dir` (`:265`, `:279`).
- **`.dat` companions are obsolete.** `src/obsolete/formations-bpn/*.dat` are not read by the current code.

## See Also
- [player-behaviors](player-behaviors.instructions.md) — role factory, `getFormation` usage, and decision order
- [coach-trainer](coach-trainer.instructions.md) — coach/trainer configs and substitution logic
- [build-run-scripts](build-run-scripts.instructions.md) — start/keepaway/train scripts and config directory flags
- [../copilot-instructions.md](../copilot-instructions.md) — repository map
