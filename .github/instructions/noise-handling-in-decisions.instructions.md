---
applyTo: "src/player/**"
---
# Noise Handling in Helios Decisions (src/player)

## TL;DR
Helios has no observation-noise model: it takes librcsc's fused `pos()/vel()` as truth and compensates only with (a) server MOVEMENT noise margins (`playerRand`, `ballRand`, `kickRand`), (b) fixed kickable-area shrinkage, and (c) age-count (`posCount`) thresholds and bonus steps.
- `playerRand()/ballRand()` are movement/kick noise parameters, not the `see` distance/direction noise; none of the see-noise knobs (`dist_noise_rate`, `focus_dist_noise_rate`, `land_*`) is read by helios code, and `gaussian_see` is only an optional `player.conf` key handled inside librcsc (see §5).
- Uncertainty of an opponent = `posCount()` converted to extra reach steps (`bound(0, posCount, 3..8)`); uncertainty of a teammate/ball = thresholds.
- `posError`/`velError`/`pos_error` are never used in `src/player` (grep verified).
- `fullstate` appears only in the start scripts (`src/start.sh.in:258-277`, `--use_fullstate`), not in C++.
- **Open the full file when:** adding denoising, changing a margin or `posCount` threshold, or hooking filtered pos/vel into planning.

## 1. Movement-noise margins (`playerRand` / `ballRand` / `kickRand`)
All use `ServerParam::i()`; pattern `kickableArea − |ball_vel|·ballRand − |my_vel|·playerRand − 0.15`.
| File:line | Use |
|---|---|
| `basic_actions/body_hold_ball2008.cpp:228, 321, 406, 859, 905, 974-981` | hold-ball candidate filter: `(my_noise + ball_noise + kickRand) * 0.95 < kickableArea − dist − 0.1` |
| `basic_actions/kick_table.cpp:1433, 1519-1525, 1633, 1761-1766` | 2- and 3-step kick feasibility: `my_noise + ball_noise + max_kick_rand > kickableArea − dist − 0.05` rejects |
| `basic_actions/body_dribble2008.cpp:297-300, 321-322, 1960-1971` | turn-only / dash-then-kick checks; `noise_buf` uses 0.5 factors |
| `planner/bhv_planned_action.cpp:140-144, 411-412` | "next kickable?" test before turning/holding |
| `planner/bhv_pass_kick_find_receiver.cpp:180-184, 529-541` | `noised_kickable_area` before `doKeepBall` |
| `planner/short_dribble_generator.cpp:397-398, 405, 422, 467` | `self_noise = min(0.1, move·playerRand)`, `ball_noise` likewise; subtracted from kickable area (+0.15) |
Obsolete copy: `basic_actions/obsolete/body_intercept2009.cpp:850-884` (not built).

## 2. Fixed kickable-area inflation/shrinkage (no noise term)
- Shrink by constants: `body_intercept2018.cpp:170` (−0.2), `:377` (−0.3); `body_stop_ball.cpp:124-126` (−0.1); `intention_dribble2008.cpp:419-447` and `planner/bhv_normal_dribble.cpp:504-532` (−0.1/−0.15/−0.2); `body_dribble2008.cpp:1162-1182`; `kick_table.cpp:144, 168, 200` (−0.2); `bhv_penalty_kick.cpp:796`.
- Inflate for opponents: `kickableArea + 0.3` in `neck_offensive_intercept_neck.cpp:108`, `basic_actions.cpp:340-341`; `body_kick_to_relative.cpp:75` (+0.15 when estimating opponent threat).
- Kick-table distance bands use `kickableMargin × {0.4, 0.6, 0.75}` (`body_hold_ball2008.cpp:207-215`, `kick_table.cpp:138-200`).

## 3. Seen-distance noise (single place)
- `planner/shoot_generator.cpp:464`: `seen_dist_noise = goalie->distFromSelf() * 0.02`, subtracted from goalie target distance when the goalie is in the penalty area (`:500-503`). Hard-coded 2% — approximates quantised `see` distance noise, independent of server settings.

