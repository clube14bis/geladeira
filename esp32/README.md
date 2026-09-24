# ESP32 firmware — Clube 14 BIS Fridge

This folder contains the ESP32 firmware that receives new orders from Firebase Realtime Database and controls the refrigerator electromagnetic lock through a relay.

Current firmware version: **2.7.1**.

## What the firmware does

1. Connects to the first configured 2.4 GHz Wi-Fi network that is available.
2. Authenticates with Firebase using the device account.
3. Establishes a safe starting point and ignores commands that existed before boot.
4. Opens a Firebase Server-Sent Events stream only for minimal unlock commands.
5. On a new Boolean unlock command, waits six seconds, releases the lock for twenty seconds, then locks it again.
6. Flashes the onboard blue LED on GPIO 2 while the door is released.
7. Writes a compact operational status heartbeat to Firebase every 60 seconds for the admin dashboard.
8. Accepts a protected remote restart request from the administration panel, but only restarts after the lock is safely closed.

The ESP32 never downloads an order object. The website first saves the complete order, then writes only `true` at `commands/geladeira/<order-id>`. The ESP32 receives the order ID from the path and the boolean value, opens the lock, and deletes the command after accepting it. Old commands are never opened again after a device restart.

The administration panel can also write `true` to `commands/geladeira/system/restart`. This branch is distinct from customer unlock commands and can only be created by an administrator through Firebase rules. The device deletes it when received, records `REMOTE_RESTART_REQUESTED`, and waits until no order is being processed and the relay is locked before restarting with the reason `REMOTO`. A restart command that existed before boot is ignored, preventing a stale Firebase value from causing an unexpected reboot.

## Production configuration

The following values are built into firmware 2.7.1. The browser and ESP32 both use six seconds before opening and a 20-second unlock period.

| Configuration | Value | Meaning |
| --- | --- | --- |
| `LED_INDICADOR` | GPIO 2 | Onboard blue LED, when present on the ESP32 board. |
| `RELE_TRAVA` | GPIO 26 | Relay input that controls the electromagnetic lock. |
| `RELE_TRAVADO` | `HIGH` | Safe relay state used for a locked door with a low-level-active relay. |
| `RELE_DESTRAVADO` | `LOW` | Relay state used to remove power from the fail-safe lock. |
| `ESPERA_ANTES_DE_ABRIR_MS` | 6,000 ms | Delay after accepting a command. |
| `TEMPO_DESTRAVADO_MS` | 20,000 ms | Door release duration. |
| `INTERVALO_HEARTBEAT_MS` | 60,000 ms | Normal Firebase status update interval. |
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

Firmware 2.7.0 uses several independent safeguards, including a dedicated ESP32 task for the blue opening indicator and a minimal command-only Firebase stream:

- A 60-second watchdog monitors only the main firmware loop. Wi-Fi and Firebase background tasks are excluded so normal TLS/network activity cannot cause a false reset.
- Firebase stream health is checked every five seconds. A stream with no event or keep-alive for two minutes is rebuilt.
- The command stream contains only a boolean value and an order ID in the path. It does not parse order JSON, products, prices, or customer data.
- If Wi-Fi does not return for five minutes, the relay is returned to `RELE_TRAVADO` and the ESP32 restarts with `WIFI_SEM_RETORNO`.
- If Wi-Fi is connected but Firebase does not return for five minutes, the same safe restart occurs with `FIREBASE_SEM_RETORNO`.
- A preventive restart is scheduled every five hours and only occurs when the lock is closed and no order is in progress.
- The device reports only online state, lock state, Wi-Fi, Firebase, command-stream state, uptime, firmware, and reset reason. This keeps the normal heartbeat small.
- Detailed events stay on the USB Serial Monitor during maintenance instead of being continuously persisted and uploaded.

The safe-reset reason is persisted locally and included in the first successful heartbeat after reboot. The admin dashboard can therefore show why a recovered device restarted.

The fragmentation protection continues to measure the largest free memory block internally. It schedules a safe restart after three periodic measurements below 10 KB and only performs it while the lock is closed with no order in progress.

## Troubleshooting

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
