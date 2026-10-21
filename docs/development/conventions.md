# Conventions

The rules below apply to every change, human or agent. Each exists because breaking it breaks
something a user would notice, usually quietly. They are written down in `AGENTS.md`; this page
says, for each one, why it exists and what (if anything) checks it.

## Size: 350 lines per file, 600 per suite

A hand-written source file stays under **350 lines**. A Unity test suite may reach **600**,
because a suite is one binary per directory and is therefore cut along directories, not files
(see [Testing](/development/testing)). Generated files (`registry_generated.h`, `ha_generated.h`,
`entities.ts`, `entityNames.generated.ts`) do not count.

Cut along a seam that already exists: one responsibility per file, the public header unchanged. A
component is a directory, so several files sharing an internal header beside one public header is
the shape to reach for. **Cut first, then add:** a file that is about to be entered and is over the
ceiling is cut before the new work goes in, not after. **Recount with `wc -l` before acting** —
line counts drift, and a remembered number sends you to split a file that is already split.

**What enforces it:** nothing. It is a convention; review is the only check.

### Two standing exemptions

- **The three contract headers** — `ot_config.h`, `ot_wire.h` and `ot_provision.h` — are over the
  line and deliberately not cut. What is in them is the contract and its explanations, and
  splitting a public header changes exactly what the ceiling orders not to change.
- **`web/src/pages/control/pixelBoiler.ts`**, by the owner's decision. It is one indivisible
  responsibility: the single canvas renderer for the whole heating-system scene, where every
  `drawX` method shares the same coordinate constants, palette and private `px()` helper. Cutting
  it would fragment that one scene contract and duplicate its shared state for no gain. The pure
  maths is already split out into host-tested modules (`pixelFire.ts`, `pixelFlow.ts`,
  `pixelPipe.ts`) and the pipe fittings are drawn by `pixelPlumbing.ts`; the scene's own draw
  code stays whole. Do not split it.

## No Arduino

No Arduino core, no Arduino library, not even transitively through a third-party component.
`platformio.ini` uses `framework = espidf` in both board environments and must never acquire
`framework = arduino`. Check with an anchor at the start of the line:

```sh
grep -n '^framework' platformio.ini
```

The word `pioarduino` in the platform URL is the vendor's name — mandatory and irrelevant. No
test or hook runs that search; today it finds only `framework = espidf`, twice.

## No GPIO number outside `components/board/`

Pins are named in exactly one place: the board descriptor in `components/board/`, read through
`board_get()`. Two boards are enough for this to stop being a formality: on the C3, lines 18 and
19 are taken by USB; on the C6 they are free, and USB moved to 12 and 13. Copying numbers from one
descriptor into another kills the console. A `grep` gives a list of candidates, and each one is
checked by eye: the argument must come from `board_get()`. The OpenTherm input inversion is a
board field too (`ot_in_inverted` in `board.h`). See [Board](/device/board).

**What enforces it:** review, plus `tools/tests/test_source_guards_led.py`, which pins that the LED
task starts only on the board's `rgb.gpio`.

## Language: English, with one exception

Everything is English: comments, headers, `docs/`, commit messages, every `ESP_LOG*` string and
every string in `web/`. Entity display names in `tools/opentherm_ids.py` are English too — the
canonical name that reaches Home Assistant through discovery. The German, Dutch and Ukrainian names
of the entities live in `tools/entity_names_i18n.py`, keyed by the same keys, and are generated
into the SPA as a web-only overlay; they never touch discovery, so Home Assistant stays English
(see [Entity registry](/development/entity-registry)). The SPA's own strings are translated in
`web/src/i18n/catalogs/`.

**Russian stays in exactly one place:** the default device name in
`components/ot_config/ot_config_defaults.c` (`Термостат OpenTherm`, followed by the same suffix
the setup access point carries) — the owner's own Home Assistant device name.

### Why log strings must be ASCII

`ot_log_render()` escapes every byte outside printable ASCII as `\u00xx`, so it can hand back
valid JSON for any text. A non-ASCII log line therefore comes back through `GET /api/log`
unreadable and ungreppable.

## Comments say why

Every non-trivial function carries a comment saying **why** it is the way it is, with a
self-contained rationale inline — not a pointer to an external document that may be deleted. Where
the obvious improvement would break something, the comment says `DO NOT` and what breaks:

