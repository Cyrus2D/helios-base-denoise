---
applyTo: "src/player/{sample_communication,keepaway_communication,sample_player}*,src/player/*say*,src/coach/**"
---
# Communication (say / hear / attentionto) in helios-base

## TL;DR
Every cycle `SampleCommunication::execute` builds at most one compact say string (ball + up to 3 players, or goalie/stamina extras) within the 10-char budget, while behaviors add pass, wait and set-play messages; heard data is decoded by librcsc and only patches stale tracks.
- Active planner: `sayBallAndPlayers` + `sayStamina` + `attentiontoSomeone` (`sample_communication.cpp:228-237`); the older `sayBall/sayGoalie/saySelf/sayPlayers` branch is `#else` dead code.
- Teammates take turns (`updateCurrentSender`, `:247`) and `attentionto` is aimed at the expected sender or ball holder, because the server delivers one heard message per group per cycle.
- Received messages are used by librcsc's world model (ball/player/goalie/stamina/lines) and by 4 helios spots (heard pass receive, pass request, set-play wait, recovery).
- **Open the full file when:** changing what is said, message priority/scoring, who speaks or listens, or coach messaging.

## Setup
- Parsers for all 20 player say types are registered in `SamplePlayer` ctor (`sample_player.cpp:107-126`) plus coach freeform `OpponentPlayerTypeMessageParser` (`:141`); the coach registers the same 20 (`coach/sample_coach.cpp:91-110`). Freeform player parsers are commented out (`:128-136`).
- Config: `use_communication : on`, `hear_opponent_audio : off` (`src/player.conf:13-14`). `execute` returns at once if `useCommunication()` is false (`sample_communication.cpp:186`).
- `SamplePlayer` builds `SampleCommunication` (`sample_player.cpp:146`); `handleServerParam` swaps in `KeepawayCommunication` only when the server is in keepaway mode (`:466-473`). `communicationImpl` calls `execute` (`:515-521`).

## Per-cycle selection (`SampleCommunication::execute`, `:184-240`)
1. `updateCurrentSender`: if something is already queued, I am the sender; else a round-robin index `cycle % N` over unums 1-11 (goalie excluded unless ball x < −10 or not PlayOn) gives `current` and `next` sender (`:247-322`).
2. Recovery: non-PlayOn, I am the sender, recovery below initial → `RecoveryMessage` (`:196-204`).
3. BeforeKickOff / AfterGoal / penalty shoot-out: stop here.
4. `sayBallAndPlayers` (`:594-1105`), then `sayStamina` (adds `StaminaMessage` only if something is already queued and 2 chars fit, `:2093-2112`), then `attentiontoSomeone`.
- Gate (`:604-613`): speak only if `shouldSayBall`, `shouldSayOpponentGoalie`, I am the current sender, or the buffer is non-empty.
- `shouldSayBall` (`:356-515`): requires `ball.seenPosCount()==0` and `seenVelCount<=2` (I just saw it well); not when a teammate is kickable; says when ball velocity changed (speed +0.1, speed < 0.5·decay·prev, or turn > 20°) after an opponent kick, when I am kickable and the next ball speed > 1, when I am the nearest teammate (opp reaches in `<=1` step), or as supporter of a nearest teammate at 1-6 m.
- Priority scores (`:619-772`): index 0 ball, 1-11 mates, 12-22 opponents; base score = cycles since last heard (`AudioMemory` times); ball forced to −65535 if not seen this cycle or a mate is kickable, 1000 if I am kickable / `shouldSayBall` / velocity changed; players need `unumCount < 2`, score × `distance_rate(dist from predicted ball, variance 30, x 1.0, y 0.5)` × `0.3^unumCount`. Illegal entries are erased, then sorted; ball + 1 player or up to 3 players are chosen (`:806-838`). Known bug: opponent branch multiplies `objects[unum]` (teammate slot) at `:767`.
- Emitted type by remaining length (`:864-1105`): `BallPlayerMessage` (10 chars; me or top player with `pos+vel`, body), `BallMessage` (6), `BallGoalieMessage` (10, when `shouldSayOpponentGoalie`), `GoalieAndPlayerMessage` / `GoalieMessage` (ball x > 34, goalie freshly seen), `Three/Two/OnePlayerMessage`. Ball velocity is zeroed when a kicker is adjacent or I will stay kickable (`:845-859`). Sent positions use `queuedNextBallPos`, `queuedNextSelfPos`, or `pos+vel`.

