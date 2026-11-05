---
applyTo: "src/player/**"
---
# HELIOS Player Behaviors (src/player)

## TL;DR
`sample_player` is one `rcsc::PlayerAgent` subclass whose every cycle runs a fixed priority chain (preprocess, shoot/force-kick/pass-receive checks, planner graph, then role or set-play), and whose roles and behaviors are plain classes selected through `Strategy`.
- Entry: `main_player.cpp` → `SamplePlayer::actionImpl()` (`sample_player.cpp:219`).
- Roles resolve only through the 11 names registered in the `Strategy` constructor (`strategy.cpp:102-115`); formation files must use those names.
- Kicks are chosen by the planner (`planner/action_chain_holder.cpp`, `Bhv_PlannedAction`); noise margins read the server's `playerRand()`/`ballRand()`, never a gaussian/quantized switch.
- Built sources are exactly the lists in `src/player/CMakeLists.txt` and `src/player/Makefile.am`; `planner/positioning.cpp` and two other files are not built.
- **Open the full file when:** changing decision order, adding a role or behavior, or touching kick/intercept margins.

## Overview
- Target `sample_player`: `src/player/CMakeLists.txt:1` (CMake) and `noinst_PROGRAMS` in `src/player/Makefile.am:5` (autotools). The 105 compiled `.cpp` names are identical in both files (verified by diff).
- `main_player.cpp:79-110` parses the command line, calls `agent.init()`, creates the console client, prints the agent2d copyright banner (keep it, per the comment at `:100-102`), and calls `client->run(&agent)`.
- `SamplePlayer` (`sample_player.h:37`) derives from `rcsc::PlayerAgent` and overrides `initImpl`, `actionImpl`, `communicationImpl`, the `handle*` callbacks, `createFieldEvaluator`, and `createActionGenerator`.

## Architecture
### Per-cycle decision order (`SamplePlayer::actionImpl`, `sample_player.cpp:219-316`)
1. `Strategy::instance().update(world())` and `FieldAnalyzer::instance().update(world())`; a new field evaluator and action generator are handed to `ActionChainHolder`.
2. `doPreprocess()` (`sample_player.cpp:527-637`); the first matching case returns:
   - frozen by tackle: `View_Tactical` + `Neck_TurnToBallOrScan`
   - BeforeKickOff / AfterGoal: `Bhv_CustomBeforeKickOff` to the formation position
   - own position invalid: `Bhv_Emergency`
   - ball unseen too long (`posCount` > 5, or > 10 for goalie): `Bhv_NeckBodyToBall`
   - `doShoot()` (`:644`): `Bhv_StrictCheckShoot`
   - queued intention: `doIntention()`
   - `doForceKick()` (`:669`): `Body_KickOneStep`
   - heard pass request: `doHeardPassReceive()` (`:705`): `Body_Intercept` or `Body_GoToPoint`
3. `ActionChainHolder::update(world)` builds the chain graph once per cycle (`planner/action_chain_holder.cpp:120-138`).
4. `Strategy::createRole(unum, world)` (`strategy.cpp:443`) maps the unum to a role name from the current formation. A null role prints an error and calls `setServerAlive(false)` (`sample_player.cpp:269-277`).
5. If `role->acceptExecution(world)` is true → `role->execute(this)`. The default (`soccer_role.cpp:96-105`) is true only in PlayOn.
6. Otherwise: penalty-kick mode → `Bhv_PenaltyKick`; everything else → `Bhv_SetPlay` (`setplay/bhv_set_play.cpp:74`).

### Strategy (`strategy.h`, `strategy.cpp`, singleton)
- Constructor fills `M_role_factory` (`strategy.cpp:102-115`) with Sample, Goalie, CenterBack, SideBack, DefensiveHalf, OffensiveHalf, SideHalf, SideForward, CenterForward, KeepawayKeeper, KeepawayTaker.
- Each `role_*.cpp` also self-registers with `SoccerRole::creators().autoReg(...)` (e.g. `role_sample.cpp:59`). That registry is used only if `USE_GENERIC_FACTORY` is defined, and it is commented out at `strategy.h:45`. The constructor map is what runs.
- `Strategy::read(config_dir)` (`strategy.cpp:173-283`) loads 13 formation files. `goalie-formation.conf` is never loaded (see Important Notes).
- `getFormation(wm)` (`strategy.cpp:714`) selects the set by game mode and situation: PlayOn → defense / offense / normal; kick-in and corner → `kickin-our` or `setplay-opp`; indirect free kicks, goal kicks, goalie catch, and free kicks → their matching set. See [formations-config](formations-config.instructions.md).
- `createFormation` (`strategy.cpp:289-352`) rejects unregistered role names and enforces a single goalie uniform number (`M_goalie_unum`).

