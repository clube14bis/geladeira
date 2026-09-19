# ESP32 firmware — Clube 14 BIS Fridge

This folder contains the ESP32 firmware that receives new orders from Firebase Realtime Database and controls the refrigerator electromagnetic lock through a relay.

Current firmware version: **2.5.4**.

## What the firmware does

1. Connects to the first configured 2.4 GHz Wi-Fi network that is available.
2. Authenticates with Firebase using the device account.
3. Queries only the most recent order to establish a safe starting point.
4. Opens a Firebase Server-Sent Events stream for new orders.
5. On a new pending order, waits six seconds, releases the lock for ten seconds, then locks it again.
6. Flashes the onboard blue LED on GPIO 2 while the door is released.
7. Writes a status heartbeat to Firebase every 30 seconds for the admin dashboard.

Old orders are never opened again after a device restart. Always create a new order only after the device reports that its order monitoring is active.

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
4. Install **FirebaseClient** and **ArduinoJson** in Library Manager.
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
Monitoramento de pedidos ativado.
```

The serial messages remain in Portuguese because they are intended for the project operator; their meaning is documented in the root README.

## LED behaviour

| Event | Blue LED on GPIO 2 |
| --- | --- |
| Wi-Fi connected | Flashes three times |
| Firebase connected | Flashes five times |
| Lock released | Flashes for the ten-second opening period |
| Device ready / locked | Off |

The red LED on many ESP32 development boards is only a power LED and is not controlled by the firmware.

## Reliability and diagnostics

Firmware 2.5.4 uses several independent safeguards, including a dedicated ESP32 task for the blue opening indicator and support for atomic root-level order patches:

- A 60-second watchdog monitors only the main firmware loop. Wi-Fi and Firebase background tasks are excluded so normal TLS/network activity cannot cause a false reset.
- Firebase stream health is checked every five seconds. A stream with no event or keep-alive for two minutes is rebuilt.
- Stream startup and recovery query only the latest order instead of downloading the complete historical `orders` node. This reduces heap pressure as the database grows.
- If Wi-Fi does not return for five minutes, the relay is returned to `RELE_TRAVADO` and the ESP32 restarts with `WIFI_SEM_RETORNO`.
- If Wi-Fi is connected but Firebase does not return for five minutes, the same safe restart occurs with `FIREBASE_SEM_RETORNO`.
- A preventive restart is scheduled every five hours and only occurs when the lock is closed and no order is in progress.
- The device reports free heap, minimum heap, largest available allocation block, stream recovery memory, RSSI, uptime, reset reason, and stream recovery count.
- A circular persistent event log records boot reason, Wi-Fi/Firebase transitions, stream recovery, lock actions, and planned reset reason. It is retained in internal non-volatile memory and displayed in the dashboard after the next successful heartbeat.

The safe-reset reason is persisted locally and included in the first successful heartbeat after reboot. The admin dashboard can therefore show why a recovered device restarted.

Event records follow `B<boot>/U<seconds>s:<event>`. For example, `B22/U301s:RESET_WIFI_SEM_RETORNO` means that boot 22 safely restarted after Wi-Fi had not returned. A power loss or total hardware freeze may prevent the final event from being written, but the reset reason on the next boot can still provide evidence.

## Reading memory values

`freeHeap` is the total free memory. `largestFreeBlock` is the maximum amount that can be allocated in a single request. A healthy board may have a smaller largest block after TLS and Firebase connections are established; what matters most is whether the values continue to fall over time.

Monitor the board for at least 24–48 hours after a firmware change. A stable range is normal. A steady decline in free heap or largest free block is evidence that should be investigated before adding more functionality.

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
