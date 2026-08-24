# Shadow Engine Knowledge Bundle Update Log

## 2026-08-24

* **v1.2.3 correction**: Retained the 30-map target but corrected two internal mapping-array relocation classes to their historical `0x24C0` and `0x24C4` per-map strides; strengthened compile-time and offline validation around the three-region layout.
* **v1.2.2 rejected**: The first runtime test had all shadows missing and zero `SliceExecute` result stores despite populated queue records. The uniform `0x24C8` relocation rule displaced the two mapping arrays and prevented shadow-job dispatch.
* **v1.2.2 candidate**: Added a measured 30-map/31-entry physical layout with `B4=21`, `A8=4`, passes 17-30, external results 17-29, admission diagnostics, compile-time coupling checks, and a capacity-specific offline validator.
* **Pressure result**: F8/F9 proved 17 dynamic-or-special plus four cached bindings during loss, zero cached bindings after recovery, 21/24 versus 20/24 faces, and no renderer clamp or lifecycle failure.
* **Pressure diagnostics**: Distinguished upstream manager admission/cache pressure from renderer face-budget pressure and added aggregate F8/F9 measurements without changing engine policy.
* **CI artifact**: Added a main-push GitHub Actions workflow that reproducibly builds the ASI and uploads the actual installable mod ZIP plus checksum without creating tags or GitHub Releases.
* **Release**: Promoted the validated architecture refactor to v1.2.1; only the embedded version identity and resulting PE checksum differ from the five-profile-tested ASI.
* **Runtime validation**: The unchanged `ac74c2a` candidate passed all five supported runtime profiles, including a 20-minute Global stability run.
* **Capacity evidence**: Extreme traffic reproduced temporary world-shadow loss near the current 24-face physical budget; retained as the next feature investigation.
* **Migration**: Rebuilt the public knowledge bundle for OKF v0.2.
* **Architecture**: Documented immutable runtime profiles, explicit subsystem ownership, and transactional patch phases.
* **Validation**: Separated offline checks from the required five-profile runtime matrix.
* **Cleanup**: Removed historical documents from active navigation while preserving their history outside the current bundle.
* **Convention**: Added mandatory code-quality and OKF v0.2 maintenance contracts.
