# Differences from the reference codebase

This is a maintainer-facing document describing how this firmware
diverges from the [reference codebase
(`dingo35/SmartEVSE-3.5`)](https://github.com/dingo35/SmartEVSE-3.5).
It is intended for contributors deciding whether a behaviour, fix, or
feature originates here or upstream, and for porting changes between
the two.

For end-user feature documentation see [Features](features.md). For
operational guidance see the audience guides
([installer](guide-installer.md), [owner](guide-owner.md),
[integrator](guide-integrator.md)).

---

## Architecture changes

Structural changes that affect the entire codebase — not specific features.

| Change | Reference state | This codebase |
|--------|---------------|------------|
| State machine location | Inline in `main.cpp` (~3,000 lines) | Extracted to `evse_state_machine.c` (pure C) |
| State representation | ~70 scattered globals | `evse_ctx_t` context struct |
| Hardware abstraction | Direct GPIO calls in logic | Function pointers via HAL callbacks |
| Global synchronization | No protection | `evse_bridge.cpp` with spinlock/mutex |
| Native testability | Not possible (Arduino dependencies) | 1,096 tests compile with plain `gcc` |
| CI pipeline | PlatformIO build only | 10-job pipeline (tests, sanitizers, valgrind, cppcheck, builds, BDD, traceability, OCPP, Modbus) |
| Test methodology | None | Specification-by-Example (SbE) with traceability |

---

## Feature Differences by Area

### Smart & Solar Mode

Background — community reports:
[#327](https://github.com/dingo35/SmartEVSE-3.5/issues/327),
[#335](https://github.com/dingo35/SmartEVSE-3.5/issues/335),
[#316](https://github.com/dingo35/SmartEVSE-3.5/issues/316).

| Improvement | Why | Details |
|-------------|-----|---------|
| EMA current smoothing | Oscillation in smart/solar modes | [Features: Solar & Smart Mode](features.md#solar--smart-mode) |
| Dead band regulation | Micro-adjustments cause unnecessary switching | [Features: Solar & Smart Mode](features.md#solar--smart-mode) |
| Symmetric ramp rates | Overshoot/undershoot from asymmetric regulation | [Features: Solar & Smart Mode](features.md#solar--smart-mode) |
| Tiered phase switching timers | Rapid 1P/3P cycling | [Features: Solar & Smart Mode](features.md#solar--smart-mode) |
| Stop/start cycling prevention | Solar mode stops and restarts unnecessarily | [Features: Solar & Smart Mode](features.md#solar--smart-mode) |
| Multi-node SolarStopTimer fix | Upstream threshold scales with `ActiveEVSE`, unreachable for 2+ nodes (commit `94ca08e`). Upstream attempted a different fix in `02dafa2` that we evaluated and rejected — it reproduces the multi-node scaling bug and causes stop/start cycling for fixed 3-phase. See [analysis](upstream-sync/analysis-02dafa2-solar-stop-threshold.md). | [PR #119](https://github.com/basmeerman/SmartEVSE-3.5/pull/119) |
| Slave mode sync via setMode() | Upstream `SETITEM(MENU_MODE)` skips phase switching and error clearing on slaves | [PR #121](https://github.com/basmeerman/SmartEVSE-3.5/pull/121) |
| Slow EV compatibility | Renault Zoe stalls on rapid current changes | [Features: Solar & Smart Mode](features.md#solar--smart-mode) |
| ChargeDelay re-set on solar loss | Solar mode ChargeDelay could expire without solar, causing charging-without-solar oscillation — integrated upstream `74e20c8` (master) + `3ab1cee` (node-side) with 3 unit tests | upstream `74e20c8`, `3ab1cee` |

### Load Balancing

Background — community report:
[#316](https://github.com/dingo35/SmartEVSE-3.5/issues/316).

| Improvement | Why | Details |
|-------------|-----|---------|
| Oscillation dampening | Current hunting between nodes | [Features: Load Balancing](features.md#load-balancing--power-sharing) |
| EMA filter on Idifference | Measurement noise causes false triggers | [Features: Load Balancing](features.md#load-balancing--power-sharing) |
| Distribution smoothing | Sudden jumps stress contactors and EV controllers | [Features: Load Balancing](features.md#load-balancing--power-sharing) |
| Diagnostic snapshot | No visibility into load balancing decisions | [Features: Load Balancing](features.md#load-balancing--power-sharing) |
| 126 convergence tests | Algorithm changes blocked by regression risk | [Features: Load Balancing](features.md#load-balancing--power-sharing) |
| CAPACITY fluctuation fix | `MaxSumMains` overwrote per-phase Idifference with a sum-of-phases value, causing current fluctuations when the EU capacity limit was configured — integrated upstream `a54b07f` (fixes #327) with 3 unit tests | upstream `a54b07f` |

### RFID, OCPP & Authorization

| Improvement | Why | Details |
|-------------|-----|---------|
| RFID toggle bug fix | Next RFID swipe toggles OFF instead of ON after Tesla disconnect | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| Bridge transaction mutex | Daily OCPP session failures from concurrent task corruption | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| Pure C OCPP logic extraction | OCPP logic untestable (85 tests added) | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| LoadBl exclusivity enforcement | OCPP limits silently ignored when LoadBl toggled at runtime | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| FreeVend solar safety | Auto-authorize bypasses solar surplus checks | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| OCPP settings validation | Invalid URLs/IDs accepted silently | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| OCPP connection telemetry | No diagnostics for connection drops | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| IEC 61851 → OCPP status mapping | EVCC integration needs standard status codes | [Features: OCPP & Authorization](features.md#rfid-ocpp--authorization) |
| Silent OCPP session loss recovery | WebSocket pings keep transport alive but don't prove backend is processing OCPP messages — integrated upstream `ecd088b` with timing logic extracted to pure C and 10 unit tests | upstream `ecd088b` |
| Atomic connector lock decision | Upstream `ocppLoop()` briefly flipped `OcppForcesLock` false→true within one iteration, causing actuator unlock/relock jitter — integrated upstream `05c7fc2` with the decision extracted to pure C and 11 unit tests | upstream `05c7fc2` |
| OCPP Finishing-before-Available sequence | CSMS missed the Finishing state because OccupiedInput went false immediately after StopTx — integrated upstream `afd72a8` with the decision extracted to pure C (`ocpp_should_report_occupied`) and 6 unit tests | upstream `afd72a8` |
| Cable disconnect detection under PAUSE | CP sampling timer left disabled after STATE_B single-shot fired — integrated upstream `e6110b1` (timerAlarmEnable in STATE_A/STATE_C1 paths) | upstream `e6110b1` |
| Public charging station LED scheme | Public-charger color semantics (available/connected/charging/reserved/faulted) selectable via new `LedMode` menu — integrated upstream `3679fe3` with the decision tree extracted to pure C (`led_public_compute()`) and 14 unit tests. Fork keeps `MENU_LEDMODE=51` to avoid renumber cascade. | upstream `3679fe3` |

### MQTT & Home Assistant

Background — community reports:
[#320](https://github.com/dingo35/SmartEVSE-3.5/issues/320),
[#294](https://github.com/dingo35/SmartEVSE-3.5/issues/294),
[PR #338](https://github.com/dingo35/SmartEVSE-3.5/pull/338).

| Improvement | Why | Details |
|-------------|-----|---------|
| Change-only publishing | 70-97% MQTT message reduction | [Features: MQTT & HA](features.md#mqtt--home-assistant) |
| Fixed HA discovery payloads | Corrupted long-term statistics | [Features: MQTT & HA](features.md#mqtt--home-assistant) |
| Energy zero-value guard | Phantom consumption in HA dashboard | [Features: MQTT & HA](features.md#mqtt--home-assistant) |
| Entity naming cleanup | HA 2025.10+ compatibility | [Features: MQTT & HA](features.md#mqtt--home-assistant) |
| New entities | Missing diagnostics (FreeHeap, MQTTMsgCount, etc.) | [Features: MQTT & HA](features.md#mqtt--home-assistant) |
| Per-phase power/energy via MQTT | No per-phase visibility | [Features: MQTT & HA](features.md#mqtt--home-assistant) |
| Metering diagnostic counters | No insight into meter communication health | [Features: MQTT & HA](features.md#mqtt--home-assistant) |

### Settings Persistence

| Improvement | Why | Details |
|-------------|-----|---------|
| NVS change detection reads NVS, not a RAM mirror | Adopts the substance of upstream `bd2475a` ("get rid of settingsCache because most of NVS's page is already in RAM"). The former 50-field `settingsCache` struct had to be declared, populated on boot and kept in step by hand, and a duplicated name between a value and its cache field silently disabled change detection for that key. | `esp32.cpp` |
| Write scheduling extracted to pure C | Upstream keeps the policy inline in a templated `ShadowPreferences` class over `std::map<String, Entry>`. The fork puts the timing decision in `nvs_shadow.c` so it is unit-testable, and uses a fixed table instead of heap-allocated map entries. | `nvs_shadow.c`, 10 tests |
| All NVS writes stay in the main loop | Upstream's `reg()` opens its own `Preferences` handle and may write from the web-server task while `loop()` writes from the main task. The fork keeps marking (a flag) separate from writing (main loop only), so no concurrent NVS handles exist. | `esp32.cpp` |
| Forced flush on LCD menu exit and reboot | Completes the second half of upstream `b97d97c`; a change made in the menu is no longer lost if power is cut before the 60s window closes | `write_settings_now()` |
| Monthly capacity peak written only on change | `CapPeak` / `CapMonth` were written on every flush, costing a flash write per minute even when the peak had not moved | `esp32.cpp` |

### Versioning & Updates

| Improvement | Why | Details |
|-------------|-----|---------|
| Fork-branded CalVer (`bm-YYYY.MM.N`) | Both projects previously published `vX.Y.Z` releases, so a version string alone could not tell a user or a maintainer which repository built a device — a recurring source of confusion in support threads. The two codebases are aligned functionally, not commit-by-commit, so a comparable number would imply a lineage that does not exist. | [Building & flashing](building_flashing.md#version-numbering) |
| `distribution` identity field | `GET /settings` and MQTT `/Distribution` report which build is running, next to the version | [MQTT reference](mqtt-home-assistant.md#full-mqtt-topic-reference) |
| Automatic updates stay within one distribution | The updater accepts an update only when both the running and candidate versions parse as this scheme, so it can never silently move a device to another distribution's firmware. Cross-distribution flashing remains available manually from the update page. | `fw_version.c`, 13 tests |
| Releases via GitHub assets only | This repository has no S3 credentials and never writes to the reference codebase's bucket; the release workflow's dormant S3 upload was removed | `.github/workflows/pio-release.yaml` |

### Metering & Modbus

| Improvement | Why | Details |
|-------------|-----|---------|
| Orno WE-517/516 meter support | Community-requested meters | [Features: Metering](features.md#metering--modbus) |
| Chint DDSU666 at meter slot 20 | **Meter type ID divergence.** Upstream `f92dce8` placed its Chint DDSU666 (single phase) at `EMConfig[]` slot 17 and renamed slot 15 to `EM_CHINT_3P`. This fork has used slots 17 and 18 for `EM_ORNO3P` / `EM_ORNO1P` since Plan 05, and the meter type is persisted in NVS and exposed over MQTT and `/settings`. Reusing 17 would silently reinterpret an existing Orno configuration as a Chint one — wrong register map, wrong readings. The fork therefore appends `EM_CHINT_1P` at slot 20. **Consequence: meter type numbers 17, 18 and 20 mean different things in the two firmwares; cross-flashing rewrites the configured meter.** | upstream `f92dce8` |
| Modbus broadcast timeout handling | Timeout on a broadcast address would advance the request loop and skip the next legitimate slave response — integrated upstream `b104576` (1-line guard) | upstream `b104576` |
| Pure C Modbus frame decoder | Modbus logic untestable | [Features: Metering](features.md#metering--modbus) |
| Pure C meter byte decoder | 30 test scenarios for all endianness/data types | [Features: Metering](features.md#metering--modbus) |
| Pure C HomeWizard P1 parser | P1 parsing untestable | [Features: Metering](features.md#metering--modbus) |
| Meter telemetry counters | No visibility into communication errors | [Features: Metering](features.md#metering--modbus) |
| Modbus frame event logger | No debugging capability for Modbus issues | [Features: Metering](features.md#metering--modbus) |
| API/MQTT staleness detection | Stale API data causes incorrect charging | [Features: Metering](features.md#metering--modbus) |
| HomeWizard P1 energy data | HA energy dashboard incomplete | [Features: Metering](features.md#metering--modbus) |
| HomeWizard P1 manual IP fallback | mDNS unreliable on some networks | [Features: Metering](features.md#metering--modbus) |

### EVCC Integration

| Improvement | Why | Details |
|-------------|-----|---------|
| IEC 61851-1 state mapping | EVCC needs standard state letters (A-F) | [Features: EVCC](features.md#evcc-integration) |
| Phase switching via HTTP | EVCC needs to control 1P/3P switching | [Features: EVCC](features.md#evcc-integration) |
| Charging state derivation | EVCC needs `charging_enabled` boolean | [Features: EVCC](features.md#evcc-integration) |
| Ready-to-use EVCC template | No documentation for EVCC setup | [Features: EVCC](features.md#evcc-integration) |

### Diagnostic Telemetry (New in Fork)

| Feature | Purpose | Details |
|---------|---------|---------|
| Ring buffer event capture | Captures state machine events, errors, meter readings | [Features: Diagnostics](features.md#diagnostic-telemetry) |
| LittleFS persistence | Diagnostics survive reboots | [Features: Diagnostics](features.md#diagnostic-telemetry) |
| WebSocket live stream | Real-time diagnostic viewer in web UI | [Features: Diagnostics](features.md#diagnostic-telemetry) |
| Test replay framework | Replay recorded sessions through test suite | [Features: Diagnostics](features.md#diagnostic-telemetry) |
| MQTT profile control | Remote diagnostic capture control | [Features: Diagnostics](features.md#diagnostic-telemetry) |

### ERE Session Logging (New in Fork)

| Feature | Purpose | Details |
|---------|---------|---------|
| Charge session tracking | Automatic per-charge session recording | [Features: ERE](features.md#ere-session-logging) |
| ERE-compatible output | Dutch ERE certificate submission format | [Features: ERE](features.md#ere-session-logging) |
| MQTT session publish | Retained JSON on session complete | [Features: ERE](features.md#ere-session-logging) |
| REST endpoint | GET /session/last for integrations | [Features: ERE](features.md#ere-session-logging) |
| Zero flash wear | MQTT-only persistence, no flash writes | [Features: ERE](features.md#ere-session-logging) |

### Capacity Tariff Peak Tracking (New in Fork)

| Feature | Purpose | Details |
|---------|---------|---------|
| 15-minute quarter-peak averaging | Matches Belgian DSO metering interval | [Features: Capacity Tariff](features.md#capacity-tariff-peak-tracking) |
| Monthly peak tracking | Records highest 15-min average per month | [Features: Capacity Tariff](features.md#capacity-tariff-peak-tracking) |
| Automatic current reduction | Clamps IsetBalanced to stay under limit | [Features: Capacity Tariff](features.md#capacity-tariff-peak-tracking) |
| LCD/Web/MQTT/REST configuration | Full configuration from all interfaces | [Features: Capacity Tariff](features.md#capacity-tariff-peak-tracking) |
| Home Assistant integration | 4 auto-discovered entities | [Features: Capacity Tariff](features.md#capacity-tariff-peak-tracking) |

### CircuitMeter — Subpanel Metering (New in Fork)

| Feature | Purpose | Details |
|---------|---------|---------|
| Subpanel breaker protection | Limits EV charging to stay within breaker rating | [Features: CircuitMeter](features.md#circuitmeter--subpanel-metering) |
| ERE 2027 compliance support | Circuit-level energy measurement for Dutch ERE Path B | [Features: CircuitMeter](features.md#circuitmeter--subpanel-metering) |
| Reuses existing Meter class | Supports all 20 meter types with zero new meter code | [Features: CircuitMeter](features.md#circuitmeter--subpanel-metering) |
| MQTT + HA auto-discovery | Circuit current, power, energy, and MaxCircuitMains | [Features: CircuitMeter](features.md#circuitmeter--subpanel-metering) |

### SoC Injection via MQTT (New in Fork)

| Feature | Purpose | Details |
|---------|---------|---------|
| MQTT SoC topics | InitialSoC, FullSoC, EnergyCapacity, EnergyRequest, EVCCID | [Features: MQTT & HA](features.md#mqtt--home-assistant) |
| WiCAN OBD-II integration | Direct SoC reading from car CAN bus | [MQTT docs: WiCAN](mqtt-home-assistant.md#wican-obd-ii-integration) |
| Session-scoped values | Auto-clear on EV disconnect | [MQTT docs: SoC Injection](mqtt-home-assistant.md#soc-injection-via-mqtt) |

### Web & Connectivity

| Improvement | Why | Details |
|-------------|-----|---------|
| Offline-first web UI | CDN dependencies break isolated networks | [Features: Web](features.md#web--connectivity) |
| WebSocket data channel | HTTP polling is slow and wasteful | [Features: Web](features.md#web--connectivity) |
| Dashboard card redesign | Outdated UI | [Features: Web](features.md#web--connectivity) |
| Dark mode | Community demand | [Features: Web](features.md#web--connectivity) |
| Load balancing node overview | No multi-node visibility | [Features: Web](features.md#web--connectivity) |
| Diagnostic telemetry viewer | No way to view diagnostics in browser | [Features: Web](features.md#web--connectivity) |
| LCD widget modernization | Old layout, not responsive | [Features: Web](features.md#web--connectivity) |

### Security Hardening (Fork-only)

Findings from the security review (see internal report; most issues are inherited from upstream).

| Fix | Why | Details |
|-----|-----|---------|
| Unsigned firmware upload rejected at `POST /update` | Upstream accepts `firmware.bin` / `firmware.debug.bin` uploads over unauthenticated HTTP with no signature check → any LAN client can flash arbitrary firmware (unauthenticated RCE). Fork accepts only `*.signed.bin` — verified via the multi-key RSA validator from PR #125 | Security fix C-1 |
| OCPP auth_key never exposed via `/settings` GET | Upstream returns the plaintext OCPP backend basic-auth key to any client calling `GET /settings`. Fork emits only `auth_key_set: bool` and the Web UI shows a placeholder | Security fix C-2 |
| MQTT private password redacted from boot log | Upstream logs the full EC-private-key-hash on every boot. Fork logs a 4-char prefix then `[redacted]` | Security fix C-5 |
| `strncpy(RequiredEVCCID, ...)` always NUL-terminated | Upstream pattern leaves the buffer unterminated when source fills it; subsequent `%s` walks past end | Security fix H-5 |
| OCPP URL validator rejects SSRF targets | Upstream accepts `wss://127.0.0.1/`, `wss://[::1]/`, `wss://169.254.x/`, `wss://user:pass@host/`. Fork rejects loopback + link-local + embedded credentials. RFC1918 still allowed for self-hosted CSMS | Security fix H-4 |
| Full partition erase on signature failure | Upstream erases only the first `ENCRYPTED_BLOCK_SIZE` (4 KB) on sig-fail, leaving >4 KB of attacker bytes in flash. Fork erases the entire partition | Security fix M-4 |
| Hash buffer zeroed before free in `validate_sig` | Hygiene: keeps debug-memory dumps from trivially revealing the firmware fingerprint | Security fix M-3 |
| Opt-in HTTP auth layer (`AuthMode` NVS setting) | Upstream has no authentication on any HTTP endpoint. Fork adds `AuthMode` (0=Off legacy default, 1=Required) with a pure-C decision helper, LCD-PIN reuse, 30-min idle session timeout, Origin-based CSRF, and applied to every mutating endpoint + `GET /settings`. **Default 0 on upgrade so no install is bricked.** Integrations (HA / custom scripts) continue to work unchanged; security-conscious users explicitly opt in | Plan 16 — Phase 1 — closes C-3, H-1, H-2, H-3, H-7, M-1, M-2, M-8 when enabled |

---

## Contributing changes to the reference codebase

When porting a change to the reference codebase
(`dingo35/SmartEVSE-3.5`):

- **Submit small, focused PRs** — the upstream maintainer is
  conservative about large changes.
- **Do not bundle test infrastructure with feature changes** — the
  reference codebase has a different test approach; bundling slows
  review.
- **Follow the reference codebase's conventions** — naming, commit
  style, and code organisation differ.
- **Rewrite the commit message in plain technical English** — strip
  AI-generated tone (no "Co-Authored-By: Claude" trailers, no
  multi-paragraph rationale unless the maintainer asks).
- **Never modify other-owned repos without explicit user approval.**
