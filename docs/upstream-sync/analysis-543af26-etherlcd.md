# Analysis: Upstream Commit 543af26 — EtherLCD Support

## Status: Long-lived test branch

This commit is being held on a **separate long-lived branch**
(`upstream/543af26-etherlcd-test`) per user direction:

> "For the EtherLCD we need a separate branch to test before merging,
> this will take a few weeks while also debug info is available from
> upstream repo's so work on that last."

**The branch is intended for hardware bring-up testing on a real EtherLCD board,
NOT for merge to master until cleared.**

## What 543af26 does

Adds support for the **EtherLCD** add-on board, which replaces the standard
SmartEVSE LCD board with an integrated Ethernet+LCD board:

| Component | Role |
|---|---|
| **CH390D** | SPI-attached Ethernet MAC/PHY chip — adds wired networking to the v3 ESP32 |
| **CH32V003** | Microcontroller on the add-on board that takes over LCD control + button reading (pins are now occupied by Ethernet SPI) |

The same six GPIO pins that originally drove the LCD and buttons are reused
for the CH390D's SPI bus. The CH32V003 brokers LCD control and button input
through a separate I²C/register interface.

The patch is **1768 insertions, 122 deletions, across 11 files**:

| File | Type | LOC |
|---|---|---|
| `ch390.cpp` / `.h` | NEW — CH390D ESP-IDF eth driver | 1217 |
| `etherlcd.cpp` / `.h` | NEW — CH32V003 board interface | 298 |
| `esp32.cpp` | Modified — pin probe, backlight wrapper, button override, /settings JSON, OCPP NetworkConnected | +105 |
| `esp32.h` | Modified — `_RSTB_*` / `_A0_*` macros wrap to etherlcd functions when EthPresent | +12 |
| `glcd.cpp` | Modified — backlight calls go through `setLCDbacklight()` | +69 |
| `main.cpp` | Modified — show Ethernet IP/time on LCD | +8 |
| `main.h` | Modified — declarations | +3 |
| `network_common.cpp` | Modified — Ethernet event handlers, `NetworkConnected()`, WiFi-stop-when-Eth-up | +174 |
| `network_common.h` | Modified — `NetworkConnected()` extern + Ethernet status flags | +4 |

## What's already on the test branch

Cherry-picked verbatim:
- `ch390.cpp`, `ch390.h` — CH390D driver (new files, no conflict)
- `etherlcd.cpp`, `etherlcd.h` — board interface (new files, no conflict)
- `esp32.h`, `glcd.cpp`, `main.h`, `network_common.h` — auto-merged successfully

Conflict-resolved with HEAD's version (preserves fork's pure-C extractions):
- `esp32.cpp`, `main.cpp`, `network_common.cpp`

Compile fixes applied to the new files:
- `ch390.cpp`, `etherlcd.cpp` — moved `#include <Arduino.h>` inside the
  `SMARTEVSE_VERSION` guard so CH32 build doesn't choke on Arduino.h (upstream
  bug — left the include unconditional)

Stub added to `esp32.cpp`:
- `setLCDbacklight()` — wrapper that calls `etherlcd_set_backlight()` if
  `EthPresent` else `ledcWrite(LCD_CHANNEL, pwm)`. Resolves the linker
  reference from `glcd.cpp` and `main.cpp`. Body matches upstream's
  definition; the wrapper itself is the only piece of upstream's `esp32.cpp`
  changes restored on this branch.

Verification at branch creation:
- ✅ ESP32 release build SUCCESS
- ✅ CH32 build SUCCESS
- ✅ 51 native test suites pass

## What's MISSING from the test branch

The conflict resolution dropped most of upstream's `esp32.cpp` and
`network_common.cpp` integration changes. The new driver files are present
but **not exercised at runtime yet**. Specifically:

### `esp32.cpp`

| Upstream change | Status on branch |
|---|---|
| `setLCDbacklight()` body (LCD backlight wrapper) | ✅ added |
| `getButtonState()` — read from CH32V003 when `EthPresent` | ❌ missing — buttons still go to direct GPIO regardless |
| `setup()` — probe CH390D, init Ethernet OR LCD | ❌ missing — no `ch390_detect()` call, no `ch390_eth_init()`, no `etherlcd_init()` |
| `setup()` — conditional `ledcSetup`/`ledcAttachPin` for LCD | ❌ missing |
| `handle_URI()` — add `eth.present`, `eth.connected`, `eth.has_ip`, `eth.ip`, `eth.mac` to `/settings` JSON | ❌ missing — also the fork has handle_URI in `http_handlers.cpp`, so the change has to be ported there |
| OCPP lifecycle: `WiFi.isConnected()` → `NetworkConnected()` | ❌ missing — OCPP won't come up over Ethernet-only |

### `network_common.cpp`

