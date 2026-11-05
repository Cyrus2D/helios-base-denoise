---
applyTo: "src/coach/**,src/trainer/**"
---
# HELIOS Coach and Trainer (src/coach, src/trainer)

## TL;DR
Coach and trainer are two thin rcsc agents: `SampleCoach` substitutes player types and broadcasts opponent player types, while `SampleTrainer` does almost nothing in the active path (its keepaway logic is a stub).
- Coach: `SampleCoach` (`src/coach/sample_coach.cpp`), binary `sample_coach`.
- Trainer: `SampleTrainer` (`src/trainer/sample_trainer.cpp`), binary `sample_trainer`.
- The trainer's only active call is `doKeepaway()`, which just logs at training time (`sample_trainer.cpp:109-124, 311-320`).
- `train.sh` launches a trainer named `helios_trainer` and a formation dir `formations-train`, neither of which exists in this tree.
- **Open the full file when:** changing substitution rules, the opponent-type broadcast, team graphics, or any trainer scenario.

## Overview
- Both agents derive from librcsc agent bases (`rcsc/coach/coach_agent.h`, `rcsc/trainer/trainer_agent.h`) and share the `main_*.cpp` pattern: signal handlers, `agent.init(cmd_parser)`, `createConsoleClient()`, `client->run(&agent)` (`coach/main_coach.cpp:60-114`, `trainer/main_trainer.cpp`).
- Both are compiled from explicit lists: `src/coach/CMakeLists.txt` (three sources, target `sample_coach`) and `src/trainer/CMakeLists.txt` (two sources, target `sample_trainer`). `src/coach/Makefile.am` and `src/trainer/Makefile.am` list the same files.
- Hand-off with the players: the coach sends `OpponentPlayerTypeMessage` freeform messages (`coach/sample_freeform_message.h/.cpp`, sent at `coach/sample_coach.cpp:690`); the player parses them with `OpponentPlayerTypeMessageParser` (`player/sample_player.cpp:141`).

## Architecture
### SampleCoach (`src/coach/sample_coach.h`, `sample_coach.cpp`)
- Members: `int M_opponent_player_types[11]` and `rcsc::TeamGraphic M_team_graphic`.
- `initImpl` (`sample_coach.cpp:147-190`): if `config().useTeamGraphic()`, loads the built-in logo (`team_logo_xpm`, from `#include "team_logo.xpm"`) or the file from `config().teamGraphicFile()`.
- `actionImpl` (`sample_coach.cpp:197-210`), each cycle: at cycle 0 with team graphics on and not all tiles sent → `sendTeamGraphic()`; then `doSubstitute()`; then `sayPlayerTypes()`.
- `doSubstitute()` (`sample_coach.cpp:257-279`):
  - once, at cycle 0 after more than 10 stopped cycles → `doFirstSubstitute()` (prints a table of player types and substitutes; `:286`)
  - later, when cycle > 0 and not PlayOn and not penalty mode → `doSubstituteTiredPlayers()` (`:437`)
- Other helpers: `substituteTo(unum, type)` (`:539`), `getFastestType(candidates)` (`:580`), `sayPlayerTypes()` (`:647`), `sendTeamGraphic()` (`:732`).
- The `handle*` overrides are empty (`sample_coach.cpp:216-250`).

### SampleTrainer (`src/trainer/sample_trainer.h`, `sample_trainer.cpp`)
- `initImpl` (`:70-102`) only calls the base and leaves a placeholder comment.
- `actionImpl` (`:109-124`): if the left team name is unknown → `doTeamNames()` and return; otherwise → `doKeepaway()`.
- Inactive scenarios kept as comments: `sampleAction()` (`:171-247`, places the ball and a player, changes player type, and kicks the ball at 2.3-2.7 speed with ±10° spread), `recoverForever()` (`:254`), and `doSubstitute()` (`:274-304`, changes player types).
- `doKeepaway()` (`:311-320`) only prints when `world().trainingTime() == world().time()`. It is a stub, not an implemented keepaway trainer.

### Config files
- `src/coach.conf`: `team_name : HELIOS_base`, `coach_name : Coach_base`, `port : 6002`, `interval_msec : 50`, `use_eye : on`, `hear_say : on`. `use_team_graphic` and `use_advice`/`use_freeform`/`use_hetero` are commented out.
- No trainer config file exists. The scripts pass no config to `sample_trainer`; `train.sh.in:22` points `player.conf` at the player it launches.

## Patterns & Conventions
- Agents are singletons inside `main_*.cpp` (`SampleCoach agent;` in an anonymous namespace, `coach/main_coach.cpp:44`).
- Logging: standard `std::cerr` / `std::cout` lines with the team name and cycle; the coach uses `debugClient().addMessage`.
- Player-type indexing: `PlayerParam::i().playerTypes()` and `ptMax()` bound the type ids used in substitutions.
- Keep the copyright banner in `main_*.cpp` (the comment asks for it to be retained).

## Integration Points
- Launched by `src/start.sh.in:355-375` as `sample_coach` on `port + 2` (or `-P`), with `--coach-config coach.conf`, unless `-C` is passed.
- Not launched by `keepaway.sh.in` (it starts players and `sample_trainer` only, `:258-286`).
- `train.sh.in` has the coach launch commented out (`:93-95`) and starts `${DIR}/helios_trainer` (`:18, 97`) with `--config_dir ${DIR}/formations-train` (`:23`).
- The player-side parser is in `src/player/sample_freeform_message_parser.*`, registered at `sample_player.cpp:141`.

## Build & Test
- Build targets: `sample_coach` (`src/coach/CMakeLists.txt:2`) and `sample_trainer` (`src/trainer/CMakeLists.txt:2`); both link `${LIBRCSC_LIB}`, Boost system, and ZLIB (see [build-run-scripts](build-run-scripts.instructions.md)).
- No tests exist for the coach or trainer.
- Trainer binary name mismatch: the CMake and autotools builds produce `sample_trainer`; `train.sh.in:18` and `:97` call `helios_trainer`, which no build target produces.

## Important Notes
- The coach's substitution is the only real logic in the active coach path. `SampleCoach` does nothing with noise parameters; noise is server-side and is not configured here.
- `formations-train/` is referenced by `train.sh.in:23` but does not exist under `src/`. `train.sh` will fail at start unless that directory is created.
- Keepaway training is not implemented in the trainer: `doKeepaway()` is a stub (`sample_trainer.cpp:311-320`). `keepaway.sh.in:285` nevertheless runs `sample_trainer -t Keeper --use_ear on` as the keepaway trainer.
- `coach/sample_freeform_message.*` defines `OpponentPlayerTypeMessage`; the coach sends it and the player parses it.

## See Also
- [player-behaviors](player-behaviors.instructions.md) — players that the coach and trainer influence
- [formations-config](formations-config.instructions.md) — formation sets loaded by the players
- [build-run-scripts](build-run-scripts.instructions.md) — start/keepaway/train scripts and build targets
- [../copilot-instructions.md](../copilot-instructions.md) — repository map
