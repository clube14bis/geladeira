# ESP32 firmware — Clube 14 BIS Fridge

This folder contains the ESP32 firmware that receives new orders from Firebase Realtime Database and controls the refrigerator electromagnetic lock through a relay.

Current firmware version: **2.8.3**.

## What the firmware does

1. Connects to the first configured 2.4 GHz Wi-Fi network that is available.
2. Authenticates with Firebase using the device account.
3. Establishes a safe starting point and ignores commands that existed before boot.
4. Opens a Firebase Server-Sent Events stream only for minimal unlock commands.
5. On a new Boolean unlock command, waits six seconds, releases the lock for twenty seconds, then locks it again.
6. Flashes the onboard blue LED on GPIO 2 while the door is released.
7. Writes a compact operational status heartbeat to Firebase every five minutes while idle for the admin dashboard; opening and locking include status in the same asynchronous request.
8. Accepts a protected remote restart request from the administration panel, but only restarts after the lock is safely closed.

The ESP32 never downloads an order object. The website first saves the complete order, then writes only `true` at `commands/geladeira/<order-id>`. The ESP32 receives the order ID from the path and the boolean value, opens the lock, and deletes the command in the same multi-location PATCH that reports opening (or locking if the opening report was superseded while offline). Old commands are never opened again after a device restart.

The administration panel can also write `true` to `commands/geladeira/system/restart`. This branch is distinct from customer unlock commands and can only be created by an administrator through Firebase rules. The device schedules its deletion in an asynchronous status update, records `REMOTE_RESTART_REQUESTED`, and waits until no order is being processed and the relay is locked before restarting with the reason `REMOTO`. A restart command that existed before boot is ignored, preventing a stale Firebase value from causing an unexpected reboot.

## Production configuration

The following values are built into firmware 2.8.3. The browser and ESP32 both use six seconds before opening and a 20-second unlock period.

| Configuration | Value | Meaning |
| --- | --- | --- |
| `LED_INDICADOR` | GPIO 2 | Onboard blue LED, when present on the ESP32 board. |
| `RELE_TRAVA` | GPIO 26 | Relay input that controls the electromagnetic lock. |
| `RELE_TRAVADO` | `HIGH` | Safe relay state used for a locked door with a low-level-active relay. |
| `RELE_DESTRAVADO` | `LOW` | Relay state used to remove power from the fail-safe lock. |
| `TemporizadorTrava::ESPERA_MS` | 6,000 ms | Delay after accepting a command. |
| `TemporizadorTrava::ABERTURA_MS` | 20,000 ms | Door release duration. |
| `INTERVALO_HEARTBEAT_MS` | 300,000 ms | Normal Firebase status update interval. |
| `INTERVALO_REINICIO_PREVENTIVO_MS` | 5 hours | Safe preventive restart interval. |
| `WATCHDOG_TIMEOUT_MS` | 60 seconds | Main-loop watchdog timeout. |

## Required wiring

```text
GPIO 2   → onboard blue LED, where available
GPIO 26  → IN on the relay module
ESP32 GND → GND on the relay module
```

The relay input must accept a 3.3 V ESP32 signal. The firmware assumes a low-level-active relay:

```text
GPIO 26 HIGH → relay inactive → lock powered through NC → door locked
GPIO 26 LOW  → relay active   → NC opens → lock power removed → door released
```

Before connecting the real lock, verify the relay's COM/NC terminals with a multimeter. See the root [README](../README.md#relay-and-electromagnetic-lock-wiring) for the 12 V power diagram and electrical safety requirements.

## Local credentials file

Copy `secrets.example.h` to `secrets.h` and set the local Wi-Fi and Firebase device credentials. This file is intentionally excluded from Git and must never be committed.

For the production refrigerator, use:

```cpp
#define DEVICE_ID "geladeira"
#define MODO_TESTE false
```

Two boards may have this same production configuration, but only **one** may be powered and connected to the refrigerator at a time. Otherwise, both boards can receive the same order.

For a bench-only diagnostic board that must never activate a relay, use:

```cpp
#define DEVICE_ID "teste"
#define MODO_TESTE true
```

Test mode connects to Wi-Fi and Firebase and publishes diagnostics at `devices/teste`, but ignores orders and never drives the relay.