## Messages sent from behaviors (not from `SampleCommunication`)
| Message | Where | When |
|---|---|---|
| `PassMessage` | `planner/bhv_pass_kick_find_receiver.cpp:964-998` (`doSayPass`), also `basic_actions/body_pass.cpp:140` | pass kick, only if `useCommunication` and ball will not stay kickable |
| `WaitRequestMessage` | `setplay/bhv_set_play_free_kick.cpp:398`, `_goal_kick.cpp:479`, `_kick_in.cpp:429`, `_indirect_free_kick.cpp:552` | set-play taker waits for tired/far teammates |
| `BallMessage` | `setplay/bhv_set_play_indirect_free_kick.cpp:274` | forced pass |
| `DribbleMessage` | disabled by `if ( 0 )` (`basic_actions/body_dribble2008.cpp:113`), `intention_dribble2008.cpp:217` | not sent from `Body_Dribble2008` |
- `sayBall/sayGoalie/saySelf/sayPlayers` are called only in the dead `#else` branch (`:232-235`); `sayIntercept`, `sayOffsideLine`, `sayDefenseLine`, `sayOpponents` have no caller at all (defined `:1362-2080`). So offside/defense lines, self pose and intercept are NOT sent. `PassRequestMessage`, `Teammate/OpponentMessage` (from `sayOpponents`), `StaminaCapacity` are never sent by active code.

## Attention (`attentiontoSomeone`, `:2140-2290`)
- Near the offside line and about to reach the ball (`self.x > offsideLine − 15`, `selfStep <= 3`): listen to the current sender, else to the most-forward visible teammate within 20 m (`:2144-2197`).
- Else listen to the fastest teammate to the ball (`mate_min <= 1`, `< self_min`, `<= opp_min+1`, `<= 5 + min(4, posCount)`, within 35 m), or the ball-nearest teammate when opp has the ball, when `ball.posCount() >= 3`, or when it is within 3.5 m (`:2213-2266`); otherwise listen to the current sender or turn `attentionto` off.
- `KeepawayCommunication` (`keepaway_communication.cpp:120-167`) is a trimmed copy of the same rules.

## How received data is used
- Librcsc side (`WorldModel::updateBallByHear`, `updatePlayerByHear`, goalie, stamina, offside line): see librcsc `communication-audio-denoising.instructions.md`. It overrides a track only when stale (`posCount >= 2/5`) or inconsistent. Heard direction is ignored.
- Helios side:
  - `SamplePlayer::doHeardPassReceive` (`sample_player.cpp:705-761`): a heard pass to me this cycle → `Body_Intercept` if ball `posCount/velCount <= 1` and `selfStep < 20`, else `Body_GoToPoint(heard receive_pos)`; sets `IntentionReceive`, neck on ball.
  - Pass request: `strict_check_pass_generator.cpp:941-944` (requests within 10 cycles), `bhv_normal_dribble.cpp:159`, `intention_dribble2008.cpp:87` stop dribbles.
  - `setplay/bhv_set_play.cpp:572`: uses `recoveryTime` of teammates.
  - Trainer message only logged (`sample_player.cpp:221-228`).

## Coach (`src/coach/sample_coach.cpp`)
- Sends no player-visible `clang`/`say`. `sayPlayerTypes` (`:647-725`) sends freeform `OpponentPlayerTypeMessage` ("pt", up to 11 types; `sample_freeform_message.cpp:49-160`) when `useFreeform()` and `canSendFreeform()` and some type changed; players parse it into the world model (`sample_player.cpp:141`). It also substitutes players (`doSubstitute`, `:257`).
- This conveys opponent stamina/speed class, not noise-related data.

## Noise relevance
- Useful: the sender relays its own seen ball (pos 0.1 m, vel quantised) and up to 3 players (~0.63-1.0 m steps), refreshing far/unseen tracks; sender selection by `seenPosCount==0` ensures only well-seen ball data is shared. `attentionto` improves the chance the useful message is the one delivered.
- Not used: own-pose broadcast (`saySelf` dead), offside/defense lines, goalie position beyond the penalty box, direction of hearing, any use of messages to correct self localization, freeform messages between players, `hear_opponent_audio`.

## See Also
- [observation-view-control](observation-view-control.instructions.md) · [noise-handling-in-decisions](noise-handling-in-decisions.instructions.md) · [player-behaviors](player-behaviors.instructions.md) · [coach-trainer](coach-trainer.instructions.md)
- [../copilot-instructions.md](../copilot-instructions.md)
