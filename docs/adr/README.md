# Architecture decision records

This directory records significant, costly-to-reverse architectural decisions
for the LE Audio Receiver project. This README is the self-contained
convention; no external guide is a prerequisite for contributing a record.

## Naming

- One ADR is one file, named `NNNN-short-decision-title.md` with a
  four-digit number.
- The next record takes the next unused number. Never renumber an
  established record. Check for numbering collisions when parallel branches
  come together.
- Never delete an ADR without explicit owner approval; decisions and their
  history are retained.

## Statuses

- `Proposed`: drafted, awaiting review. Agents may draft Proposed records.
- `Accepted`: explicitly approved by the human product owner. Implementation
  alone is never approval; a pending PR is not approval.
- `Rejected`: explicitly declined. The record is retained with its context.
- `Superseded`: replaced by a later ADR. The old record is retained, links to
  its replacement, and keeps its original rationale.
- Only explicit owner approval can move a record to `Accepted`. When
  direction changes, write a new ADR and mark the old one `Superseded` with
  links in both directions.

## Format

Each record uses the small template: a header with `Date`, `Status` and
`Approval`, then `Context`, `Options considered`, `Decision and rationale`,
`Consequences`, and `References`. Records are short, self-contained, and in
plain English. They record verified facts from code and evidence at drafting
time; they do not invent historical debates, alternatives, dates, or
approvals, and they do not depend on temporary handoffs or planned-removal
files for their durable rationale.

## Index

| ADR | Title | Status |
|---|---|---|
| [0001 Private BlueZ guest isolation](0001-private-bluez-guest-isolation.md) | PB-053 lane runs inside a private QEMU guest with host-owned process boundaries. | Proposed |
| [0002 Per-entry audio LTV interposition](0002-per-entry-audio-ltv-interposition.md) | Repository-owned GNU ld `--wrap` guard validates LTV entry length before the SDK parser. | Proposed |
| [0003 Additive frozen coverage contract](0003-additive-frozen-coverage-contract.md) | New sources are covered under a separate sidecar without rebaselining the frozen file. | Proposed |