# Smallest IPC instances, tracked

`data/ipc` is gitignored (`.gitignore`, `data/ipc`), so a clean checkout does not
have it and any test reading from it fails with "File does not exist" on CI.

These are byte-for-byte copies of the instances the landmark tests use:

* `<domain>/{domain.pddl, p69.pddl}` — `data/ipc/<domain>/train/`, the smallest
  instance of each of the ten shipped IPC domains (the soundness oracle, the
  determinism sweep and the initially-true sweep run over all ten).
* `miconic-ipc-hard/{domain.pddl, p30-hard.pddl}` — `data/ipc/miconic-ipc/test/`,
  485 passengers over 196 floors: the instance that pins the §2.5 producibility
  filter, where `origin(?, ?)` used to carry 95,060 unreachable members.

524 KB in total. Regenerate with `tools/`-free plain copies if `data/ipc` moves.
