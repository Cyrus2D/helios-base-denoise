---
applyTo: "src/player/{neck_*,view_*,bhv_scan_field,bhv_basic_move,sample_player,body_*}*,src/player/basic_actions/{neck,view}*"
---
# Observation, View & Neck Control (helios-base)

## TL;DR
Helios never asks for better sensor data; it only steers WHERE and HOW WIDE the player looks, using librcsc age counters (`posCount`, `seenPosCount`, `dirCount`, `ghostCount`) to decide what is stale.
- View width comes from `View_Tactical` (`view_tactical.cpp:49`; returns at once when the server param `full_view` is on), set every cycle in `SamplePlayer::doPreprocess`; quality is never lowered (`ViewQuality::LOW` appears nowhere in `src/`).
- The neck is a per-behavior choice: `Neck_ScanField` (look at the least-recently-seen angles), `Neck_TurnToBallOrScan(count_thr)`, `Neck_TurnToBall`, and the intercept necks.
- `synch_see : on` and `normal_view_time_thr : 20` (`src/player.conf:7-8`) make `View_Synch` pick narrow/normal so that `see` arrives in step with `sense_body`.
- There is no use of `focus` (`Focus_*` files are built but never called), no `pointto`, and no gaussian/fullstate switch in C++ (`gaussian_see` is an optional `player.conf` key read by librcsc).
- **Open the full file when:** changing view width, neck targets, ball-search logic, or count thresholds that trigger a look.

## View width
- `SamplePlayer::doPreprocess` sets `View_Tactical` in all normal paths: frozen (`sample_player.cpp:549`), before kick-off (`:564`), ball search (`:591`), default (`:600`). Behaviors can override with `setViewAction` (last call wins).
- `View_Tactical::execute` (`view_tactical.cpp:49-97`): first `if ( ServerParam::i().fullView() ) return true;` (`:54-58`; the server then sends a 360° `see` every cycle, so no view action is needed; the same guard is at the top of `View_Synch::execute`, `view_synch.cpp:52-56`); BeforeKickOff / AfterGoal / penalty setup modes → `View_Wide`; `PenaltyTaken_` → `View_Synch`; our goalie free kick → wide for goalie (`:225-233`); otherwise `doDefault`.
- `doDefault` (`view_tactical.cpp:104-217`), in order:
  1. `! ball.posValid()` → WIDE (`:108`).
  2. Reach step = `min(self, mate, opp)` from `InterceptTable`; `ball_dist` = distance from queued next self pos to `ball.inertiaPoint(step)`.
  3. Goalie, ball not kickable and far/outside box: WIDE if the ball angle is outside neck range + normal half-width − 3, NORMAL if outside narrow (`:116-145`).
  4. `ball_dist > 40` → WIDE; `> 20` → NORMAL; `> 10` and ball angle from body `> 120°` → WIDE.
  5. Field player, nearest mate and opp both `> 5` from ball, `ball_dist > 10` → NORMAL (`:196-206`).
  6. Else `View_Synch`.
- `View_Synch::execute` (`basic_actions/view_synch.cpp:50-99`; returns true immediately under `full_view`): `SeeState::synch_see_mode()` → NARROW; not yet synchronised → `doTimerSynchView` (NARROW first, then NORMAL if the `see` arrived `< normalViewTimeThr` ms after `sense_body`, `:106-170`); synchronised: NORMAL when the last timing was `TIME_0_00`, else NARROW. Not-in-PlayOn and not synchronised returns false (no change).
- Standard server trade-off (not read from config here): narrow = 60° cone with the fastest `see` rate, normal = 120°, wide = 180° with the slowest rate (see the server `visual-observation-detail` instruction). The code assumes this timing; it never reads `visual_detail` or noise parameters.
- `Bhv_Emergency` (`basic_actions/bhv_emergency.cpp:51-62`, used when own position is invalid): `View_Synch` if quality is not HIGH, then `Bhv_ScanField`.

## Neck control (who calls what)
| Situation | Neck action | Where |
|---|---|---|
| Role default, not kickable | `Neck_ScanField` | `role_sample.cpp:106`, `role_*.cpp:~100` |
| `Bhv_BasicMove` idle | `Neck_TurnToBallOrScan(0)`; `Neck_TurnToBall` if opp kickable and ball < 18 | `bhv_basic_move.cpp:117-121` |
| Chasing ball | `Neck_OffensiveInterceptNeck` | `bhv_basic_move.cpp:88`, `intention_receive.cpp:159-192` |
| Goalie | `Neck_GoalieTurnNeck` / `Neck_TurnToBall` | `bhv_goalie_basic_move.cpp:131, 396...` |
| Kick via planner | `Neck_TurnToReceiver` (pass) | `planner/neck_turn_to_receiver.cpp` |
| Heard pass receive | `Neck_TurnToBall` | `sample_player.cpp:738, 751` |

