# Packaging

Use `../package-internal-tools.ps1` after the canonical build and checks.
ASI archives contain `bin/ShadowEnginePatch.asi` and `ASI_README.md` renamed
`README.md`. The separate NexusTools import archive contains its manifest and
three Lua files. Do not bundle game files, tools in the ASI ZIP, or private
captures. Preserve previous deliveries. See `../BUILDING.md` for profiles.
