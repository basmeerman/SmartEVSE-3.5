# Upstream Sync State

Tracks integration status of upstream commits from `dingo35/SmartEVSE-3.5`.

**Last synced to:** `790f2a9` (2026-04-10, 2026-04-13 triage closed)
**Current upstream HEAD:** `9e1faa9` (2026-09-01)
**Open window:** 2026-09-08 triage below — 117 commits behind `origin/master`, 99 of them new since `790f2a9`.
**Open pending commits (2026-04-13 window):** 0 in master backlog. EtherLCD is parked on long-lived test branch `upstream/543af26-etherlcd-test` (awaiting hardware testing, no master PR until cleared). 2 deferred to Plan 07 (Web UI). 9 integrated, 1 already-fixed.

Prior sync window (2026-03-29, now closed): 2 integrated (PR #130), 1 rejected,
1 evaluated/deferred (190777f), 1 skipped.

---

## Sync: 2026-09-08 — Triage (99 new commits since `790f2a9`)

Fork development paused after 2026-04-20 and resumed on 2026-09-08. In that window
`origin/master` moved to `9e1faa9` (2026-09-01) and is now **117 commits ahead** of
`myfork/master`. 18 of those belong to the closed 2026-03-29 / 2026-04-13 windows —
they were integrated by adaptation rather than by merge, so their hashes still show
as absent from the fork. The remaining 99 are triaged below, grouped by theme.

Two structural facts dominate this window and should be settled before any bulk merge:

1. **Upstream deleted the v4 / CH32 target and the `ENABLE_OCPP` build switch.** The fork
   built and budgeted both (`pio run -e ch32`, CI enforced the CH32 flash/RAM budget).
   **Decisions 1 and 1b (2026-09-08): the fork follows upstream on both**, so this stops
   being a divergence and becomes a scheduled removal — the `ENABLE_OCPP` flag first, then
   v4/CH32. OCPP functionality is unaffected: it is always compiled in and gated at runtime
   by `OcppMode`. See group A and the decisions section at the end.
2. **Upstream and the fork implemented capacity-tariff peak tracking and CircuitMeter
   independently.** The fork shipped them as Plan 13 / Plan 14 (`capacity_peak.c`,
   pure-C, tested); upstream shipped `CapacityMode` + `interval.html` + `capacity.html`
   + a `/powerday` endpoint in `meter.cpp`. These are not mergeable as diffs — only the
   externally visible contracts (NVS keys, MQTT topic names, meter type IDs, `/settings`
   JSON keys) are worth aligning.

The practical consequence: **cherry-pick-and-adapt remains the only viable strategy;
a merge of `origin/master` is off the table for this window.** Decisions 1 and 2 narrow the
gap deliberately — dropping v4/CH32 and adopting `shadowPrefs` both move the fork's
`esp32.cpp`, `main.cpp` and `glcd.cpp` closer to upstream's shape, which is what makes
later picks cheaper. Plans 01-16 keep the fork diverged by design; that part is not
changing.

### A. Structural divergence — resolved: fork follows upstream (decision 1)

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `df910fb` | 2026-06-27 | remove all v4 / CH32 stuff | **Adopt (adapt)** | P2 | Fork drops the v4 / CH32 target to match upstream. Our removal is larger than upstream's diff: `ch32.cpp/h`, `wchisp.cpp/h`, the `v4` and `ch32` PlatformIO envs (`platformio.ini:80`, `:95`), ~254 `SMARTEVSE_VERSION` guards across `src/`, the CH32 flash/RAM budget gate in CI, plus the CLAUDE.md budget table and pre-push verification step 5. Do it as its own PR, before the `esp32.cpp`-heavy work in group E. |
| `f72ade5` | 2026-06-27 | remove ENABLE_OCPP ifdefs | **Integrated** | P2 | Fork PR (2026-09-08). The flag only ever controlled *compile-time inclusion* of the MicroOcpp library; whether OCPP runs is the separate NVS-persisted `OcppMode` setting (`main.cpp:295`, default 0), reachable from the web UI, MQTT `/OCPP` and the bridge. Dropping the flag therefore removes a build configuration, not a feature. The platform half of each guard is kept — every `#if ENABLE_OCPP && defined(SMARTEVSE_VERSION)` became `#if defined(SMARTEVSE_VERSION)` — so OCPP still stays out of the CH32 build. `esp32.h:165` RFID menu range becomes a constant 6. |
| `9510b30` | 2026-06-29 | main.cpp: remove unused v4 Read functions | **Adopt** | P2 | Falls out of `df910fb`; take it in the same PR. |
| `9651e62` | 2026-06-08 | fix v4 bug | **Skip** | — | Fixes an upstream `SMARTCircuitSE_VERSION` typo in code the fork never had (`grep SMARTCircuitSE` returns nothing), on a target the fork is now removing. |

### B. Correctness bugs that also exist in the fork — P1

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `36621b6` | 2026-08-28 | fix comma-operator misparse in `Update.begin()` calls | **Adopt** | **P1** | Confirmed present in the fork at `network_common.cpp:1684` and `:1719`: `if (!Update.begin(size), U_FLASH)` parses as a comma expression, so `U_FLASH` is the condition (always true → the `!` makes the error branch dead) and `Update.begin()` is called with the default partition. The OTA failure path is silently unreachable. Fork-only extra: check whether `firmware_manager.cpp:321` (`Update.begin(updateSize, partition)`) is correct — it appears to be. |
| `1c80751` | 2026-06-14 | Replace `localtime()` with thread-safe `localtime_r()` | **Adopt** | **P1** | Fork has 4 unguarded `localtime()` call sites: `esp32.cpp:1927`, `:3071`, `:3089`, `glcd.cpp:819`. Called from different FreeRTOS tasks; the returned `struct tm` is a shared static. |
| `e36a3cb` | 2026-05-02 | Only allow portal page and `/erasesettings` when portal is active | **Adopt (adapt)** | **P1** | The fork does not restrict URIs in portal mode (`WIFImode == 2`). In portal mode the device is an open AP, so every mutating endpoint is reachable unauthenticated. Overlaps Plan 16 (`AuthMode`/`require_auth`): the fork's version should route through the existing auth gate rather than adding a second parallel check, and must keep the fork's extra endpoints in mind. Treat as a security item with a regression test. |
| `8439936` | 2026-05-29 | esp32.cpp: fix JSON response on `/currents` and `/ev_meter` | **Evaluate** | P2 | Check against the fork's `http_handlers.cpp` — these endpoints were refactored during Plan 04/09, so the bug may not exist here. |
| `164af0b` | 2026-05-07 | main.cpp: Fix energy meter polling in ModbusRequestLoop | **Evaluate** | P2 | Modbus request-loop ordering; interacts with the fork's CircuitMeter slot in the same loop. |
| `e80af3f` | 2026-04-28 | network_common.cpp: fix WiFi needs Disable/Enable to work after reboot | **Evaluate** | P2 | One-line. Directly relevant to fork issue #168 (user reports unreliable WiFi). Verify against the fork's `handleWIFImode()`, which the EtherLCD branch has already restructured. |

### C. EtherLCD follow-ups — fold into `fix/issue-168-etherlcd`

The fork's EtherLCD integration (upstream `543af26` + `7353d50`) sits on
`fix/issue-168-etherlcd`, blocked on on-device bring-up. Upstream has since shipped
three follow-ups that belong on that same branch, not on master.

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `c725bb2` | 2026-06-02 | etherlcd.cpp: check `spi_device_acquire_bus()` lock status | **Adopt → branch #168** | **P2** | Bug fix in code the fork already carries. Unchecked bus acquisition on the CH390D SPI path. |
| `9e1faa9` | 2026-09-01 | glcd: show network menu options for both WiFi and Ethernet | **Adopt → branch #168** | **P2** | `getMenuItems()` gated AutoUpdate/Pairing/AppServer on `MENU_WIFI == 1`, hiding them on an Ethernet-only device. Upstream's fix uses `NetworkConnected()` — the same helper the fork's branch introduced. Near-verbatim applicable. |
| `5d1e849` | 2026-04-22 | Add EtherLCD documentation | **Adopt → branch #168** | P3 | `docs/EtherLCD.md`, 35 lines. Merge with the fork's `analysis-543af26-etherlcd.md`. |
| `ac0b04e` | 2026-08-31 | network_common.cpp: Add WiFi network scanning to portal | **Evaluate** | P3 | Not EtherLCD, but same user problem (issue #168: "wifi connection is really bad"). Scanning in the setup portal is a genuine usability win. Conflicts with the fork's portal changes from Plan 16 and item B/`e36a3cb`. |

### D. Capacity tariff and CircuitMeter — parallel implementations

Upstream's Flanders capacity work (Jan–Mar 2026, merged into upstream master late) and
its CircuitMeter are functional equivalents of the fork's Plan 13 and Plan 14. **None of
these are code-mergeable.** The action is a contract-compatibility review, not a port.

| Hashes | Title | Classification | Priority |
|--------|-------|----------------|----------|
| `07fc19d`, `4d5bc74`, `7369c7d`, `fddc3e2`, `bf9d33b`, `0a19379`, `1e598a2`, `0ae8556` | `meter.cpp`/`meter.h` peak-power measurement, 15-min period reset, `UpdateCapacity` rework, `AssumedVoltage` | **Parallel implementation** | P3 |
| `f33eeda`, `0d8d8bb`, `a66d4a4` | `CapacityMode` LCD menu + NVS, rename MANUAL→FIXED | **Parallel implementation** | P3 |
| `b35b3fe`, `45b6682`, `8f5f950`, `01606f4`, `e879633`, `21dd094`, `8bee133`, `e5f53db`, `0839243`, `bef23bf`, `3727b39` | `interval.html` / `capacity.html` capacity interval UI | **Defer → Plan 07** | P4 |
| `ac85215`, `03b00cb`, `45ba77d` | `/powerday` endpoint + 24h circular buffer + chunked JSON | **Evaluate** | P3 |
| `faf0bd9` | MQTT parameters to feed MainsMeter power/energy | **Evaluate** | P3 |
| `ed13611`, `d6dc047`, `ebf2cc6`, `e1668a2` | Introduce CircuitMeter; own 20s timeout; feed script; debug msg | **Parallel implementation** | P2 |
| `e58ed08`, `beaabb3` | `MaxSumMains = 0` when CapacityMode disabled; MaxSumMains NVS storage bug | **Evaluate** | **P2** |
| `ad6e9cd` | remove `Nr_Of_Phases_Charging` | **Rejected** | — |

Specific findings:

- **`ad6e9cd` is a hard conflict.** The fork's solar and phase-switching logic
  (Plan 01, `GOING_TO_SWITCH_1P` / `GOING_TO_SWITCH_3P`) is built on
  `Nr_Of_Phases_Charging`, with tests asserting on it. Removing it is not an option here.
- **`d6dc047` (CircuitMeter 20s timeout)** is worth adopting on merit: upstream found
  11s too short for a CircuitMeter on Modbus. The fork's Plan 14 CircuitMeter uses the
  shared meter timeout and would hit the same false-timeout behaviour.
- **`beaabb3` (MaxSumMains NVS storage bug)** — one-line; verify whether the fork's
  `PREFS_PUT_*_IF_CHANGED` path has the same defect.
- **Decision 3 (2026-09-08): keep the fork's names, document the difference.** No
  aliases and no renames. The two models are not the same feature — upstream's
  `CapacityMode` is interval-table driven (`interval.html`, `capacity.html`, `/powerday`),
  the fork's Plan 13 is 15-min rolling peak tracking with automatic `IsetBalanced`
  clamping — so a shared vocabulary would misrepresent both. Record in
  `upstream-differences.md`: fork publishes `/CurrentMaxSumMains`, `/CapacityLimit`,
  `/CapacityWindowAvg`, `/CapacityMonthlyPeak`, `/CapacityHeadroom` and has no
  `MaxSumMainsTime` topic, where upstream publishes `/MaxSumMains` and `/MaxSumMainsTime`.
  A Home Assistant config written for one firmware does not carry over to the other. This
  closes items `4198813` and `b2e8ad4` as **Rejected** rather than *Evaluate*.

### E. NVS rework — architectural collision

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `bd2475a` | 2026-06-17 | get rid of `settingsCache` | **Evaluate (blocked)** | P3 | −203/+77 in `esp32.cpp`. |
| `0e1644f` | 2026-06-17 | introduce `shadowPrefs` | **Evaluate (blocked)** | P3 | Replacement mechanism. |
| `8659f3e`, `e4d3505`, `7e194ee` | 2026-06-17/22 | fix begin/end; atomic per-variable save; write untouched keys after 1 minute | **Evaluate (blocked)** | P3 | |
| `b87da05` | 2026-06-17 | get rid of `request_write_settings` | **Evaluate (blocked)** | P3 | Touches `esp32.h`, `main.cpp`, `meter.cpp`, `modbus.cpp`, `network_common.cpp`. |
| `459e182` | 2026-06-17 | reduce OCPP MaxCurrent overhead | **Adopt** | P3 | One-line, independent of the rework. |
| `e72abb6` | 2026-06-17 | fix RequiredEVCCID | **Adopt** | **P2** | 3 lines. `RequiredEVCCID` is the buffer behind security finding H-5; re-verify the fork's NUL-termination still holds after any change here. |

**Decision 2 (2026-09-08): adopt `shadowPrefs`.** The fork used
`request_write_settings()` + `PREFS_PUT_*_IF_CHANGED` against `settingsCache`
(74 references in `esp32.cpp`, 23 `request_write_settings()` call sites); upstream deleted
both. The rework buys atomic per-key saves and less flash wear, and it is a prerequisite
for cleanly taking any later upstream `esp32.cpp` change — every one of them now sits on
top of it, `b97d97c` (group H) included. Classification for the whole series therefore
moves from *Evaluate (blocked)* to **Adopt (adapt)**, scheduled after the P1 bug bundle
and after the group A removal (which shrinks `esp32.cpp` first).

Risk to manage: the NVS layer holds every user setting and the native harness cannot reach
Arduino `Preferences`, so nothing here is covered by `make test`. The port needs on-device
verification — settings survive reboot, LCD menu exit writes immediately, and no key is
lost when several are changed inside the 60s window.

### F. Networked meters / HomeWizard

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `c534b4c` | 2026-05-22 | Generalized support for networked meters and expanded HomeWizard support (#372) | **Evaluate** | P2 | 669 insertions across `network_common.cpp`, `glcd.cpp`, `main.h`, `meter.*`. Overlaps the fork's Plan 09 HomeWizard P1 work and Plan 05 meter extraction. Large conflict surface; port the concept, not the diff. |
| `e6aff5d` | 2026-06-13 | Improvements for network meter implementation (#381) | **Evaluate** | P2 | Follow-up to `c534b4c`; only meaningful together with it. |
| `2f5cac1` | 2026-05-29 | make homewizard task persistent | **Evaluate** | P2 | Task lifecycle fix; check the fork's HomeWizard task for the same churn. |
| `c1db16f` | 2026-05-25 | only run `discoverNetworkMeters()` when a network meter is configured | **Adopt (adapt)** | P2 | The fork's mDNS discovery has known intermittent issues; not running it when nothing needs it is a straight win. |
| `99a0612` | 2026-05-22 | don't clog the log when no networkmeters are present | **Adopt (adapt)** | P3 | Pairs with `c1db16f`. |
| `2be0d71` | 2026-08-27 | Fix undefined `mainsHost` crashing WebUI for HomeWizard mains meters (#393) | **Evaluate** | **P2** | User-visible WebUI crash. 2 lines in `index.html`; the fork's `index.html` is heavily diverged, so re-derive rather than cherry-pick. Check whether the fork's Plan 07 UI has the same undefined reference. |
| `f92dce8` | 2026-04-15 | Add support for Chint DDSU666 (#128) | **Adopt with renumber** | P3 | **Meter-type ID collision — see below.** |

**Meter type ID collision (`f92dce8`).** Upstream renamed `EM_CHINT` → `EM_CHINT_3P` (15)
and claimed **slot 17** for `EM_CHINT_1P`. The fork's Plan 05 already took slots 17 and 18
for `EM_ORNO3P` / `EM_ORNO1P`, with `EM_CUSTOM` at 19 (`meter.h:47-52`). Meter type is a
persisted NVS value and is exposed over MQTT and `/settings`, so the two firmwares now
disagree about what "17" means. Consequences to record: a fork user's Orno configuration
would silently become a Chint configuration if they ever flash upstream, and vice versa.
**Decision 4 (2026-09-08): add Chint DDSU666 at fork slot 20, keep Orno at 17/18.**
Adopting upstream's numbering would silently convert every existing fork user's Orno
setting into a Chint one — wrong register map, wrong readings — so the fork renumbers
nothing. Mechanically this is cheap: the LCD range is derived from the table size
(`esp32.h:179`, `EMConfigSize / sizeof(EMConfig[0]) - 1`) and nothing treats `EM_CUSTOM`
as the last entry; only direct `EMConfig[EM_CUSTOM]` lookups exist. Work item: add
`EM_CHINT_1P` at 20 with the DDSU666 register profile, add the profile to the Plan 12
Modbus compatibility suite, and document in `upstream-differences.md` that fork 17/18 are
Orno 3P/1P while upstream 17 is Chint 1P — cross-flashing between the two rewrites the
meter type.

### G. MQTT / integration surface

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `2ae8b2e` | 2026-04-28 | Add MQTT `StateID` topic for interfacing with evcc.io (#357) | **Adopt (adapt)** | **P2** | The fork publishes `/State` (`getStateNameWeb`) but has **no `StateID` topic** (`grep StateID` returns nothing). This is the topic evcc reads. The fork's Plan 04 EVCC work went through the HTTP API and IEC 61851 mapping instead, so this is a genuine gap for MQTT-only evcc users. Adopt the topic name verbatim — interop names must match upstream. |
| `1ea2301` | 2026-05-05 | Update esp32.cpp (#375) | **Adopt (adapt)** | P2 | The Home Assistant discovery `announce()` for `StateID` that `2ae8b2e` forgot. Take both together, and register it through the fork's change-only publish slot mechanism (Plan 08) rather than an unconditional publish. |
| `4198813` | 2026-04-15 | Add MaxSumMains and MaxSumMainsTime to MQTT (#355) | **Rejected** | — | Per decision 3: the fork keeps `/CurrentMaxSumMains` and does not adopt upstream's topic names. |
| `b2e8ad4` | 2026-04-15 | Update MQTT announcement for MaxSumMainsTime to minute | **Rejected** | — | Unit correction on a topic the fork does not publish. |
| `7823707` | 2026-08-27 | Add MQTT announcements for circuit energy metrics (#400) | **Already fixed (verify names)** | P3 | The fork already publishes `/CircuitImportEnergy` and `/CircuitExportEnergy` with HA announcements (`esp32.cpp:1097-1098`, `:1325-1327`) from Plan 14. Diff the topic strings against upstream's and record any mismatch. |

### H. Web UI and LCD

The fork's `index.html` diverges from upstream by 500+ lines and uses `style.css` where
upstream uses `styling.css` — the same collision that deferred `2c015fb` and `92d42eb` in
the 2026-04-13 window. All UI commits land in the Plan 07 bucket unless they fix a bug.

| Hash | Date | Title | Classification | Priority |
|------|------|-------|----------------|----------|
| `6d00b13` | 2026-04-28 | LCD via websocket — bugfix connection error (#361) | **Evaluate** | P2 |
| `d8c7963` | 2026-05-13 | LCD via websocket — reconnect when tab is back in focus | **Evaluate** | P3 |
| `46f582a` | 2026-05-21 | index.html: refresh stale statuspage when tab is back in focus | **Evaluate** | P3 |
| `75301a8` | 2026-08-31 | get timezone from browser when serving index.html | **Evaluate** | P3 |
| `00ea727` | 2026-05-29 | glcd: use static RAM buffer for the web-LCD | **Evaluate** | P2 |
| `a14526c` | 2026-05-01 | Restyled car image for the LCD display (#360) | **Defer → Plan 07** | P4 |
| `a88fcf5` | 2026-08-31 | glcd: add exit-direction icon on LCD | **Defer → Plan 07** | P4 |
| `b97d97c` | 2026-08-28 | glcd.cpp: clear all error flags; write settings immediately on menu exit | **Adopt (adapt)** | **P2** |
| `aed95e5` | 2026-05-29 | update2.html: smaller upload chunk | **Evaluate** | P3 |

`b97d97c` carries a real bug fix the fork shares: `clearErrorFlags(!(NO_ERROR))` at
`glcd.cpp:1448` evaluates to `clearErrorFlags(1)` — it clears only bit 0, not "all errors"
as the comment claims. Upstream's `clearErrorFlags(0xFF)` is correct. Adopt that half; the
second half (`shadowPrefs.loop(true)`) depends on group E and does not apply yet.

### I. Heap, flash and network tuning

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `4eb7dea` | 2026-05-29 | esp32, network_common: major heap-fragmentation & flash cleanup | **Evaluate** | P2 | 662 changed lines in `esp32.cpp` alone. Real value (the fork sits at ~89% flash on the EtherLCD branch) but a near-total conflict with the fork's diverged `esp32.cpp`. Mine for specific techniques rather than porting. |
| `0de2aa0` | 2026-07-03 | limit lwIP for http; add MQTT debug error messages | **Evaluate** | P3 | Overlaps the fork's HTTP connection cap (PR #151). |
| `5175293` | 2026-06-27 | disable powersave for ESP32 WiFi | **Adopt (pair)** | P3 | Take together with `2d95cf0`. |
| `2d95cf0` | 2026-06-30 | get rid of `setSleep` | **Adopt (pair)** | P3 | Net effect of the pair: no WiFi power save. Plausible relief for the reconnect complaints behind issue #168. |
| `7600180` + `5891a77` | 2026-06-29 / 07-05 | stack/heap warnings in debug log, then reverted | **Skip (net no-op)** | — | Upstream reverted its own commit; nothing to take. |

### J. OCPP and misc

| Hash | Date | Title | Classification | Priority | Notes |
|------|------|-------|----------------|----------|-------|
| `a310e9e` + `be30619` | 2026-05-01 / 05-29 | Increase mbedtls context to 8Kb; then 8kb in / 4kb out | **Integrated** | **P1** | Fork PR #171 (fork issue #169 — Tap Electric `wss://`). Includes a CI hash pin on `libmbedtls_2.a` so a framework refresh cannot silently restore the 6 KB context. |
| `a7589d8` + `5905283` + `db5ccad` | 2026-05-22 → 06-01 | fix delayed charging with OCPP, then reverted | **Skip (net no-op)** | — | Verify the net tree state before discarding: upstream applied two variants and reverted one. |
| `7b5a30e` | 2026-05-30 | glcd: display start time when delayed charging with OCPP | **Evaluate** | P3 | Depends on the delayed-charging state above. |
| `649fc69` | 2026-04-20 | move OCPP variables to where they belong | **Skip** | P4 | Pure code motion; the fork's OCPP state already lives in `ocpp_logic.c`. |
| `7b6d9f4` | 2026-05-22 | send OCPP debug messages to telnet if DBG==1 | **Evaluate** | P4 | Fork has its own diagnostic telemetry channel (Plan 06); telnet may be redundant. |
| `aa55838` | 2026-08-22 | change auto update url to `SmartEVSE_firmware` | **Rejected** | — | The fork serves its own OTA distribution with multi-key signature validation (PRs #123, #125, #126). Adopting upstream's URL would point fork devices at upstream images. |

### K. Docs and cosmetic — P4

| Hash | Date | Title | Classification |
|------|------|-------|----------------|
| `c0c6b16` | 2026-02-25 | Improve integrations section (#334) | **Evaluate** — 1,031 lines of HA/ESPHome YAML under `integrations/`; predates the first sync window and was never triaged. Compare against the fork's own HA and integration docs before taking. |
| `72e5c31` | 2026-03-10 | docs: update with new Capacity Mode | **Defer** — describes upstream's CapacityMode, not the fork's Plan 13 model. |
| `f4a8ef6` | 2026-05-03 | docs: improve home battery API info | **Adopt (adapt)** |
| `817ffc6` | 2026-05-06 | docs: update feeding power/energy mainsmeter values via MQTT API | **Adopt (adapt)** |
| `d0d5cb2` | 2026-06-01 | esp32.cpp: fix mojibake in temperature | **Evaluate** — 1 line, check if the fork shows the same. |
| `f909095` | 2026-02-24 | feed_mains.sh: vary power for better testing | **Skip** |

### Recommended batching

1. **P1 correctness bundle (one PR, own tests):** `36621b6` OTA `Update.begin()` misparse,
   `1c80751` `localtime_r`, `b97d97c` (the `clearErrorFlags(0xFF)` half). All three are
   confirmed present in the fork and independent of any upstream refactor.
2. **P1 security (separate PR):** `e36a3cb` portal-mode URI restriction, routed through
   the Plan 16 auth gate, with a regression test.
3. **EtherLCD branch top-up (no master PR):** `c725bb2`, `9e1faa9`, `5d1e849` onto
   `fix/issue-168-etherlcd` — still blocked on hardware bring-up.
4. **Interop bundle:** `2ae8b2e` + `1ea2301` MQTT `StateID` for evcc, published through
   the Plan 08 change-only slot mechanism; verify `7823707` topic names match.
5. **Meter compatibility:** `f92dce8` Chint DDSU666 at fork slot 20 + Plan 12 register
   profile + `upstream-differences.md` entry for the 17/18 collision.
6. **Network meter evaluation (analysis doc first, no code):** `c534b4c` + `e6aff5d` +
   `2f5cac1` + `c1db16f` + `99a0612` + `2be0d71`.
7. **Decision required before anything else touches `esp32.cpp`:** group E
   (`shadowPrefs`). Adopting or permanently rejecting it determines how expensive every
   later upstream `esp32.cpp` pick will be.
8. **Contract-compatibility review (analysis doc, no code):** group D naming — MQTT
   topics, NVS keys and `/settings` fields for capacity tariff and CircuitMeter.

### Decisions (2026-09-08)

All four questions raised by this triage were answered by the maintainer on 2026-09-08.

| # | Question | Decision | Consequence |
|---|----------|----------|-------------|
| 1 | v4 / CH32 target | **Drop it, follow upstream** | Own PR: delete `ch32.cpp/h`, `wchisp.cpp/h`, the `v4` and `ch32` envs, ~254 `SMARTEVSE_VERSION` guards, the CH32 CI budget gate, the CLAUDE.md budget row and pre-push verification step 5. |
| 1b | `ENABLE_OCPP` build flag | **Drop it, keep the functionality** | `f72ade5` adopted: OCPP is always compiled in, and enabling it stays the runtime `OcppMode` setting. Done in its own PR ahead of the v4/CH32 removal, since the guards are `#if ENABLE_OCPP && defined(SMARTEVSE_VERSION)` and only the first half comes out. |
| 2 | Upstream `shadowPrefs` NVS rework | **Adopt** | Port `bd2475a` → `7e194ee` adapted, after the P1 bundle and after the group A removal. Needs on-device verification; the native harness cannot reach Arduino `Preferences`. Supersedes the fork's `settingsCache` / `request_write_settings` pattern and the project-memory note describing it. |
| 3 | Capacity tariff naming | **Keep fork names, document the difference** | No aliases, no renames. `4198813` and `b2e8ad4` become *Rejected*. `upstream-differences.md` records the topic-name divergence and that HA configs are not portable between the two firmwares. |
| 4 | Meter type IDs / Chint DDSU666 | **Add at slot 20, keep Orno at 17/18** | New `EM_CHINT_1P` = 20 with the DDSU666 register profile, a Plan 12 compatibility profile, and an `upstream-differences.md` entry for the 17/18 vs upstream-17 collision. |

Decisions 1 and 2 are deliberate convergence: both shrink and reshape `esp32.cpp`,
`main.cpp` and `glcd.cpp` toward upstream, which lowers the cost of every later pick.
Decisions 3 and 4 are deliberate divergence, both to protect existing fork users'
configurations. Ordering matters — group A removal first, then `shadowPrefs`, because the
removal deletes a large amount of the `esp32.cpp` that the NVS port would otherwise have
to be adapted around.

### Execution order after these decisions

1. P1 correctness bundle — `36621b6`, `1c80751`, `b97d97c` (the `clearErrorFlags(0xFF)`
   half only, until `shadowPrefs` lands).
2. P1 security — `e36a3cb` portal-mode URI restriction via the Plan 16 auth gate.
3. Group A removal — `ENABLE_OCPP` flag first (decision 1b, done), then v4 / CH32 (decision 1).
4. Group E — `shadowPrefs` NVS rework (decision 2), plus `e72abb6` and `459e182`.
5. Interop — `2ae8b2e` + `1ea2301` MQTT `StateID` through the Plan 08 change-only slots.
6. Meters — Chint DDSU666 at slot 20 (decision 4) + Plan 12 profile.
7. EtherLCD branch top-up — `c725bb2`, `9e1faa9`, `5d1e849`, still gated on hardware.
8. Analysis-only — network meters (group F) and the remaining group D evaluation items.

---

## Sync: 2026-04-13 — Triage (13 commits since `ecd088b`)

| # | Hash | Date | Author | Title | Classification | Priority | Fork PR | Notes |
|---|------|------|--------|-------|----------------|----------|---------|-------|
| 1 | `e6110b1` | 2026-03-31 | stegen | Fix cable disconnect not detected when switching to PAUSE (fixes #347) | **Integrated** | **P1** | #133 | Applied in `evse_bridge.cpp` STATE_A/B1 and STATE_C1 paths (2-line fix) |
| 2 | `cdc8f67` | 2026-03-31 | dingo35 | Prevent `[Mains\|EV]Meter.[Im\|Ex]port_active_energy` exported when zero | **Already fixed** | — | — | Fork already has broader `> 0` guards at `esp32.cpp:1257-1300` + Circuit meter. No action. |
| 3 | `b104576` | 2026-03-31 | stegen | `modbus.cpp`: do not advance request loop on broadcast timeouts | **Integrated** | P2 | (P2 bundle) | Verbatim; 1-line guard on `BROADCAST_ADR` |
| 4 | `4e6c06d` | 2026-04-01 | dingo35 | `update2.html`: warning message + layout | **Integrated** | P4 | (P4 batch) | Applied verbatim to fork's update2.html (label clarity, HTTPS-upload warning, button caps) |
| 5 | `543af26` | 2026-04-01 | stegen | EtherLCD support: Ethernet add-on board that replaces the LCD board (#349) | **Parked on long-lived test branch** | P3 | branch [`upstream/543af26-etherlcd-test`](https://github.com/basmeerman/SmartEVSE-3.5/tree/upstream/543af26-etherlcd-test) | Cherry-pick + minimal compile fixes done. Branch builds (ESP32+CH32) and 51 native suites pass. **Several upstream `esp32.cpp` and `network_common.cpp` integration points are NOT applied** (HEAD's pure-C extractions preserved instead). Per user direction: weeks of on-device hardware testing required before any master PR. Full status, missing-integrations checklist and on-device test protocol live in [`analysis-543af26-etherlcd.md`](https://github.com/basmeerman/SmartEVSE-3.5/blob/upstream/543af26-etherlcd-test/docs/upstream-sync/analysis-543af26-etherlcd.md) on that branch. |
| 6 | `afd72a8` | 2026-04-03 | stegen | OCPP: send Finishing state before Available (fixes #348) | **Integrated** | P2 | (P2 bundle) | Decision extracted to `ocpp_should_report_occupied()` in ocpp_logic.c; 6 unit tests |
| 7 | `74e20c8` | 2026-04-07 | stegen | `main.cpp`: reset ChargeDelay countdown when solar power disappears (master) | **Integrated** | P2 | (P2 bundle) | Ported into pure C `evse_tick_1s()`; 3 unit tests in test_tick_1s.c |
| 8 | `2c015fb` | 2026-04-08 | Juurlink | Improved Raw Settings view: formatted JSON + Download button (#353) | **Evaluated — defer to Plan 07** | P3 | — | 280/25 lines lands in fork's 500-line diverged `index.html`; stylesheet rename collision (`styling.css` vs fork `style.css`); `packfs.py` path differs. No functional regression from keeping existing Raw Data link. See [analysis](analysis-2c015fb-raw-settings-ui.md). |
| 9 | `3ab1cee` | 2026-04-08 | stegen | `main.cpp`: reset Node ChargeDelay countdown when solar power disappears | **Integrated** | P2 | (P2 bundle) | Applied in `processAllNodeStates()` master-side slave-node error tracking |
| 10 | `a54b07f` | 2026-04-09 | stegen | `main.cpp`: prevent current fluctuations when CAPACITY is used (fixes #327) | **Integrated** | P2 | (P2 bundle) | Applied in pure C `evse_calc_balanced_current()`; 3 unit tests. `test_s9_maxsummains_limits` updated to use larger exceedance (Isum 350→600) so per-phase reduction crosses fork's SmartDeadBand — documents the gentler, correct per-phase semantics. |
| 11 | `92d42eb` | 2026-04-10 | Juurlink | Refactor tooltips: centralize styles in `styling.css` + a11y (#301) | **Evaluated — defer to Plan 07** | P4 | — | Same `styling.css` vs fork's `style.css` naming collision as #8 (2c015fb). Tooltip refactor + a11y improvements are nice-to-have; fold into Plan 07 Web UI Modernization when executed. No functional regression. |
| 12 | `3679fe3` | 2026-04-10 | stegen | OCPP: public charging station LED colour scheme when OCPP is enabled (#351) | **Integrated** | P3 | (this PR) | Public scheme extracted to `led_public_compute()` in `led_color.c`; 14 unit tests. `MENU_LEDMODE=51` (fork avoids renumber cascade). |
| 13 | `790f2a9` | 2026-04-10 | stegen | `docs`: update OCPP documentation | **Integrated (adapted)** | P4 | (P4 batch) | Applied the restructured OCPP section to `docs/configuration.md`. Omitted the "Remote firmware updates over OCPP" claim — that's the 190777f feature which is deferred in the fork. |

### Suggested batching

1. **Safety / P1 first:**
   - #1 `e6110b1` cable-disconnect-on-PAUSE — needs fork state-machine analysis
2. **Bug-fix bundle (P2) — all solar/load-balancing/OCPP stability:**
   - #3 `b104576` Modbus broadcast timeout
   - #6 `afd72a8` OCPP Finishing→Available sequence
   - #7 + #9 `74e20c8` + `3ab1cee` ChargeDelay reset (bundled, both sides)
   - #10 `a54b07f` CAPACITY current fluctuation — review for overlap with Plan 13
3. **Features (P3) — each as separate PR:**
   - #12 `3679fe3` OCPP LED scheme — adapt into `led_color.c`
   - #8 `2c015fb` Raw Settings UI — **evaluated and deferred** to Plan 07 (Web UI Modernization); see analysis
   - #5 `543af26` EtherLCD — **parked on long-lived branch** `upstream/543af26-etherlcd-test`. Cherry-pick + minimal compile fixes only; missing integrations documented on that branch. Awaiting on-device hardware bring-up.
4. **Cosmetic / docs (P4) — processed:**
   - #4 `4e6c06d` update2.html — **integrated**
   - #11 `92d42eb` tooltip CSS — **deferred to Plan 07** (styling.css name collision)
   - #13 `790f2a9` OCPP docs update — **integrated (adapted)**
5. **No action (already fixed):**
   - #2 `cdc8f67` energy zero-value guard — noted, no action

---

## Sync: 2026-03-29 — Triage (CLOSED)

| # | Hash | Date | Author | Title | Classification | Priority | Fork PR | Notes |
|---|------|------|--------|-------|---------------|----------|---------|-------|
| 1 | `ecd088b` | 2026-03-29 | stegen | OCPP: recover from silent session loss (#345) | **Integrated** | P2 | #130 | Logic extracted to `ocpp_silence_decide()` in ocpp_logic.c, 10 unit tests |
| 2 | `05c7fc2` | 2026-03-27 | stegen | OCPP: prevent actuator unlock/relock jitter | **Integrated** | P2 | #130 | Logic extracted to `ocpp_should_force_lock()` in ocpp_logic.c, 11 unit tests |
| 3 | `02dafa2` | 2026-03-27 | stegen | Fix: Solar 1P stop timer | **Rejected** | P1 | #119 (alt) | Same bug as our PR #119; upstream's fix is incorrect — see analysis |
| 4 | `190777f` | 2026-03-25 | stegen | Add OCPP firmware update functionality | **Evaluated — adopt later** | P3 | — | Multi-key compatible (validation path unchanged); deferred to separate PR — see [analysis](analysis-190777f-ocpp-firmware-update.md) |
| 5 | `c0c6b16` | 2026-02-25 | hmmbob | Improve integrations section (#334) | Docs only | P4 | — | ESPHome configs, no firmware |

---

## Commit Analyses

### #3: `02dafa2` — Fix: Solar 1P stop timer (CONFLICTS WITH FORK — REJECTED)

**Summary:** Upstream fixed the same SolarStopTimer threshold bug that our PR #119
addressed, but with a different (and incorrect) approach.

**Decision:** **Reject upstream change.** Keep PR #119. Documented as a conscious
divergence in `docs/upstream-differences.md`.

**Full analysis:** [analysis-02dafa2-solar-stop-threshold.md](analysis-02dafa2-solar-stop-threshold.md).

**One-line rationale:** Upstream removed `Nr_Of_Phases_Charging` but kept
`ActiveEVSE`. Our fix removed `ActiveEVSE` but kept `Nr_Of_Phases_Charging`.
Working from the EVSE's actual perspective (it only sees `Isum` from the mains
meter, not house/solar separately), and tracing the code path through phase
switching, the upstream formula:

- Reproduces the original `ActiveEVSE` scaling bug for multi-node setups (timer
  threshold grows with node count and becomes unreachable)
- Causes stop/start cycling for fixed 3-phase configurations (`EnableC2 != AUTO`),
  because the threshold becomes 2A when the actual single-EVSE 3-phase draw is 18A

Our formula adapts correctly via `Nr_Of_Phases_Charging` (which is set by the
phase-switch logic that runs *before* SolarStopTimer fires) and is constant
regardless of node count.

**Sub-change in same upstream commit:** `static uint8_t Broadcast = 1` → `= 4` in
`timer1s_modbus_broadcast()`. Delays the first Modbus broadcast from ~1s to ~4s
after boot (one-shot init delay). Low value, low risk. Tracked as **P4** —
evaluate independently if/when we touch Modbus init timing.

### #1 + #2: `ecd088b` + `05c7fc2` — OCPP resilience (INTEGRATED)

**Bundled** as one fork PR. Both touch only `esp32.cpp` (firmware glue) plus
one line in `main.cpp` (global declaration).

**#1 — `ecd088b` — Silent session loss recovery**
The MicroOcpp WebSocket layer keeps the transport alive with ping/pong frames,
but those don't prove the OCPP backend is still processing application
messages. Upstream's fix sends periodic Heartbeat probes and forces a WebSocket
reconnect when the backend stays silent past a timeout. In the fork, the timing
decision was extracted into `ocpp_silence_decide()` (pure C in `ocpp_logic.c`)
so the (now/last_response/last_probe → action) mapping can be unit-tested
without millis() or MicroOcpp. The glue layer in `ocppLoop()` calls the pure
function and dispatches `sendRequest("Heartbeat")` / `reloadConfigs()`.

  - 10 unit tests in `test_ocpp_resilience.c` (REQ-OCPP-100..104)
  - Constants `OCPP_PROBE_INTERVAL_MS = 90000` and `OCPP_SILENCE_TIMEOUT_MS = 300000`
    match upstream
  - Cold-boot guard: `last_response_ms == 0` cannot trigger reconnect
  - Reconnect priority over probe verified by test

**#2 — `05c7fc2` — Actuator unlock/relock jitter**
Upstream bug: `OcppForcesLock` was reset to false unconditionally and then
conditionally set to true within the same `ocppLoop()` iteration. The actuator
dispatcher could sample mid-flip and translate the brief false→true into rapid
unlock/relock cycling. The fix is to compute the lock decision once and assign
once. In the fork, the decision is now `ocpp_should_force_lock()` (pure C);
the glue layer assigns the result in a single statement, achieving the same
atomicity and gaining exhaustive unit-test coverage.

  - 11 unit tests in `test_ocpp_connector.c` (REQ-OCPP-110..113)
  - Boundary tests for `PILOT_3V` and `PILOT_9V`
  - Tests for both lock conditions independently and combined
  - All-false baseline asserted

**Verification:** Full 5-step pre-push pipeline (native tests, ASan+UBSan,
cppcheck, ESP32 release build, CH32 build) all green. Traceability spec
regenerated.

### #4: `190777f` — Add OCPP firmware update functionality (EVALUATED — DEFER)

**Multi-key compatibility:** ✅ **fully compatible, no adaptation needed.**
Upstream calls `forceUpdate(url, /*validate=*/true)` which the fork already
routes through `validate_sig()` — and PR #125 made `validate_sig()` a
multi-key loop. Both upstream-signed and fork-signed firmware can be pushed
via OCPP without code changes.

**Other concerns** (not blockers, but should be addressed when integrating):

1. **Stack budget** — upstream creates a 4096-byte FreeRTOS task; CLAUDE.md
   requires a memory-budget check + rationale for new task creation.
2. **`OcppFwStatus` race** — written by download task, read by OCPP loop
   task; uses `volatile int` only. Acceptable on Xtensa but worth wrapping
   for consistency.
3. **`shouldReboot` reuse** — the install-status callback reads `shouldReboot`
   which is also set by web-UI updates and other paths; could report
   `Installed` for the OCPP transaction even when the OCPP install never ran.
4. **Concurrent update guard** — races with web-UI update path on
   `downloadProgress`.

**Decision:** Defer to a separate P3 PR. Needs on-device CSMS verification
(push fork-signed, upstream-signed, unsigned, and corrupted firmware).

**Full analysis:** [analysis-190777f-ocpp-firmware-update.md](analysis-190777f-ocpp-firmware-update.md).

### #5: `c0c6b16` — Improve integrations section

**Summary:** Adds ESPHome YAML configurations for various smart meter modules.
Documentation only, no firmware changes.

**Action:** Skip — integrations/ directory is not fork-specific.

---

## Next Actions

1. [x] **#3 (Solar 1P):** Rejected. Documented as conscious divergence in
        `upstream-differences.md`. `Broadcast = 4` sub-change deferred (P4).
2. [x] **#1 + #2 (OCPP resilience):** Integrated as bundled fork PR with
        pure C extraction and 21 unit tests (PR #130, merged).
3. [x] **#4 (OCPP FW update):** Evaluated — multi-key compatible. Deferred to
        separate P3 PR (needs on-device CSMS verification + memory budget review).
4. [x] **#5 (Docs):** Skipped — ESPHome integrations dir not fork-specific.