### Roles (`soccer_role.h/.cpp`, `role_*.h/.cpp`)
- `SoccerRole` base: pure virtual `bool execute(rcsc::PlayerAgent*)` and virtual `acceptExecution(const WorldModel&)`.
- `RoleSample` (`role_sample.cpp:67-117`): kickable → `Bhv_PlannedAction`, falling back to `Body_HoldBall` + `Neck_ScanField`; not kickable → `Bhv_BasicMove`.
- `RoleGoalie` (`role_goalie.cpp:66-128`): catches inside its own penalty box (`agent->doCatch()`); kickable → `Body_ClearBall`; chase situation → `Bhv_GoalieChaseBall`; otherwise `Bhv_GoalieBasicMove`.
- The other roles and the two keepaway roles were checked only for the behaviors they call (`Bhv_PlannedAction`, `Bhv_BasicMove`, `Body_HoldBall`, `Body_Intercept`, `Neck_*`).

### Planner (`planner/`)
- `SamplePlayer::createActionGenerator` (`sample_player.cpp:797-847`) composes `ActGen_Shoot` (chain length ≥ 2), `ActGen_StrictCheckPass`, `ActGen_Cross`, `ActGen_ShortDribble`, and `ActGen_SelfPass` (each capped at chain length 1). `ActGen_DirectPass` and `ActGen_SimpleDribble` are commented out there but still compiled.
- Evaluator: `SampleFieldEvaluator` (`sample_field_evaluator.h/.cpp`, `sample_player.cpp:778-781`).
- `Bhv_PlannedAction::execute` (`planner/bhv_planned_action.cpp:204`) follows the first action of the best chain and sets `Neck_TurnToReceiver`. Hold chains call `Body_HoldBall`.
- Supporting pieces: `Bhv_PassKickFindReceiver`, `Bhv_NormalDribble`, `Bhv_StrictCheckShoot`, `FieldAnalyzer`, `ClearGenerator`, `CrossGenerator`.

### Basic actions (`basic_actions/`)
- Helios ships its own copies. No file includes `rcsc/action/*`; across the tree the only librcsc player-action header included is `rcsc/player/soccer_action.h`.
- Alias headers select the current implementation: `body_intercept.h:39` → `Body_Intercept2018`; `body_dribble.h:38` → `Body_Dribble2008`; `body_hold_ball.h:38` → `Body_HoldBall2008`; `body_clear_ball.h:38` → `Body_ClearBall2009`; `body_advance_ball.h:38` → `Body_AdvanceBall2009`. Call sites use the alias names.
- `kick_table.cpp` and `body_smart_kick.cpp` are referenced from the planner (`planner/bhv_pass_kick_find_receiver.cpp`, `planner/bhv_strict_check_shoot.cpp`, `planner/body_force_shoot.cpp`), and `kick_table.cpp` is also included by `sample_player.cpp:65`.

### Set play (`setplay/`)
- `Bhv_SetPlay::execute` (`setplay/bhv_set_play.cpp:74-180`) dispatches by game mode to `Bhv_SetPlayKickOff`, `KickIn`, `GoalKick`, `FreeKick`, `IndirectFreeKick`, `Bhv_GoalieFreeKick`, and `Bhv_TheirGoalKickMove`.

### Communication
- `communicationImpl()` (`sample_player.cpp:515`) calls `M_communication->execute(this)`.
- `SamplePlayer` always constructs `SampleCommunication` (`sample_player.cpp:146`). `KeepawayCommunication` is instantiated only when the server is in keepaway mode (`SamplePlayer::handleServerParam`, `sample_player.cpp:466-473`).
- `SampleCommunication::execute` returns early unless `config().useCommunication()` (`sample_communication.cpp:184-186`). `src/player.conf` sets `use_communication : on`.
- Say parsers are registered in the constructor (`sample_player.cpp:107-126`). The coach's `OpponentPlayerTypeMessage` (sent at `coach/sample_coach.cpp:690`) is parsed by `OpponentPlayerTypeMessageParser` (`sample_player.cpp:141`).

