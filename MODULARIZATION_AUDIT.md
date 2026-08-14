# v0.9.0 modularization audit

## Scope

This build changes source organization only. It intentionally retains v0.8.9's
maps, passes, queue, B4/A8 profile, owner reserve, candidate passthrough,
render-record repair, sidecar lifetime, diagnostic frequency and known FPS
regression.

## Machine-code equivalence proof

Before changing the version string or adding module comments, the preserved
2,903 lines from `v0.8.9/src/shadow_engine_patch.c` were split mechanically into
eight ordered include modules. Compiling the modular aggregator with the exact
v0.8.9 TinyCC command produced SHA-256:

`D622440495530E905C96B0B8D5CC6C0D9A4E7EDE3F0979BF75341F8A260415C0`

That is byte-identical to the tested v0.8.9 ASI. Therefore the file split itself
changed no generated machine code or data.

The final v0.9.0 binary differs because its embedded version string is
`0.9.0-stage-p-modular-unity-refactor`. It has the same byte length as the old
label, avoiding an incidental PE data-layout shift. Module comments do not generate
runtime code.

Final binary comparison found equal 76,288-byte file lengths and only 42 changed
bytes: the two embedded version-label occurrences plus the two-byte PE checksum
update caused by those label changes. No executable-code range changed.

## Retained

- 24 physical maps and added maps 16-23.
- Paired passes 17-24 and 25-entry queue layout.
- Whole-record 24-face admission boundary.
- B4=16, A8=4 and eight-owner physical reserve.
- Full native candidate passthrough.
- Null-resource compaction and lifecycle tracing.
- Null/non-null record repair and symmetric sidecar acquire/release.
- Current high-frequency event logging and F8/F9 diagnostics.

## Changed

- One 2,903-line implementation file became a small unity-build aggregator plus
  eight responsibility-based modules.
- Added explicit dependency and ownership documentation.
- Changed only the embedded build version after equivalence was proven.

## Removed and regressions

- Removed: no engine patch, policy, diagnostic, guard or hook.
- New runtime regression expected from modularization: none.
- Known retained regression: v0.8.9's major FPS loss remains intentionally
  present. This build must not be presented as its fix.