## Arduino IDE installation

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software/).
2. In Arduino IDE settings, add this Additional Boards Manager URL:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Install **esp32 by Espressif Systems** in Boards Manager.
4. Install **FirebaseClient** in Library Manager.
5. Open `esp32.ino`.
6. Create and complete the local `secrets.h` file as described above.
7. Select **ESP32 Dev Module** under **Tools → Board**.
8. Connect the ESP32 with a data-capable USB cable and select its serial port.
9. Click **Upload**. If the IDE remains at “Connecting…”, hold the **BOOT** button until writing begins.
10. Open Serial Monitor at **115200 baud**.

Expected successful startup output:

```text
Wi-Fi connected: <ip>
ESP32 geladeira preparado (PRODUCAO)
Firebase connected.
Sincronização inicial concluída; ESP32 pronto para uso.
Monitoramento de comandos ativado.
```

The serial messages remain in Portuguese because they are intended for the project operator; their meaning is documented in the root README.

## LED behaviour

| Event | Blue LED on GPIO 2 |
| --- | --- |
| Wi-Fi connected | Flashes three times |
| Firebase connected | Flashes five times |
| Lock released | Flashes during the 20-second opening period |
| Device ready / locked | Off |

The red LED on many ESP32 development boards is only a power LED and is not controlled by the firmware.

## Reliability and diagnostics

Firmware 2.8.3 uses several independent safeguards, including one persistent ESP32 task for both the relay timing and blue opening indicator and a minimal command-only Firebase stream:

- A 60-second task watchdog monitors the main firmware loop. Idle tasks are not subscribed by this configuration. The separate interrupt watchdog remains enabled; an interrupt watchdog reset requires the serial panic log to diagnose.
- Firebase callbacks only capture incoming commands and request recovery. Order dispatch, NVS writes, asynchronous send scheduling, and stream cancellation run after `firebase.loop()` returns, avoiding nested network operations inside result callbacks. Stream recovery waits for command synchronization before subscribing again.
- The relay task owns the six-second wait, twenty-second release, and LED blinking. It never calls Firebase, Wi-Fi, NVS or Serial. Two fixed-size events report opening and locking to the main loop. A stalled network loop does not extend the physical release period; this is not protection against power loss or a processor-wide failure.
- One asynchronous multi-location PATCH updates the order execution state, the device status, and command deletion together. In normal operation each opening cycle uses two writes instead of five (excluding idle heartbeats). The initial command synchronization explicitly uses a non-streaming GET; only the dedicated stream client uses SSE.
- At most one operational PATCH is in flight. A four-entry fixed queue holds confirmations; each is attempted at most three times with a five-second retry delay and a fifteen-second request deadline. A failed idle heartbeat waits for the next five-minute interval instead of retrying continuously. A closure supersedes an unsent opening for that order. A new cycle waits in the existing order queue if confirmation capacity is exhausted. Pending confirmations are in RAM and do not survive a reset.
- Idle status is sent every five minutes (80% fewer periodic writes). The matching admin panel uses fifteen minutes without a signal as the offline threshold for firmware 2.8.3 and later, while retaining three minutes for older firmware. Monitoring therefore detects idle failures less quickly. Memory diagnostics still run locally once a minute without extra network writes.
- Firebase stream health is checked every five seconds. A stream with no event or keep-alive for two minutes is rebuilt.
- The command stream contains only a boolean value and an order ID in the path. It does not parse order JSON, products, prices, or customer data.
- If Wi-Fi does not return for five minutes, the relay is returned to `RELE_TRAVADO` and the ESP32 restarts with `WIFI_SEM_RETORNO`.
- If Wi-Fi is connected but Firebase does not return for five minutes, the same safe restart occurs with `FIREBASE_SEM_RETORNO`.
- Boot never waits forever for Wi-Fi. The relay starts locked, then Wi-Fi and Firebase reconnection continue from the main loop so a failed connection cannot leave startup blocked indefinitely.
- Wi-Fi modem-sleep is disabled because the device is permanently powered. This avoids power-save receive latency for Firebase stream commands and keep-alives.
- A preventive restart is scheduled every five hours and only occurs when the lock is closed and no order is in progress.
- Before each major stage (Wi-Fi, Firebase, command stream, and order handling), the firmware stores a compact diagnostic marker. A later panic reports `FALHA_DO_PROGRAMA_<stage>` in the dashboard.
- Three consecutive program panics activate a temporary safe mode: the relay remains locked while the device restores Wi-Fi and Firebase, and new commands are held instead of being discarded. After two stable online minutes, it resumes normal operation and processes the held command.
- The device reports only online state, lock state, Wi-Fi, Firebase, command-stream state, uptime, firmware, and reset reason. This keeps the normal heartbeat small.
- Detailed events stay on the USB Serial Monitor during maintenance instead of being continuously persisted and uploaded.

