---
type: Documentation Convention
title: Shadow Engine OKF v0.2 Maintenance
description: Mandatory rules for maintaining the repository's public Google OKF v0.2 bundle.
tags: [shadow-engine, okf, documentation, provenance]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T10:30:00+02:00
sources:
  - id: okf-spec
    resource: https://github.com/GoogleCloudPlatform/open-knowledge-format/blob/main/SPEC.md
    title: Open Knowledge Format v0.2 specification
    author: team:google-cloud-platform
---

# Format contract

This directory is an OKF bundle; `.okf` is not a file extension. Every
non-reserved Markdown file represents one durable concept and requires
parseable YAML frontmatter with a non-empty `type`.[^okf-spec]

Only the root `index.md` has frontmatter, containing only
`okf_version: "0.2"`. Subdirectory indices and every `log.md` have none. Logs
use newest-first `YYYY-MM-DD` sections.

# Authoring contract

Keep one subject per concept and use indices for progressive disclosure. Use
`generated.at`, never legacy `timestamp`; use `sources`, never a `# Citations`
body section. Source individual claims with Markdown footnotes keyed to stable
`sources[].id` values.

Use `draft`, `stable`, or `deprecated` honestly. Do not add a human verifier
unless that person actually reviewed the concept against its sources. Schema,
link, build, and runtime checks do not automatically establish content truth.

# Public boundary

This bundle contains sanitized public knowledge only. Private runtime logs,
Ubisoft artifacts, dumps, credentials, unpublished paths, and redistribution-
restricted files remain outside the repository.

# Change workflow

OKF is a lookup system, not only an output format. Before investigation or
implementation, search the active bundle and relevant archive provenance, then
verify important claims against current sources. If durable knowledge exists
only in a log, chat, handoff, archive narrative, or code comment, promote it to
a focused active concept and make it discoverable from an index.

Any code change that alters durable architecture, compatibility, validation,
or operations updates the affected concept and bundle log in the same commit.
Adding, moving, or removing concepts also updates the nearest `index.md`.
Validate YAML, reserved-file structure, lifecycle fields, provenance, and local
links before committing.

[^okf-spec]: Open Knowledge Format v0.2 specification
