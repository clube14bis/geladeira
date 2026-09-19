# Clube 14 BIS Fridge

<img width="1280" height="853" alt="Clube 14 BIS Fridge" src="https://github.com/user-attachments/assets/02b59fa2-7ec7-47b4-98f5-437ef00a6a8e" />

Clube 14 BIS Fridge is a self-service system for a shared refrigerator. Members open the web app from a QR code, sign in, choose their drinks, confirm the cart, and collect the items after the refrigerator door is released. Orders are stored in Firebase and Google Sheets, while an ESP32 drives the relay connected to the electromagnetic lock.

## Architecture

| Component | Responsibility |
| --- | --- |
| GitHub Pages | Hosts the public ordering site and the administration panel. |
| Firebase Authentication | Account creation, sign-in, session handling, and password-reset email. |
| Firebase Realtime Database | Users, catalog, orders, order history, ESP32 status, and access control. |
| Cloudflare Worker | Lets users sign in with a username by resolving the matching Firebase email address. |
| Google Apps Script + Google Sheets | Creates the operational withdrawal report. |
| ESP32 | Listens for new orders and controls the lock relay. |

## Main features

### Customer site

- Username and password login, including a show/hide password control.
- Registration with full name, username, phone number, Brazilian CPF, email, and password.
- Email-based password reset through Firebase Authentication.
- Product catalog grouped by category, with image, price, stock indicator, and quantity controls.
- Floating cart summary, full cart page, quantity changes, removal, Brazilian Real total, and Pix copy-and-paste payment data.
- Door-progress screen: six-second preparation period, ten-second unlock period, and a closing notice.
- Personal three-month calendar history with daily consumption and totals.
- Published Google Sheets usage report.

### Administration panel

- Restricted to Firebase users marked as administrators.
- Create, edit, reorder, show/hide, and remove products.
- Create categories and remove empty categories.
- Edit name, price, quantity, image URL, category, and visibility before saving changes.
- Informational inventory control: stock decreases on an order but intentionally does not block selection at zero.
- Live ESP32 dashboard with online state, lock state, Wi-Fi quality, Firebase and stream state, uptime, firmware version, reset reason, and memory diagnostics.

## Order flow

1. A customer signs in, selects items, and confirms the cart.
2. The web app creates a pending order, saves personal history, and updates informational stock in Firebase.
3. The ESP32 receives the new Firebase Realtime Database stream event.
4. The Google Sheets report is sent in the background and never delays the lock command.
5. It waits six seconds, then releases the relay for ten seconds while the onboard blue LED flashes.
6. The relay interrupts power to the fail-safe electromagnetic lock, allowing the door to open.
7. After ten seconds, the relay returns to the locked state and the ESP32 records the lock state in Firebase.

## ESP32 reliability design

Firmware **2.5.4** is designed for unattended operation. It does not repeatedly poll the whole order history. Instead, it keeps a Firebase stream open and only checks the most recent order when it starts or rebuilds a stream. This prevents a large historical response from exhausting the ESP32 heap. It processes small root-level Firebase patches created by the website's atomic order update while still ignoring the large startup snapshot. The opening LED is driven by its own ESP32 task, so a delayed network operation cannot hide the visual opening signal.

| Protection | Behaviour |
| --- | --- |
| Task watchdog | Restarts the main firmware loop after a complete 60-second stall. Wi-Fi and Firebase background tasks are excluded to prevent false resets during TLS/network activity. |
| Stream health check | Checks the Firebase stream every five seconds and rebuilds it if no event or keep-alive arrives for two minutes. |
| Wi-Fi recovery | Tries the configured 2.4 GHz networks again after a disconnect. |
| Wi-Fi fallback reset | If Wi-Fi cannot return for five minutes, locks the relay output and restarts with `WIFI_SEM_RETORNO`. |
| Firebase fallback reset | If Wi-Fi is connected but Firebase does not return for five minutes, locks the relay output and restarts with `FIREBASE_SEM_RETORNO`. |
| Preventive reset | Every five hours, restarts only when the lock is closed and there is no order being processed. |
| Memory telemetry | Sends free heap, minimum heap, largest free block, stream-recovery memory, RSSI, and uptime to the dashboard every 30 seconds. |
| Persistent event log | Keeps a circular history in ESP32 non-volatile memory and sends it to the dashboard after the next successful Firebase connection. |