- `Neck_TurnToBallOrScan(count_thr)` (`basic_actions/neck_turn_to_ball_or_scan.cpp:53-97`): if `ball.posCount() <= count_thr` → `Neck_ScanField` (ball is fresh, look elsewhere); if the ball is `posCount <= 0` and within `visibleDistance − 0.2` and nobody is kickable → scan; if the ball can never be faced (`> maxNeckAngle + view/2 + 2`) → scan; else `Neck_TurnToBall`. `count_thr = -1` means "always look at the ball unless impossible".
- `Neck_TurnToBall` (`basic_actions/basic_actions.cpp:276-`): never-look → scan; opponent can reach the ball in `<= 1` step → face the ball exactly; otherwise pick a neck window around the ball and, with 11 opponents known, use `Neck_ScanPlayers::get_best_angle` inside that window to also refresh player counts (`:354-375`).
- `Neck_ScanField` (`neck_scan_field.cpp:61-171`), cached per cycle and view width. Order:
  1. Wide view near a pitch edge → look to the field centre (`calcAngleForWidePitchEdge`, `:356`).
  2. If no player within 30 m is a ghost → `Neck_ScanPlayers::get_best_angle` (maximises player accuracy).
  3. Else `calcAngleDefault` (`:178-349`): slide a view-width window over the neck range and choose the angle with the largest sum of `wm.dirCount(angle)` (oldest-seen directions), optionally rejecting angles that look outside the pitch.
- `Neck_ScanPlayers::calculate_score` (`neck_scan_players.cpp:237-327`): for every other player in the cone (5° inside each edge), `score += (seenPosCount + 1)² · exp(−d²/(2·20²))`; ghosts with `ghostCount >= 5` ignored, odd-ghost count capped at 2; teammates ahead of the ball/x>30 count ×2 when our team has the ball; the sum is multiplied by `1 + edge_buffer/90`. Needs all 22 players known (`:126`).
- `Neck_TurnToLowConfTeammate` (`neck_turn_to_low_conf_teammate.cpp:60-125`): picks the farthest-forward visible teammate with the largest `posCount` (> 1) within 40 m; falls back to `Neck_TurnToBallOrScan(0)`.
- `Neck_TurnToGoalieOrScan` / `Neck_TurnToBallAndPlayer` compare `posCount <= count_thr` and treat `isGhost()` as unseen (`neck_turn_to_goalie_or_scan.cpp:64-99`, `neck_turn_to_ball_and_player.cpp:67-79`).

## Intercept neck + view coupling (`neck_offensive_intercept_neck.cpp:64-270`)
- Opponent kickable → `Neck_TurnToBallAndPlayer(opp, 0)`.
- View by remaining intercept steps (`self_min`) and ball freshness: `self_min == 4 && opp_min >= 3` and (`ball.seenPosCount()==0` or slow trap) → WIDE; `self_min == 3` and `seenPosCount <= 1` → NORMAL (`:123-146`); ball not in visible range and angle outside the cone → WIDE/NORMAL (`:148-166`); `self_min > 15/10` → NORMAL; `<= 6` → `View_Synch`.
- Neck choice: `self_min == 5` with a fast ball → look at ball; ball near their box → `Neck_TurnToGoalieOrScan`; `ball.posCount() <= 1` → `Neck_TurnToLowConfTeammate`; else `Neck_TurnToBallOrScan` with `count_thr = -1` when `self_min <= 5 && seenPosCount >= 2` and (wide/normal view or `seenVelCount >= 2`) (`:255-266`).
- `Neck_DefaultInterceptNeck::doTurnToReceiver` (`neck_default_intercept_neck.cpp:116-197`): when about to kick a pass (`selfStep < 2`) and the receiver is not freshly seen (`posCount > 0`), force `View_Synch` and face the receiver (+3 m ahead if far).

## Search and emergency
- `doPreprocess` (`sample_player.cpp:582-594`): `ball.posCount() > 5` (goalie `> 10`), or non-PlayOn and `seenPosCount > count_thr + 10` → `Bhv_NeckBodyToBall`.
- `Bhv_ScanField::findBall` (`basic_actions/bhv_scan_field.cpp:85-135`): forces WIDE, requires a new `see` since the last decision, faces the last seen ball position (or field centre) and flips 180° every 3 `lostCount` cycles. `scanAllField` (`:142-170`) turns the body by (old+new view width)/2 each cycle with the neck fixed.
- `Bhv_GoToPointLookBall` (`bhv_go_to_point_look_ball.cpp:62`) looks at the ball when `posCount > 5`.

## Relation to the world model
- Counters read: `ball.posCount/seenPosCount/seenVelCount/velCount/lostCount`, `player.posCount/seenPosCount/ghostCount/isGhost`, `wm.dirCount(angle)`, `self.posValid()`. All are librcsc ages, not error bounds (see librcsc `observation-fusion-denoising.instructions.md`).
- Nothing reads `posError`/`velError` or `ViewWidth` noise effects; narrower view gives a fresher `see` but a smaller cone, and the code trades this only through the distance/step rules above.

## Not found
- No `change_view` quality changes, no `focus`/`attentionto` use for vision, no `see`-timing adaptation beyond `View_Synch`, no adaptation to `gaussian_see` or to `visual_noise` parameters.

## See Also
- [player-behaviors](player-behaviors.instructions.md) · [communication-say-hear](communication-say-hear.instructions.md) · [noise-handling-in-decisions](noise-handling-in-decisions.instructions.md)
- [../copilot-instructions.md](../copilot-instructions.md)