## Patterns & Conventions
- Behaviors are classes with `bool execute(rcsc::PlayerAgent*)`, called as `Bhv_X().execute( agent )` (e.g. `role_sample.cpp:116`).
- Singletons: `Strategy::instance()` / `Strategy::i()`, `FieldAnalyzer::instance()`, `ActionChainHolder::instance()`, `KickTable::instance()`.
- Server constants via a local alias: `const ServerParam & SP = ServerParam::i();` (e.g. `body_hold_ball2008.cpp:195`).
- Debug output: `dlog.addText( Logger::TEAM | ROLE | ..., __FILE__": ..." )`. Categories are switched on by `debug_*` lines in `src/player.conf`.

## Integration Points
- Formations and kick table come from `config().configDir()`, set by `--config_dir` (`strategy.cpp` via `sample_player.cpp:197`). `KickTable::read(<config_dir>/kick-table)` (`sample_player.cpp:203`) is optional: no file of that name exists in the repo, and a failed read is silently skipped.
- Server and team settings come from `src/player.conf` (see [build-run-scripts](build-run-scripts.instructions.md)).

## Build & Test
- Needs C++17, librcsc, and Boost system; zlib is optional (`src/player/CMakeLists.txt:122-128`, `configure.ac:25-64`).
- Build instructions live in [build-run-scripts](build-run-scripts.instructions.md). The repository has no test directories, test files, or CI configuration.

## Important Notes
- **Noise model (verified):** helios never sends `(gaussian_see)`. The string `gaussian` appears nowhere in the repository, including ignored files. It never reads `dist_noise_rate`, `focus_dist_noise_rate`, `land_dist_noise_rate`, or `land_focus_dist_noise_rate`. The match for `seen_dist_noise` (`planner/shoot_generator.cpp:464`) is a local variable. So the observation model is whatever the server uses by default; the server's quantized model is the default, and `gaussian_see` is the client-side switch (`rcssserver-denoise/src/player.cpp:187, 2387-2390`).
- **Margins use server rand parameters:** `ServerParam::playerRand()` and `ballRand()` (`rcsc/common/server_param.h:858, 888`, from the server's init params) scale own and ball velocity in kick and intercept margins: `body_hold_ball2008.cpp` (lines 228, 321, 859, 905, 974-980), `kick_table.cpp` (1433, 1519, 1633, 1761), `body_dribble2008.cpp` (298-299, 321-322, 1960-1962), `planner/bhv_planned_action.cpp` (141-142, 411-412), `planner/bhv_pass_kick_find_receiver.cpp` (181-182, 529-531), `planner/short_dribble_generator.cpp:397`. These are fixed-rate margins and do not depend on which observation model the server uses.
- **Compiled but unused:** `keepaway_communication.cpp` (used only in keepaway mode, see Communication); `actgen_direct_pass.cpp` and `actgen_simple_dribble.cpp` (commented out in `createActionGenerator`); `basic_actions/intercept_evaluator_sample.cpp` is used through `InterceptEvaluator::create("Default")` at `body_intercept2018.cpp:471`.
- **Not built (in neither CMake nor Makefile.am):** `planner/positioning.cpp` (self-references only), `basic_actions/intercept_evaluator_factory.cpp` (registration actually lives in `intercept_evaluator_sample.cpp:47-53`), `basic_actions/obsolete/body_intercept2009.cpp` (self-references only).
- **Not loaded:** `formations-*/goalie-formation.conf` is never read by `Strategy::read`.
- **Keepaway roles:** `KeepawayKeeper` and `KeepawayTaker` appear only in `formations-keeper/normal-formation.conf` and `formations-taker/normal-formation.conf` (see [formations-config](formations-config.instructions.md)).

## Deep Dives
- [observation-view-control](observation-view-control.instructions.md) — view width, neck turning, `synch_see`, count-driven scanning
- [communication-say-hear](communication-say-hear.instructions.md) — say selection, attentionto, heard-data use, coach messages
- [noise-handling-in-decisions](noise-handling-in-decisions.instructions.md) — every noise margin / `posCount` threshold, plus denoising hook points

## See Also
- [formations-config](formations-config.instructions.md) — formation JSON, sets, and loading
- [coach-trainer](coach-trainer.instructions.md) — coach and trainer agents
- [build-run-scripts](build-run-scripts.instructions.md) — build systems, start scripts, eval/ and ssil/
- [../copilot-instructions.md](../copilot-instructions.md) — repository map
