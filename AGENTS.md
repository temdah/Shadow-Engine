# Shadow Engine Contributor Entry Point

Read:

1. The parent workspace `AGENTS.md`, when this repository is inside the Watch Dogs Modding workspace
2. `docs\okf\index.md`
3. `docs\okf\current-status.md`
4. `docs\okf\operations\code-quality.md`
5. `docs\okf\operations\okf-maintenance.md`
6. Only the additional concepts and source relevant to the task

Before reverse engineering or changing engine behavior, complete
`docs\okf\operations\knowledge-preflight.md`. Search active and archived OKF,
current source, affected values/RVAs, runtime markers, and adjacent subsystems.
Record what was searched and promote durable archive-only findings into a
focused active concept. OKF lookup is mandatory work, not only documentation
performed after code changes.

## Architecture rules

- `src\shadow_engine_patch.c` remains composition-only; production behavior belongs in its owning module.
- Preserve documented module dependency order. Do not use hidden globals, compatibility aliases, or include-order coupling to bypass an interface.
- Put mutable state in its owning `ShadowEngineContext` subsystem with explicit initialization, validity, and cleanup.
- Regional differences belong only in immutable `RuntimeProfile` data. Never branch common engine policy on profile number or storefront.
- Bootstrap owns ordering only. Profile recovery, engine policy, diagnostics, lifecycle, and resources stay in cohesive modules.
- Perform complete read-only preflight before mutation. Reversible writes use the patch transaction journal and rollback path.
- Preserve full native light passthrough and unknown-build fail-closed behavior.
- Keep inactive probes, tracing, repair code, and speculative abstraction out of the production unity build.

## Change rules

- Start from the accepted baseline and change one runtime hypothesis at a time.
- Do not call a value capacity until allocation, writers, readers, indexing, and lifetime are mapped.
- Do not derive one uniform layout formula from a single old/new pair; identify every intervening variable-length region and validate each independently.
- Use named constants for engine-derived values and comments for invariants and rationale.
- A feature change must map to one owning module, one validation plan, and the affected OKF concepts.
- Build only through `build.ps1` with TinyCC 0.9.27 win64.

## Validation rules

Run relevant refactor equivalence checks, the regional runtime corpus gate for
capacity or relocation changes, transaction fault-injection tests for mutation
changes, warning-clean reproducible builds, and public/private OKF validation.
Use the risk tiers in `docs\okf\validation\regional-runtime-matrix.md`: shared
policy changes with an unchanged compatibility surface require one Global
high-load run after the corpus gate; regional/profile/hook/resolver changes
require affected-profile or complete five-profile testing. Tim installs and
launches manually.

## Evidence and Git

Keep private logs and proprietary artifacts outside the repository. Every test
handoff states retained, changed, removed, and regressed behavior. Use
Conventional Commits. Ask Tim for explicit permission immediately before every
push; earlier permission never carries forward.