```c
// DO NOT move it to .bss "to be safe". Zeroed at every boot, it restarts the watchdog on every
// ...
```

Comments that retell the code are noise and are deleted.

## Decisions are recorded where they live

A decision a reader half a year from now could reasonably dispute is explained where it lives: in
the comment beside the code, or in the component's `README.md` — self-contained, so that no
deleted design document is needed to understand why the obvious alternative was not taken. On this
site such a decision gets a "Why not …" subsection on the page that owns the feature.

## Public headers are contracts

Every public header under `components/<name>/include/` describes:

- **ownership** — who allocates, who frees, how long a returned pointer lives;
- **task context** — which task may call it, whether it blocks, whether it is ISR-safe;
- **failure** — what happens on failure, and which `esp_err_t` values the caller is obliged to
  handle.

A caller should be able to use a component from its header alone.

## Every component has a README

Every `components/<name>/` directory carries a `README.md` with the same sections: Purpose ·
Responsibility · Public API · Implementation · Tests · Notes. It is the per-component document;
[Components](/architecture/components) ties them together. They are docs, not the code — re-read
the source before trusting a detail.

## Documentation is as-built

`docs/` describes how the firmware works **now**, not how it was planned: no phase, wave or
decision-ID history, no dates on facts (cite the unit and firmware instead). When a feature lands,
its plan and step list are deleted and the durable content is folded into the page that owns it
and the component README. Where a document and the code disagree, the code wins and the document
is fixed.

## The rules that carry load

These are architectural, and each has its own page; they are listed here because every change is
checked against them.

| Rule | Why | Where |
| --- | --- | --- |
| `ot_bus` never stops the conversation with the boiler | silence over 5 s reads to the boiler as a shorted thermostat — a demand for heat. Falling silent is the hottest state, not a safe one | [Bus](/protocol/bus), [Failure model](/architecture/failure-model) |
| Nothing reboots because a peer is absent | a missing broker, network or Home Assistant is a state to report and retry; the firmware has no `esp_restart()` call at all, and `test_source_guards_mqtt.py` pins that the MQTT glue never gains one | [Failure model](/architecture/failure-model) |
| The CH bit never rises before the held ID 1 has gone out | a boiler asked for heat with no setpoint heats to a value nobody chose | [Executor](/architecture/executor) |
| One list of entities, ever | REST, MQTT, discovery and the frontend types are generated from `tools/opentherm_ids.py`; fix the generator, not the output | [Entity registry](/development/entity-registry) |
| The web interface has no privileged handle | if a screen needs a path of its own, the API is wrong | [Web UI](/interfaces/web-ui) |
| Secrets are write-only | a stored password is returned by no API projection; a read hands back a sentinel. This is what makes one universal binary safe to configure in a browser | [Security](/architecture/security) |
| The input inversion is derived from someone else's code, not the specification | if not a single frame decodes, flip `ot_in_inverted` first, before looking for a bug in `ot_decode` | [Physical layer](/protocol/physical-layer) |
| The platform pin carries load | a floating URL once moved the toolchain out from under a verified design | [Building](/development/building) |

## How work is done

### Split before starting

A plan turns into independent tasks, each naming its own files and its own test suite.
Non-overlapping tasks run in parallel; overlapping ones run one after another, and that is stated
in the plan, not discovered in a conflict. Work that requires reading many files is delegated, and
the delegate returns a summary.

Three things are shared between parallel tasks, and they are lifted differently: the build
directory (`PLATFORMIO_BUILD_DIR=/tmp/pio-<task>`), `web/dist` (a git worktree per firmware task
running beside a web task), and a shared header — which nothing lifts. See
[Building](/development/building).

### Several sessions on one repository

Several sessions work on this repository at once. Check `git status` before starting and before
committing: a dirty tree you did not make is another session's work in progress — do not touch
it, and commit only your own paths. Nothing is committed or pushed without the owner's
permission, and flashing a device is the owner's business.

### Verification is output

"The tests pass" without the pasted output is a step that was not done. A build is judged by its
exit code, never by the tail of its output.

### Counter-review in the middle

A body of green tests can still hide a channel that does not work end to end, because each half
was tested against its own idea of the contract. Any work with more than one moving surface gets
a counter-review as soon as the surfaces first meet — and a design gets one before anything is
built on top of it — not at the end.
