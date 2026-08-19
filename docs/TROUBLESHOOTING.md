# Troubleshooting

## Alice reports that no devices were found

Start with the ESP32 serial log. The app message does not always reveal the
stage at which commissioning failed.

### No BLE connection appears in the log

The following line never appears:

```text
BLE GAP connection established
```

Check that:

- Bluetooth and the Nearby devices permission are enabled on the phone;
- you are using the current QR code for this specific ESP32;
- the ESP32 and Station are nearby, preferably within 5 m;
- the ESP32 is in commissioning mode;
- another phone is not connected to Matter BLE at the same time.

Print the code again with:

```text
matter onboardingcodes ble
```

### Commissioning Complete followed by RemoveFabric

If the ESP32 reaches `Commissioning Complete` but the controller immediately
sends `RemoveFabric`, check the metadata required for practical compatibility:

```text
matter esp attribute get 0 0x28 0x0f
matter esp attribute get 1 0x300 0xfffc
matter esp attribute get 1 0x300 0x400a
```

The current firmware should return:

- a non-empty `BasicInformation.SerialNumber` derived from the ESP32 MAC;
- `ColorControl.FeatureMap = 25`;
- `ColorControl.ColorCapabilities = 25`.

The value `25` (`0x19`) means Hue/Saturation + XY + Color Temperature. An older
firmware version without a serial number consistently completed Wi-Fi transfer
but was then removed by the Yandex controller.

## The ESP32 connected to Wi-Fi but was not added

Matter stores Wi-Fi separately from the fabric. After a failed attempt, the
ESP32 may remain on the network and continue commissioning over BLE/DNS-SD;
this is normal.

Make sure the phone and Station are on the same main network, Wi-Fi client
isolation is disabled, and multicast/mDNS is not filtered. During commissioning,
the phone must use a 2.4 GHz network.

## The device is already commissioned

First remove it from Yandex Home with Alice. Then, only if another commissioning
attempt does not start, run this command in the ESP32 console:

```text
matter esp factoryreset
```

This is destructive: it removes Matter fabrics, Wi-Fi credentials, and the
saved state. The device must be commissioned again after restarting.

## The Matter device card works but the light does not respond

Check the `Matter -> LampSmart` and `TX command` log lines:

- `NimBLE advertiser is busy` — BLE is still occupied by commissioning; the
  bridge retries the command automatically;
- the command is transmitted without errors but the light does nothing — the
  controller ID, subgroup, or protocol variant is almost always wrong;
- white works but RGB does not — verify the `0x22` capture and `[R,G,B]` order;
- cold and warm are swapped — change the channel interpretation in the bridge
  or test this particular model with the USB test firmware.

## State does not match after a restart

The Matter state and Wi-Fi credentials are stored in NVS and reapplied at
startup. However, the original remote transmits commands directly to the light,
and the ESP32 does not listen for them while operating as a bridge. Changes made
with the remote therefore do not update Matter.

Expected behavior:

1. the remote changes the physical light;
2. the Alice device card may show the previous state;
3. the next Alice command or ESP32 restart applies the saved Matter state.

## The build cannot find ESP-Matter

If CMake reports `ESP_MATTER_PATH is not set`, check that these paths exist:

```text
.tooling/esp-idf/export.sh
.tooling/esp-matter/export.sh
.tooling/esp-matter/connectedhomeip/connectedhomeip
```

Then repeat the steps in [INSTALL.md](INSTALL.md). Do not mix incompatible
ESP-IDF and ESP-Matter branches; the tested version pair is listed in README.

## The USB monitor does not open

- Close any other serial monitor using the port.
- On macOS, use `/dev/cu.*` rather than `/dev/tty.*`.
- On Linux, check access to `/dev/ttyACM*` or `/dev/ttyUSB*`.
- If the port changed after a reset, locate it again.

Exit the ESP-IDF monitor with `Ctrl+]`.