The fork's `network_common.cpp` is heavily diverged (WebSocket data channel,
HomeWizard P1 mDNS, etc.). The upstream changes here are substantial:

| Upstream change | Status on branch |
|---|---|
| `NetworkConnected()` definition (returns WiFi.isConnected() OR EthHasIP) | ❌ missing |
| Ethernet event handlers (`onEthernetEvent`) | ❌ missing |
| WiFi stop when Ethernet has IP | ❌ missing |
| Mongoose binding to Ethernet interface | ❌ missing |

Without these, the EtherLCD board would boot, the CH390D could in theory be
detected (if `setup()` called `ch390_detect()`), but no IP would be
obtained and no network services would run on Ethernet.

## Pre-merge work needed (for whoever picks this back up)

1. **`esp32.cpp::setup()`** — add the probe/init block:
   ```cpp
   if (ch390_detect()) {
       _LOG_A("CH390D Ethernet detected\n");
       ch390_eth_init();
       etherlcd_init();
   } else {
       // existing LCD pin setup (current code path)
   }
   ```
   Also condition `ledcSetup(LCD_CHANNEL, ...)` and
   `ledcAttachPin(PIN_LCD_LED, LCD_CHANNEL)` on `!EthPresent`.

2. **`esp32.cpp::getButtonState()`** — wrap the existing block:
   ```cpp
   if (EthPresent) {
       ButtonState = etherlcd_read_buttons() & 0x07;
   } else {
       // existing code
   }
   ```

3. **`network_common.cpp`** — port upstream's Ethernet event handling and
   `NetworkConnected()` definition into the fork's structure. Most invasive
   change — fork's WiFi/MQTT init flow may need restructuring so that the
   network-up signal can come from either interface.

4. **`http_handlers.cpp`** (fork-specific location) — port the upstream
   `eth.*` JSON additions into the `/settings` endpoint.

5. **OCPP lifecycle** — replace `WiFi.isConnected()` with `NetworkConnected()`
   in `esp32.cpp::loop()` (fork still has this call after we took --ours).

## Pre-integration on-device test protocol

Hardware required:
- A SmartEVSE v3 board fitted with the EtherLCD add-on (CH390D + CH32V003).
- An Ethernet patch cable to a router with DHCP.
- Optionally: same board WITHOUT the EtherLCD add-on, to verify backwards
  compatibility (probe must fail-safe to LCD).

### Bring-up checklist

- [ ] Flash this branch's firmware to a v3 board WITHOUT EtherLCD attached.
      Verify the LCD still works, buttons still work, WiFi still works.
      (Confirms the CH390D probe doesn't false-positive.)
- [ ] Flash to a v3 board WITH EtherLCD attached. Confirm:
  - [ ] Boot logs show "CH390D Ethernet detected"
  - [ ] Backlight comes up (via CH32V003)
  - [ ] LCD draws (via CH32V003 SPI broker)
  - [ ] All three buttons read correctly
  - [ ] DHCP completes; charger gets an IPv4 address
  - [ ] Web UI reachable over Ethernet IP
  - [ ] WiFi shut down (per upstream's design)
  - [ ] `/settings` JSON includes `eth.present`, `eth.connected`, `eth.has_ip`, `eth.ip`, `eth.mac`
- [ ] OCPP test (with backend):
  - [ ] OCPP comes up over Ethernet (NetworkConnected covers Eth)
  - [ ] StatusNotification / Heartbeat flow normally
- [ ] MQTT test:
  - [ ] MQTT publish reaches broker over Ethernet
- [ ] Stress / soak:
  - [ ] 24h continuous charging session — no Ethernet drops, no crashes
  - [ ] Pull / re-plug Ethernet cable — confirm reconnect, no crash
  - [ ] DHCP renewal cycle survives

### Negative tests

- [ ] CH390D detection failure path: simulate broken add-on by holding
  `CH390_INT` low at boot. Charger should fall back to LCD mode without crashing.
- [ ] No-link condition: plug Ethernet cable into a switch with no DHCP server.
  Charger should not block waiting for IP.

## Decision when ready

When the on-device tests pass:
1. Apply the missing `esp32.cpp` / `network_common.cpp` changes listed above
2. Port the `eth.*` `/settings` JSON to `http_handlers.cpp`
3. Run full 5-step verification
4. Open PR to master with on-device test results in the body
5. Tag commit with `Upstream-Commit: 543af26b6fec65e7fde772c19ecb8d04d2f09c8a`

If on-device tests reveal blocking issues, file an upstream issue with
findings; this branch can stay open indefinitely as a sandbox.

## Branch lifecycle

- Branch name: `upstream/543af26-etherlcd-test`
- Pushed to: `myfork`
- **Do NOT delete** without explicit user approval — this is a long-lived
  hardware-test branch.
- Periodically rebase on master to stay current with other fork work
  (`git rebase myfork/master` from this branch).
