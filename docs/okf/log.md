# Shadow Engine Knowledge Bundle Update Log

## 2026-08-24

* **CI artifact**: Added a main-push GitHub Actions workflow that reproducibly builds the ASI and uploads the actual installable mod ZIP plus checksum without creating tags or GitHub Releases.
* **Release**: Promoted the validated architecture refactor to v1.2.1; only the embedded version identity and resulting PE checksum differ from the five-profile-tested ASI.
* **Runtime validation**: The unchanged `ac74c2a` candidate passed all five supported runtime profiles, including a 20-minute Global stability run.
* **Capacity evidence**: Extreme traffic reproduced temporary world-shadow loss near the current 24-face physical budget; retained as the next feature investigation.
* **Migration**: Rebuilt the public knowledge bundle for OKF v0.2.
* **Architecture**: Documented immutable runtime profiles, explicit subsystem ownership, and transactional patch phases.
* **Validation**: Separated offline checks from the required five-profile runtime matrix.
* **Cleanup**: Removed historical documents from active navigation while preserving their history outside the current bundle.
* **Convention**: Added mandatory code-quality and OKF v0.2 maintenance contracts.
