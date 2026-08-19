# Installation, build, and commissioning

This guide reproduces the tested configuration on macOS or Linux. All SDKs are
installed locally under `.tooling/` and are not committed to Git.

## Requirements

- ESP32-S3 with at least 4 MB of flash;
- USB data cable;
- Python 3, Git, CMake, and Ninja;
- macOS or Linux;
- 2.4 GHz Wi-Fi access point;
- Matter hub, such as a Yandex Station Max.

On macOS, install Command Line Tools:

```sh
xcode-select --install
```

The required Linux packages are listed in the official
[ESP-IDF guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/linux-macos-setup.html).

## 1. ESP-IDF

Run these commands from the repository root:

```sh
mkdir -p .tooling
git clone --recursive --branch v5.5.4 \
  https://github.com/espressif/esp-idf.git .tooling/esp-idf
export IDF_TOOLS_PATH="$PWD/.tooling/espressif-tools"
.tooling/esp-idf/install.sh esp32s3
source .tooling/esp-idf/export.sh
```

## 2. ESP-Matter

The pinned commit reproduces the tested build:

```sh
git clone https://github.com/espressif/esp-matter.git .tooling/esp-matter
git -C .tooling/esp-matter checkout 177452a85301875f8f5a5865b8836f39d0953092
git -C .tooling/esp-matter submodule update --init --depth 1
```

Fetch only the required connectedhomeip submodules. On macOS:

```sh
cd .tooling/esp-matter/connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 darwin --shallow
cd ../../../..
```

On Linux, replace `darwin` with `linux`:

```sh
cd .tooling/esp-matter/connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 linux --shallow
cd ../../../..
```

Install the ESP-Matter Python dependencies without building host tools:

```sh
source .tooling/esp-idf/export.sh
cd .tooling/esp-matter
./install.sh --no-host-tool
cd ../..
```

The `idf.sh` script activates these local environments automatically before
each invocation.

## 3. Find the USB port

On macOS:

```sh
ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null
```

On Linux:

```sh
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

The examples below use `/dev/cu.YOUR_PORT` as a placeholder. On Linux, the user
must have access to the serial port, usually through the `dialout` group.

## 4. Capture the original remote ID

If the protocol `variant`, controller ID, and subgroup are unknown:

```sh
./idf.sh -C sniffer set-target esp32s3
./idf.sh -C sniffer build
./idf.sh -C sniffer -p /dev/cu.YOUR_PORT flash monitor
```

Press power, color, or another button on the original remote. Expected output:

```text
FOUND variant=v1a id=0x5018 index=1 wire-group=0x5118 command=on(0x10) param=0 args=[0,0,0]
```

Exit the ESP-IDF monitor with `Ctrl+]`.

## 5. Test control without Matter

Before integrating Matter, verify that the captured group actually controls the
light:

```sh
./idf.sh set-target esp32s3
./idf.sh build
./idf.sh -p /dev/cu.YOUR_PORT flash monitor
```

The following commands are available at the `lamp>` prompt:

```text
status
variant v1a
group 0x5018
index 1
on
off
level 255 0
level 0 255
rgb 255 0 0
```

Values entered in this test firmware are not copied automatically into the
Matter project.

## 6. Configure and flash the Matter bridge

Open the configuration menu:

```sh
./idf.sh -C matter menuconfig
```

Under `LampSmart bridge`, enter the values reported by the sniffer:

- `LampSmart controller ID` — the `id` field;
- `LampSmart subgroup index` — the `index` field.

Build and flash:

```sh
./idf.sh -C matter build
./idf.sh -C matter -p /dev/cu.YOUR_PORT flash monitor
```

The first ESP-Matter build may take several minutes. The resulting binary is
written to `matter/build/lampsmart_matter_bridge.bin`.

## 7. Add the device to Alice

Run this command in the ESP32 console:

```text
matter onboardingcodes ble
```

The firmware prints a `QRCodeUrl` and an 11-digit `ManualPairingCode`.

1. Connect the phone to the main 2.4 GHz Wi-Fi network.
2. Open the Yandex Home with Alice app.
3. Tap `+` → `Smart home device` → `Find Matter devices`.
4. Scan the QR code or enter the manual code.
5. Select the Yandex Station as the Matter hub.
6. Assign a name and room to the light.

Keep the ESP32, phone, and Station near one another during commissioning. Once
BLE commissioning is complete, Matter control runs over the local Wi-Fi
network.

## 8. Verify state restoration

After successfully controlling the light, power the ESP32 off for a few seconds
and turn it back on. The log should show a connection to the saved Wi-Fi network
and the restored state being passed to `lamp_bridge`. You do not need to scan
the QR code again.

Do not run `matter esp factoryreset` for a normal restart: it removes the Matter
fabric and Wi-Fi credentials.