The dashboard considers the device offline when the latest heartbeat is older than 90 seconds. Values such as “Firebase connected” and “stream active” are the last status reported by the device; when the device is offline, they are historical values rather than live confirmation.

The persistent event log uses records such as `B22/U18s:WIFI_CONNECTED`: `B` is the boot number, `U` is the elapsed time since that boot, and the final value is the event. It records device boot reason, Wi-Fi/Firebase changes, stream recovery, order processing, lock release/lock state, and planned reset reason. The log survives a normal ESP32 reset and helps identify what happened immediately before a recovery.

### Memory diagnostics

Free heap alone is not enough to assess memory health. The dashboard also reports the **largest free block**, which is the largest single allocation currently possible. For example, 80 KB total free heap with a 20 KB largest block cannot allocate a new 40 KB buffer. The firmware avoids the old full-history read specifically for this reason.

Normal values can change after boot while Wi-Fi, TLS, and Firebase allocate their buffers. Watch the trend over 24–48 hours:

- Stable values or small variations are expected.
- A continuing drop in free heap or largest free block can indicate fragmentation or a memory leak.
- The reset reason after a recovery helps identify a Wi-Fi, Firebase, watchdog, power, or planned-reset event.

## Project links and files

| Item | Location |
| --- | --- |
| Public site | [clube14bis.github.io/geladeira](https://clube14bis.github.io/geladeira/) |
| Admin panel | [admin.html](https://clube14bis.github.io/geladeira/admin.html) |
| Repository | [github.com/clube14bis/geladeira](https://github.com/clube14bis/geladeira) |
| ESP32 firmware | [esp32/esp32.ino](esp32/esp32.ino) |
| Credentials template | [esp32/secrets.example.h](esp32/secrets.example.h) |
| Realtime Database rules | [firebase-rules.json](firebase-rules.json) |
| Google Apps Script | [google-apps-script/Code.gs](google-apps-script/Code.gs) |

## Using the customer site

1. Scan the QR code and open the site.
2. Sign in with username and password, or choose **Create account**.
3. For a new account, provide the requested identification and contact fields.
4. Add products. The floating **View cart** control appears after the first item is selected.
5. Review quantities and total in the cart, then confirm the order.
6. Copy the Pix payment information when needed.
7. Wait for the opening screen, collect the items, and close the refrigerator door.
8. Use **History** to review orders from the last three months.

Password recovery uses the registered email address. A CPF alone is not used as a password-recovery factor because it is not a secure account-verification method.

## Using the administration panel

Open [admin.html](https://clube14bis.github.io/geladeira/admin.html) and sign in with an account that is listed as an administrator in Firebase.

### Catalog management

1. Set **Show** to make a product visible in the public catalog.
2. Choose its category, enter a price in BRL, and set a non-negative integer stock amount.
3. Use the arrows to set product order.
4. Add a public direct image URL for each product.
5. Save changes to commit all edits to Firebase.

Product images are displayed inside a square frame while preserving their aspect ratio. Categories can only be deleted after their products have been moved or removed.

### Inventory policy

Order confirmation uses Firebase transactions to avoid negative stock values. Stock is informational by design: a zero-stock product remains selectable and can still be ordered. This is intentional for this project and should be changed only if strict stock blocking becomes necessary.

### ESP32 dashboard interpretation

- **Online** means the most recent heartbeat is less than 90 seconds old.
- **Lock state** is red when locked and green when open.
- **Wi-Fi** includes the network name, RSSI in dBm, and a quality label.
- **Stream recoveries** records automatic stream rebuilds since the last device boot.
- **Memory since ready** compares memory immediately after the Firebase stream becomes operational with the lowest later value.
- **Largest free block** is more useful than total heap when checking allocation headroom.
- **Last initialization** shows why the currently running firmware started.

## Firebase and Cloudflare setup

### Firebase initial setup

1. Create or open the `geladeira-14-bis` Firebase project.
2. In **Authentication**, enable the Email/Password provider.
3. Configure the password-reset email template as needed.
4. Create a Realtime Database.
5. Publish [firebase-rules.json](firebase-rules.json) in Realtime Database Rules.
6. In **Project settings → General**, create a Web App and copy its public configuration to `firebase-config.js`.

### Database structure

```text
users/<uid>                 customer profile
usernames/<username>        username-to-email lookup index
catalog/<id>                product data, image, price, stock, visibility
catalogConfig/categoryOrder category ordering
orders/<id>                 pending order consumed by ESP32
userOrders/<uid>/<id>       personal order history
admins/<uid>                admin permission
devices/geladeira           ESP32 heartbeat and diagnostic status
```

Firebase Authentication uses email addresses internally. The Cloudflare Worker maps the username entered by the user to its Firebase email address. Never publish worker tokens, Firebase device credentials, Wi-Fi passwords, or administrator secrets.

Do not replace the database rules with globally open read/write rules.

## Google Sheets and Apps Script

### Sheet layout

| Row | Content |
| --- | --- |
| 1 | `Geladeira 14 BIS` title |
| 2 | Date, time, customer, item, and value headers |
| 3 | Most recent order |
| 4 onward | Older orders |

Each new order is inserted on row 3. When a template row exists below it, the script copies that row’s formatting so manually chosen colors, fonts, widths, and alignment are preserved.

### Apps Script setup

1. In the spreadsheet, open **Extensions → Apps Script**.
2. Copy [google-apps-script/Code.gs](google-apps-script/Code.gs).
3. In Script Properties, configure `SPREADSHEET_ID` and `FIREBASE_API_KEY`. `DEVICE_SECRET` is only needed for direct device requests.
4. Run `autorizarIntegracaoFirebase` once and approve the requested permissions.
5. Run `configurarPlanilha` only when creating or rebuilding the initial worksheet structure.
6. Deploy as a Web App running as the owner.
7. Copy the resulting `/exec` URL into `sheetsEndpoint` in `firebase-config.js`.

The Apps Script validates the Firebase token, prevents formula injection in the sheet, and writes monetary values in BRL.

## Installing the ESP32 firmware

See [esp32/README.md](esp32/README.md) for the focused firmware guide. The summary below is enough for a normal USB update.

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software/).
2. Add this Boards Manager URL in Arduino IDE settings:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Install **esp32 by Espressif Systems** in Boards Manager.
4. Install **FirebaseClient** and **ArduinoJson** in Library Manager.
5. Open [esp32/esp32.ino](esp32/esp32.ino).
6. Copy `esp32/secrets.example.h` to local `esp32/secrets.h` and fill in Wi-Fi and Firebase device credentials.
7. Select **Tools → Board → ESP32 Arduino → ESP32 Dev Module**.
8. Connect a data-capable USB cable, select the serial port, and choose **Upload**.
9. If the IDE stays at “Connecting…”, hold the **BOOT** button until upload starts.
10. Open Serial Monitor at **115200 baud**.

Expected startup messages include:

```text
Wi-Fi connected: <ip>
ESP32 geladeira preparado (PRODUCAO)
Firebase connected.
Monitoramento de pedidos ativado.
```

The red LED on many ESP32 boards is only a power indicator. The firmware uses the programmable onboard blue LED on GPIO 2 when that LED is present.

### Two identical ESP32 boards

Both boards may use the same production configuration:

```cpp
#define DEVICE_ID "geladeira"
#define MODO_TESTE false
```

Only one board must be powered and connected to the refrigerator system at a time. Two active boards using the same device ID would both receive the same order and could both operate a relay.

## Relay and electromagnetic lock wiring

> **Electrical safety:** never connect 127/220 V mains power to an ESP32. Use a certified DC power supply, disconnect power before changing wires, and ask a qualified electrician for help if necessary.

### Required parts

| Part | Recommended specification | Purpose |
| --- | --- | --- |
| ESP32 | ESP32 Dev Module or DevKit | Wi-Fi and control logic |
| Relay module | One channel, 5 V coil, 3.3 V-compatible input, COM/NC/NO terminals | Switches lock power |
| Lock | 12 V DC fail-safe electromagnetic lock | Locks while powered |
| Lock supply | Certified 12 V DC source sized for the lock current | Powers the lock |
| Buck converter | LM2596 or equivalent, 12 V to 5 V, at least 2 A | Powers the ESP32 and relay |
| Fuse | Suitable holder and fuse for the lock current | Protects the 12 V branch |
| Enclosure and terminals | Insulated box, strain relief, terminals, 0.5–0.75 mm² cable | Safe installation |

### Low-voltage control wiring

```text
ESP32 GPIO 26  ─── IN on relay module
ESP32 GND      ─── GND on relay module
Buck 5 V OUT+  ─── VCC on relay module

12 V supply +  ─── Buck IN+
12 V supply -  ─── Buck IN-
Buck 5 V OUT+  ─── ESP32 5V/VIN
Buck GND OUT-  ─── ESP32 GND
```

ESP32 GND and relay-module GND must be common for a standard relay input. Choose a relay module whose input accepts a 3.3 V signal; some 5 V relay modules need a level shifter or transistor.

### Lock power wiring

```text
12 V supply + ── fuse ── COM on relay
                             │
                             └── NC on relay ─── lock positive terminal

12 V supply - ─────────────────────────────────── lock negative terminal
```

Leave **NO** unused in this fail-safe wiring. GPIO 26 connects only to the relay input; 12 V must never reach ESP32 GPIO, 3.3 V, 5 V, or GND pins.

The firmware assumes a low-level-active relay:

| Condition | GPIO 26 | Relay | Fail-safe lock |
| --- | ---: | --- | --- |
| Normal / locked | HIGH | Inactive | Receives 12 V through NC |
| Opening | LOW | Active | NC opens; 12 V is removed; door releases |
| After ten seconds | HIGH | Inactive | NC closes; lock returns |

Before connecting the lock, verify COM/NC with a multimeter. If the relay logic is inverted, adjust `RELE_TRAVADO` and `RELE_DESTRAVADO` in [esp32/esp32.ino](esp32/esp32.ino).

### Safe installation order

1. Flash the firmware and test the ESP32 alone: three Wi-Fi flashes and five Firebase flashes.
2. Connect the buck converter and relay, but not the lock.
3. Create a new order and confirm the relay changes state for ten seconds.
4. Verify COM/NC continuity with a multimeter.
5. Power off, add the fused 12 V supply and lock as shown above.
6. Test first with the door open.
7. Put the supply, buck, relay, and terminals in an insulated enclosure away from moisture and moving parts.

Never power the lock from computer USB, leave exposed wiring inside the refrigerator, omit the fuse, or operate without an emergency mechanical opening method.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No three blue flashes | Wi-Fi credentials, 2.4 GHz availability, and power |
| Three flashes but not five | Firebase device credentials and Realtime Database rules |
| No “Monitoramento de pedidos ativado” serial message | Restart and inspect Serial Monitor at 115200 baud |
| Site confirms but the LED does not flash | Place a new order only after the ESP32 is ready; check dashboard and serial output |
| Relay appears inverted | Verify COM/NC and adjust relay logic constants |
| Door does not release | Measure the lock’s 12 V during the ten-second window |
| Order missing from Sheets | Check Apps Script deployment, `/exec` endpoint, script properties, and authorization |
| Catalog changes do not save | Check admin permission and Firebase rules |
| Dashboard offline | Confirm power, Wi-Fi, Firebase, and the latest heartbeat age. A stale connected state is not a live confirmation. |

## Deployment and security

The website is deployed from the `main` branch through GitHub Pages. Website changes need a commit and a short deployment delay. ESP32 changes require a USB upload; publishing to GitHub does **not** update a physical device automatically.

For updates on another computer, clone or download the updated repository, preserve the local `esp32/secrets.h` file with its configured credentials, and follow the Arduino IDE steps above. Secure OTA updates can be added later, but an unauthenticated internet-exposed OTA endpoint must never be used.

Never commit `esp32/secrets.h`, Wi-Fi passwords, customer passwords, Cloudflare tokens, Firebase device credentials, or Apps Script secrets. Keep Firebase rules restrictive and back up important data before deletion.
