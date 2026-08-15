# Versioning

`pymimir` is versioned `MAJOR.MINOR.PATCH`, declared once in `setup.py` as `__version__` and
forwarded to CMake as `MIMIR_VERSION_INFO`.

## The rule

| Component | Bump it when |
|---|---|
| `MAJOR` | An existing binding is removed, renamed, or changes meaning in a way that breaks a caller written against the previous version. |
| `MINOR` | **Anything new becomes visible from Python**: a new class, function, enum value, option field, or keyword argument. Also: an existing binding gains capability a caller could branch on. |
| `PATCH` | Nothing on the Python surface changed. Bug fixes, performance work, C++-only refactors, build changes. |

The `MINOR` rule is the one that matters, and it is deliberately broad: a *new option field* counts.
That is what lets a downstream project express a real floor.

```python
# Before: the only way to find out whether the wheel has the feature.
if not hasattr(search.IWOptions, "landmark_novelty_graph"):
    raise RuntimeError("pymimir too old")
```

```
# After: an ordinary dependency constraint, resolvable before anything is installed.
pymimir>=0.14
```

Probing with `hasattr` at import time cannot fail a dependency resolution, cannot be read off a
deployed environment, and has to be written once per feature. A version floor does all three. That
only works if the version actually moves when the surface does -- hence the rule.

## Consequences worth stating

* **Two builds that report the same version expose the same Python API.** If they do not, the bump
  was missed; that is a bug, not a judgement call.
* **A `PATCH` bump is a promise, not just a smaller number.** It tells a consumer that no `hasattr`
  probe they have written can change its answer across the upgrade.
* Version bumps belong in the commit that adds the binding, not in a separate release commit --
  otherwise the intermediate commits are wheels that lie.

## History of the MINOR component

| Version | Python-visible change |
|---|---|
| `0.14.0` | Landmark-restricted novelty: `IWOptions.landmark_novelty_graph`, `IWOptions.landmark_novelty_table_options`, `LandmarkNoveltyTableOptions`, `LandmarkNoveltyPruningStrategy`, and the `IPruningStrategy` precheck-capability queries. |
