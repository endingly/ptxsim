# Dispatch controls and selection evidence

Read this reference when selecting, retrying, or resuming a worker, or establishing
that a primary can perform Sol duties. Reuse an already checked tool schema and
selection record while the tool/session configuration is unchanged. A worker with
a complete packet need not repeat discovery; check again for missing evidence,
a changed session, or a reported mismatch. This file does not define model defaults:
use the [registry](../orchestration.md#model-preferences).

## Tool controls and context inheritance

Use the actual tool's reasoning-effort control. Prompt text, a role label, and
an output-token budget do not configure it. Do not silently substitute `high`
or `xhigh` for Luna's `max`, omit the effort to inherit a default, or invent a
schema field. If the required pair is not expressible or supported, treat that
pair as unavailable and follow the [fallback rules](../orchestration.md#availability-and-fallback); do not claim it was selected.
A host-controlled primary need not be reconfigured to keep coordinating, but
it may satisfy the Authority Sol gate directly only when assigned that role at
the required `gpt-6.1-sol` / `high` pair and eligible under the distinct-review
rule. Selecting that pair alone does not confer authority.

Check the actual tool's supported model and effort values before requesting an
override, including whether the required pair is supported together. Public
availability does not prove availability in this session. Do not attempt an
unavailable identifier or silently omit an explicit model or effort selection.
The host/user controls the primary model; editing these documents does not switch
it or change host configuration. A primary on an unlisted model may coordinate
permitted work, but cannot use itself to bypass the worker registry or Authority
Sol gate.

Prefer a self-contained task packet and the smallest supported context fork
that retains explicit model/effort selection. If a full-history fork forces
parent model or effort inheritance, use a supported limited/no-history fork
instead where possible. Otherwise report the inherited pair and the limitation.
Inheritance cannot bypass the registry; a noncompliant or unknown pair cannot
satisfy a required Authority Sol gate. Check retained settings before resuming a
worker.

Record requested model and effort, role, scope, any Implementation Sol or Astra
effort-exception reason before dispatch. Add runtime-reported effective model
and effort when exposed. Mark either missing
effective field as unverified; a role name, self-identification, or successful
spawn alone proves neither setting. An accepted explicit selection through a
supported tool control, with no known override, is sufficient selection evidence;
missing runtime telemetry alone need not block work. An unknown requested or
inherited pair, or a known mismatch, requires compliant redispatch/reassignment,
not a claim that the original task met policy. For an Authority Sol gate, require
assignment to the Authority Sol role plus either an explicit accepted
`gpt-6.1-sol` / `high` selection with no known override or a host-confirmed
session at that pair.
An Implementation Sol selection, at default `medium` or a justified `high`
exception, is evidence for implementation only. Model/effort selection alone
does not confer authority. Otherwise leave the gate pending. Do not claim
confirmed effective settings or independent review without evidence.

## Usage audits only

When the user requests a usage/cost audit, distinguish requests, input/cached-input/
output tokens, and actual cost over the same period. Do not invent telemetry or
enforce model-percentage quotas. Ordinary task packets need selection evidence,
not a new billing investigation.
