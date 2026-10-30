# State trees: future work

Gaps against RedStateTree (`Engine/Plugins/RED/StateTree/RedStateTree`) and things spotted while moving the STAI
trees to its structure (branch `statetree_update_prototype`). Roughly by value.

## Verify first

- [ ] Build the server with the branch and run bots in game. mangos fetches PlayerBots with `GIT_TAG "viger_server"`
  (`src/CMakeLists.txt`): configure with `-DFETCHCONTENT_SOURCE_DIR_PLAYERBOTS=<this checkout>` (or point the tag at
  this branch once it is pushed). Only the state tree core was compiled
  (`tools/StateTreeHostTest`, host stubs instead of the game), the game's tasks and providers went through a mechanical
  rename (`OnEnter` -> `OnEnterState`, `OnExit` -> `OnExitState`, `IsTickable` -> `ShouldCallTick`, `GetTaskClass` ->
  `GetNodeClass`, `STAI_Task` -> `STAI_TaskBase`, `STAI_GlobalTask` -> `STAI_DataProviderBase` for providers) that no
  compiler has checked yet.
- [ ] `Warrior/STAI_Combat_Prot_Warrior.xml` "Charge": its two tasks ran one after another, the converter made it a
  parent state with the children "Charge Stance" (`OnStateSucceeded` -> `NextState`) and "Charge CastSpell"; check the
  stance switch and the charge in game.
- [ ] BotStateTree addon (client side): the snapshot's `C` / `V` records got a trailing `^<depth>` field (nested
  states) and transitions read `<trigger> <target>` (`OnStateCompleted Root/Idle`, `OnDelegate ... Root/...`); indent
  nested states and show the new trigger names. The map app's JSON nodes got `depth` / `parent`.
- [ ] The tasks of an active state now run at the same time (RedStateTree semantics), they used to run one after
  another; trees written for the old order must use child states with `NextState` (the converter does it).

## Runtime (RedStateTree capabilities not ported yet)

- [ ] Property functions (`FRedSTPropertyFunctionBase`: BooleanAlgebra, MathExpression, Randoms, VectorAlgebra,
  EvaluateCurve): bindings only copy a property; e.g. `<Binding local="x" external="..." function="Not"/>` or
  property function nodes feeding a binding.
- [ ] Tree parameters and parameter overrides of linked assets: `Task_RunSubtree` cannot pass values into its subtree;
  a `type="LinkedAsset" asset="statetree/..."` state with parameters would replace it (UE linked asset states).
- [ ] Run parallel tree (`FRedSTTask_RunParallelStateTree`): the Reaction tree could be a parallel tree of each main
  state instead of a separate HLS state, and global behaviours (trading, loot) could be shared that way.
- [ ] Weak execution context (`FRedStateTreeWeakExecutionContext::FinishTask` / `BroadcastDelegate` from callbacks):
  tasks have to poll in `OnTick` (Task_CastSpell waits for the spell go every update); an async finish would let most
  tasks stop ticking.
- [ ] Scheduled ticks (`IsScheduledTickAllowed`): a tree whose active states only wait for delegates / events could
  sleep until one fires instead of being updated every bot update (big CPU saving with many idle bots).
- [ ] Events: only reach the tree that sent them (no propagation through the `Task_RunSubtree` stack), carry no
  payload, and nothing outside the trees sends them yet (chat commands, `PlayerbotAI` callbacks such as combat start
  could become events instead of HLS transitions).
- [ ] Considerations: scores are multiplied; UE / Red combine them with operands (And / Or) and indent and use rich
  curves with tangents (here piecewise linear keys).
- [ ] `TryFollowTransitions` only follows transitions to states below the state; UE follows any target.
- [ ] Transition delay random variance (UE `DelayRandomVariance`); `Task_Wait` already has its deviation.
- [ ] Per node custom debug data (`AddCustomDebugData` / `AddCustomDebugFunc`) into the snapshot, e.g. the scores of
  the considerations and the inputs of conditions at the last selection.
- [ ] Profiling: every task has its own `TRACE_CPU_PROFILER_ENTRY_SCOPE`; the `STAI_TaskBase` / `STAI_ConditionBase`
  wrappers could open one per node class like `RED_GENERATE_ST_TASK_STATS`.
- [ ] Schema per tree kind (`URedStateTreeSchema`: allowed node classes, state types, selections): e.g. no movement
  goals in Reaction trees, checked by the loader and the editor.

## Playerbots specific

- [ ] Global tasks with `<Conditions>` (`bActive`, movement goals) are a playerbots concept; in RedStateTree terms
  the goals would be tasks of states with enter conditions, consider moving them there.
- [ ] The failed state retry delay could be a condition (`Condition_Cooldown`) instead of a state attribute.
- [ ] The HLS `StateMachine` (NonCombat / Combat / Dead + additive Reaction) could itself be the root of one tree with
  linked subtrees and event driven transitions, as the RED AI does.
- [ ] Per bot memory: conditions and considerations got contexts of their own (`STAI_TaskContext`, ~100 bytes each);
  a smaller node context for nodes without data would save memory with many bots.
- [ ] XML class names keep their history (`*_GlobalTask`, `Task_*`, `STAI_*`); aliases with a common scheme
  (`DP_*` for data providers) would make the editor's lists easier to read.
- [ ] Trees are loaded only at server start; a `.statetree reload` GM command (bots keep the old shared assets until
  their state restarts) would make editing with the editor much faster.
- [ ] The editor reads node classes from `schema.json`, generated by scanning the sources (`gen_schema.py`); the server
  could dump it from the factory instead (exact property types, settings declared by each node class).
- [ ] Port `tools/StateTreeHostTest` into the AutomationTest suite (`Tests/`), so tree regressions are caught by the
  normal test run.