The safe-reset reason is persisted locally and included in the first successful heartbeat after reboot. The admin dashboard can therefore show why a recovered device restarted.

The fragmentation protection continues to measure the largest free memory block internally. It schedules a safe restart after three periodic measurements below 10 KB and only performs it while the lock is closed with no order in progress.

## Troubleshooting

`WATCHDOG_INTERNO` is the ESP32 interrupt watchdog reset reason, distinct from the 60-second task watchdog configured here. `FALHA_DO_PROGRAMA_STREAM_ATIVO` means a panic occurred after the last persisted stage was `STREAM_ATIVO`; that stage is context, not a crash location or proof of a stream bug. These fields describe the previous reboot even while the recovered board is online.

Version 2.8.3 separates relay timing from the network loop, uses asynchronous combined confirmations, and retains the callback deferral introduced locally in 2.8.2. It does not establish the cause of a previously observed panic. To diagnose a recurrence, capture the USB Serial Monitor at 115200 baud from before the restart, including `Guru Meditation`, the exception/watchdog message and the complete `Backtrace`. Keep the matching firmware ELF from the compile output to decode addresses. Record whether the relay was switching at the time and the installed ESP32 core and FirebaseClient versions. Do not disable watchdogs to hide these resets.


| Observation | Likely check |
| --- | --- |
| No Wi-Fi flashes | Wi-Fi credentials, 2.4 GHz coverage, power, or antenna location |
| Wi-Fi works but Firebase does not flash | Firebase device credentials, Firebase rules, or internet access |
| No order monitoring message | Inspect Serial Monitor at 115200 baud and reboot after confirming credentials |
| New order does not release the door | Ensure the board is in production mode, wait until monitoring is active, then test a new order and relay wiring |
| Relay has inverse behaviour | Verify COM/NC and adjust `RELE_TRAVADO` / `RELE_DESTRAVADO` only after testing without the lock |
| Dashboard shows offline | Check device power, Wi-Fi, Firebase, and the last heartbeat. The last reported Firebase or stream state is not live once the heartbeat is stale. |

## Security reminders

- Never publish `secrets.h`.
- Never connect 12 V lock power to an ESP32 pin.
- Keep the lock supply fused and the low-voltage wiring inside an insulated enclosure.
- Keep Firebase rules restrictive; do not make the database globally writable.
- GitHub publication updates source code only. A physical ESP32 needs a USB upload until a separate authenticated OTA system is implemented.

## Validating this change

From the project root:

```sh
c++ -std=c++11 -Wall -Wextra -Werror tests/controle-trava.test.cpp -o /tmp/14bis-controle-trava-test
/tmp/14bis-controle-trava-test
python3 tests/firmware-envios.test.py
node --test tests/*.test.cjs
arduino-cli compile --fqbn esp32:esp32:esp32 esp32
```

The C++ tests cover timing, millis rollover, delayed networking and bounded confirmations. The Python runner compiles the actual firmware send functions against controlled network doubles, checking combined JSON payloads, cadence, timeouts, retries and remote restart acknowledgment. These host checks do not exercise the ESP32 radio, FreeRTOS scheduler or electrical wiring.

Before using the release on the refrigerator, validate on the bench with the production timings: make a new request, measure the relay transition after six seconds and its return after twenty seconds, disconnect Wi-Fi during release, and confirm that the relay still returns on time. Restore Wi-Fi and verify that a pending closed state reaches the order and administration panel. Verify a second waiting order and a remote restart during a cycle. Keep both `esp32.ino` and `ControleTrava.h` in the sketch folder when uploading over USB. The existing Firebase rules support these paths; no permission broadening is required.
