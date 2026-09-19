# Product

This document defines the product intent and lifecycle for le-audio-receiver,
managed with [Backlog.md](https://backlog.md). It records the project-specific
contract: configuration, field vocabulary, lifecycle semantics, and ownership
boundaries. It is not an item index; the `backlog board` and `backlog task list`
commands are views.

- [Configuration](#configuration)
- [Field vocabulary](#field-vocabulary)
- [Lifecycle](#lifecycle)
- [Definitions](#definitions)
- [Section ownership](#section-ownership)
- [Tooling](#tooling)

## Configuration

`backlog.config.yml` at the repository root sets
`docs/product/backlog` as `backlog_directory`:

```yaml
statuses: ["Backlog", "Ready", "In Progress", "Blocked", "Review", "Done"]
priorities: ["P0", "P1", "P2", "P3"]
types: ["feature", "bug", "research", "refactor", "tech-debt", "docs"]
task_prefix: "PB"
zero_padded_ids: 3
backlog_directory: "docs/product/backlog"
remote_operations: false
```

The storage layout under `docs/product/backlog/` is:

```text
tasks/       # active items, any non-terminal status
completed/   # terminal Done items, retained product history
archive/     # dropped items, retained outside active views
drafts/      # not used
```

## Field vocabulary

### IDs

`PB-001`, `PB-002`, ... are allocated by `backlog task create` (monotonic
max+1 across visible tasks; `backlog doctor` detects duplicates). IDs are
never reused or renumbered; references use the ID, not the filename.

### Status

```text
Backlog -> Ready -> In Progress -> Review -> Done
                In Progress <-> Blocked
any active status -> archived (dropped)
```

`Done` is the terminal status (last entry of `statuses`).
`backlog task complete <id>` moves the file for a Done task to `completed/`.

**Dropped** is not a status. `backlog task archive <id>` moves an active item
to `archive/tasks/`; the archive is dropped history. Record the drop rationale
in the item's Final Summary before archiving.

### Priority

```text
P0  urgent or actively blocking
P1  intended near-term work
P2  useful planned work; normal default
P3  speculative, conditional, or deliberately deferred
```

### Type

```text
feature     new user-visible or product-visible capability
bug         existing behavior is incorrect
research    investigation needed before an implementation decision
refactor    internal structural change, intentionally unchanged behavior
tech-debt   maintenance, build, infrastructure, or accumulated deficiency
docs        documentation is primary deliverable
```

### Size and area labels

Backlog.md has no native fields for size or area, so labels carry them:

```text
size:S | size:M | size:L        S localized, M several components, L cross-cutting
area:<subsystem>                audio bluetooth pairing flpr clock-recovery
                                hil hardware interoperability release flashing
                                build ci testing documentation security
```

`size:*` may be absent for an early Backlog item; set it before the item is
Ready. An L item normally stays Backlog until refined or split.

### Dependencies

Native frontmatter `dependencies:` lists PB IDs only for true sequencing.
Related but independent work belongs in the Description text.

## Lifecycle

- **Implementation agents may only begin Ready work.**
- **An agent may take an item to Done** through the PR gate:
  1. acceptance criteria are checked from real evidence;
  2. repository gates are green;
  3. Final Summary is filled with outcome, decisions, and validation;
  4. the item's Done transition, first changing its status and then moving it
     to `completed/`, is committed with the work in one PR whose title starts
     with the item ID, for example `PB-006: ...`.
- **Human product-owner merge is official acceptance.** A Done item merged
  into `main` is officially done. If PR is rejected or changes are requested,
  move the item back to `tasks/` with status In Progress or Review, fix, and
  re-PR. An agent never merges its own PR.
- **Review** remains available for implementation-complete items awaiting
  direction or bundling, but it is not a required stop. The happy path is
  `In Progress -> Done` inside the PR.
- Done does not imply a published release. The release records own the release
  history.
- **Dropped** requires a human decision. Record the rationale in Final Summary,
  then use `backlog task archive`. Archived items are retained permanently.
- Status transitions go through the `backlog` CLI or MCP, not hand edits, so
  IDs, filenames, and metadata stay consistent. Editors may change the
  Description, Implementation Plan, and Implementation Notes.

`docs/product/backlog/` alone owns the current product-item status, priority,
and dependencies. Existing `docs/development/` plans, results, and handoffs
remain technical context and immutable evidence where applicable. `STATUS.md`
remains the current implementation and evidence snapshot, not a second task
list. Active standalone plans and design documents should cite the relevant PB
IDs; completed or historical plans do not need conversion into Done tasks.

## Definitions

**Definition of Ready**: the problem is understandable; the desired outcome is
explicit; the scope is bounded; non-goals are stated; acceptance criteria are
observable; dependencies are known; size is set; no unresolved product question
blocks implementation; the item does not require the implementation agent to
invent product behavior.

**Definition of Review** (before entering Review): acceptance criteria are
checked from evidence; repository gates have run; Final Summary records the
implementation, validation commands, and outcomes; required user docs,
reference docs, and ADR updates are included; known limitations stay within the
stated non-goals.

**Definition of Done** (agent-reachable, PR-gated): acceptance criteria are
checked from real evidence; repository gates are green; Final Summary is filled
with validation commands and outcomes; the Done transition is committed with
the work in one PR titled with the item ID. It is officially done when the
human product owner merges the PR.

**Blocked** requires a concrete external or technical obstacle in Implementation
Notes. An underspecified item is not Blocked; it remains Backlog.

**Work in progress**: the default soft limit is one In Progress item for this
single-owner project unless parallel work is explicitly intended.

## Section ownership

**Product-owned** (implementation agents may not silently change; refinement
may change only when explicitly requested): title, priority, status, type,
the Problem, Desired outcome, Scope, and Non-goals sections of the Description,
and acceptance-criteria text.

**Execution-owned**: Implementation Plan, Implementation Notes, Final Summary,
acceptance-criteria checkbox state, and validation evidence.

**Shared metadata** (correctable when repository inspection proves the initial
classification wrong; corrections remain visible in task history): size/area
labels and dependencies.

## Tooling

The repository dev shell provides `backlog`, pinned through the flake input
`backlog-md` (`github:MrLesk/Backlog.md`). It is intentionally not a global
installation: the tool version that understands this backlog lives with the
repository.

```bash
nix develop -c backlog task list --plain
nix develop -c backlog task PB-001 --plain
nix develop -c backlog doctor
nix develop -c backlog board
nix develop -c backlog browser
```

The Markdown files are the store; the CLI, board, and web UI are views.
Everything remains greppable:

```bash
rg 'status: Review' docs/product/backlog/tasks
rg 'area:audio' docs/product/backlog/tasks
rg 'PB-017' docs/product
```
