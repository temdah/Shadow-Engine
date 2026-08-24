---
type: Evidence Policy
title: Evidence and Redistribution Policy
description: Defines claim strength and what may be included in public Shadow Engine documentation.
tags: [shadow-engine, evidence, privacy, redistribution]
status: draft
generated:
  by: codex/gpt-5
  at: 2026-08-24T00:00:00+02:00
sources:
  - id: policy
    resource: ../../EVIDENCE_POLICY.md
    title: Repository evidence policy
---

# Claims

Distinguish runtime-confirmed facts, static-analysis facts, user observations,
historical records, and hypotheses. Offline validation does not prove runtime
behavior. A successful function return does not by itself prove a visible
rendering result.

# Public boundary

Public documentation may contain sanitized conclusions and project-owned,
reproducible implementation details. Do not publish Ubisoft binaries, process
images, dumps, saves, private logs, credentials, or third-party files without
redistribution permission.

# Change discipline

Preserve the evidence produced by a build before changing that build's code.
State retained, changed, removed, and regressed behavior with every test
handoff.
