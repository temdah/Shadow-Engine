# Shadow Engine test gates

Run `validate_refactor.py` for source-policy, profile, signature, topology, and
architecture invariants. Use `--policy-target-v124` for the v1.2.4 policy:
30 maps, coherent `B4=25`, and `A8=4`.

Run `test_patch_transaction.ps1` whenever mutation or allocation behavior
changes.

Run `validate_runtime_corpus.py` for every capacity, layout, relocation, or
regional-address change. It requires private corpus paths supplied as command
arguments; proprietary executables and mapped images must remain outside Git.
Use `--a4ee-runtime-image` when a mapped A4EE image is available. Until then,
pass its frozen successful log with `--a4ee-attestation-log`.

Run `test_runtime_corpus.py` to exercise the three-region formula and its two
uniform-stride regression guards without requiring proprietary fixtures.

These tests authorize proportionate manual testing; they cannot launch the
game or classify rendered output.
