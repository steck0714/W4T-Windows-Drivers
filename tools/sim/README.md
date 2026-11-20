# tools/sim

`sim_findagent.py`: a line-by-line model of `drivers/t214-aula/kit/agent/findagent.cmd` (finding the boot partition by
`\agent\w64.cmd` when its drive letter is anything, or none). It runs 3,200 combinations (partition letter, letterless decoy
volumes, a duplicate `\agent` folder, `\\?\Volume{GUID}\` openable or not, mountvol assigning or not, a second letter on a volume
that already has one, free letters scarce or none) and checks: existing letters are never changed, A: B: C: are never touched,
no temporary letter is left behind, a second run finds it in step 1.

It models the algorithm, not cmd parsing. Run: `python3 tools/sim/sim_findagent.py` (prints `invariant violations: 0`).