## 4. Age-count (`posCount`, `seenPosCount`, `velCount`, `bodyCount`, `ghostCount`) usage
Ball:
- `sample_player.cpp:582-594`: search when `ball.posCount() > 5` (goalie 10) or `seenPosCount > count_thr+10` (not PlayOn).
- `sample_player.cpp:727-728`: heard-pass receive needs `posCount <= 1 && velCount <= 1`.
- `body_intercept2018.cpp:306-313`: if `reachStep >= 2` and `seenPosCount > 3` face the ball instead of the preset point.
- `neck_offensive_intercept_neck.cpp:125-139, 215, 238, 257-260`; `neck_turn_to_ball_or_scan.cpp:61-73` (see observation-view-control).
Opponents (staleness → faster-than-seen reach):
- Bonus steps: `planner/bhv_planned_action.cpp:391` (`dist −= bound(0,posCount,3)·realSpeedMax`); `self_pass_generator.cpp:638, 702, 717` (up to 8/3); `short_dribble_generator.cpp:813-817` (8/4); `shoot_generator.cpp:530-558, 674-718` (goalie `min(5,posCount)` inside box else `posCount−1` ≤1); `strict_check_pass_generator.cpp:1725, 1742`; `clear_generator.cpp:536`, `tackle_generator.cpp:779` (≤3); `body_pass.cpp:1047, 1198`; `body_dribble2008.cpp:817`; `bhv_penalty_kick.cpp:529, 642`; `intercept_evaluator_sample.cpp:221-225` (first opponent `min(5,posCount)`).
- Skip stale opponents: `posCount > 10` (`bhv_basic_tackle.cpp:282`, `body_hold_ball2008.cpp:567`, `body_pass.cpp:1043`, `shoot_generator.cpp:414`, `strict_check_pass_generator.cpp:393`); `>=10` (`body_dribble2008.cpp:1867, 2170, 2272`); `>5` (`:1561, 1987`); `>=8` (`kick_table.cpp:1006, 1193`); `>=4` (`body_kick_to_relative.cpp:67`, `cross_generator.cpp:291`); `field_analyzer.cpp:606` (`valid_opponent_threshold`); `simple_pass_checker.cpp:55, 200` (`VALID_OPPONENT_ACCURACY = 20`). Ghosts are skipped in most of these.
Teammates:
- `actgen_shoot.cpp:44, 58` and `actgen_direct_pass.cpp:115, 253-256` (`VALID_PLAYER_THRESHOLD = 10`; plus `unumCount`); `simple_pass_checker.cpp:54, 84` (`VALID_TEAMMATE_ACCURACY = 8`); `predict_state.cpp:46` (8); `field_analyzer.cpp:768, 1177-1179` (`posCount>=5`, `unumCount>=10`, `ghostCount>=2`); `body_pass.cpp:258` (`>3`), `:391-395` (velocity scaled by `velCount`), `:1299` (`0.98^posCount` confidence); `bhv_set_play_indirect_free_kick.cpp:232, 351` (`>5`).
- Receiver freshness: `bhv_pass_kick_find_receiver.cpp:151, 342-365, 482` (`seenPosCount <= 3` to kick without checking; else look first; `isGhost` → hold).
Seen vs fused:
- `strict_check_pass_generator.cpp:106-124`, `field_analyzer.cpp:299-303`, `body_dribble2008.cpp:759-767`, `self_pass_generator.cpp:606-624`: use `seenPos()/seenVel()` when `seenPosCount <= posCount` (i.e. when the raw see is at least as new as the heard/fused value), else `pos()/vel()`. `field_analyzer.cpp:87-89` derives `estimate_virtual_dash_distance` from `min(seenPosCount, posCount, 10)`.
Direction accuracy: `shoot_generator.cpp:209-220` rejects shots when the goalie is stale (`5 < posCount < 30`) and `wm.dirCount(ball_move_angle) > 3`.
Interception margin: helios relies on librcsc `InterceptTable` (reach steps) and adds only the `-0.2/-0.3` shrink and `+3` step slack in `intercept_evaluator_sample.cpp:226-228`; no noise inflation of ball position.
Self: `posValid()` only (`sample_player.cpp:571`, `basic_actions.cpp:91, 490`); `velValid()` guards in `body_*` (`body_dribble2008.cpp:87`, `body_hold_ball2008.cpp:92`, `body_kick_one_step.cpp:77`, `body_smart_kick.cpp:75`, `body_stop_ball.cpp:70`).

## 5. Observation-model switches
- `gaussian_see`: helios C++ never sends or reads it (no `gaussian` string in the repo). It can be enabled with `gaussian_see : on` in `src/player.conf`: librcsc reads it (`player_config.cpp:224`) and sends `(gaussian_see)` (`player_agent.cpp:1367`). It is absent from the shipped `player.conf`, and `start.sh` has no flag for it. The server param `see_noise_free` makes the server ignore it for distance.
- `fullstate`: only `--fullstate ignore|reference|override` options in `src/start.sh.in:258-277` → `--use_fullstate`/`--debug_fullstate` passed to librcsc; no C++ branch. `--fullstate reference` plus `--denoise-log-dir` is the dataset-logging setup (see [build-run-scripts](build-run-scripts.instructions.md)).
- `synch_see : on`, `normal_view_time_thr : 20` are the only see-related knobs (`src/player.conf:7-8`).

## Candidate hook points for helios-side denoising
1. A per-cycle post-update filter step at the top of `SamplePlayer::actionImpl` (`sample_player.cpp:219`) or `handleActionStart` (`:323`, empty) to smooth/override `wm` ball and opponent pos/vel before `ActionChainHolder::update`.
2. The "seen vs fused" selector (`strict_check_pass_generator.cpp:106-124`, `field_analyzer.cpp:299-303`, `self_pass_generator.cpp:606-624`): replace with one shared accessor returning a filtered state.
3. Margin helpers: centralise `kickableArea − ballRand·|v| − playerRand·|v| − 0.15` (6 copies in §1) and add an observation-error term (e.g. `see`-distance noise ∝ distance, like `shoot_generator.cpp:464`).
4. `posCount` → error: replace the many `bound(0, posCount, N)` bonuses (§4) with a growth model in one function (`FieldAnalyzer::estimate_virtual_dash_distance`, `field_analyzer.cpp:85`).
5. Count thresholds as tuning constants: `sample_player.cpp:582`, `simple_pass_checker.cpp:54-55`, `actgen_shoot.cpp:44`, `predict_state.cpp:46`.
6. Neck/view: use predicted error growth (instead of `seenPosCount`) in `Neck_ScanPlayers::calculate_score` (`neck_scan_players.cpp:268-289`) and `View_Tactical::doDefault`.
7. Communication: richer ball/player relays (`sayBallAndPlayers`, `sample_communication.cpp:594`) and re-enabling `saySelf`/offside-line messages; decode-side fusion in librcsc.
8. Config: `(gaussian_see)` is sent only if librcsc `gaussianSee()` is enabled (default false) — no helios change needed beyond `player.conf`.

## Not found
- No use of `posError`, `velError`, `rposError`, variance/covariance, Kalman filter, or any filtering in helios; no gaussian_see/fullstate C++ handling.

## See Also
- [observation-view-control](observation-view-control.instructions.md) · [communication-say-hear](communication-say-hear.instructions.md) · [player-behaviors](player-behaviors.instructions.md)
- [../copilot-instructions.md](../copilot-instructions.md)
