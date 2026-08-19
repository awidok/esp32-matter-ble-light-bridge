# LampSmart protocol

This document describes the observed LampSmart `v1a` light protocol. It was
obtained by passively capturing the original remote and verified by transmitting
commands from an ESP32-S3. Other models may use `v1b`, `v2`, `v3`, or different
commands.

## Transport

The remote does not establish a BLE connection to the light. A command is sent
as a non-connectable BLE advertising packet and repeated several times. This
allows the original remote, LampSmart Pro, and the ESP32 to control the same
light without a central Bluetooth server.

The light sends no acknowledgement or telemetry. The transmitter knows only
which command it sent, not the light's actual state.

## Captured addressing

```text
variant:       v1a
controller ID: 0x5018
subgroup:      1
wire group:    0x5118
```

In `v1a`, the on-air group is constructed as follows:

```text
wire_group = (controller_id & 0xF0FF) | ((subgroup & 0x0F) << 8)
```

The controller ID and subgroup must therefore be stored separately. Using only
`0x5118` as the controller ID is incorrect because the subgroup would be
applied twice.

## Confirmed commands

| Command | Parameter | Arguments | Purpose |
| --- | --- | --- | --- |
| `0x10` | `0x00` | — | main power on |
| `0x11` | `0x00` | — | main power off |
| `0x12` | `0x00` | — | secondary on, model-dependent |
| `0x13` | `0x00` | — | secondary off, model-dependent |
| `0x1E` | `0x00` | — | enable the built-in RGB effect |
| `0x1F` | `0x00` | — | disable the built-in RGB effect |
| `0x21` | `0x00` | `cold, warm` | absolute CCT channel levels |
| `0x21` | `0x14` | — | brighter |
| `0x21` | `0x28` | — | dimmer |
| `0x21` | `0x18` | — | warmer |
| `0x21` | `0x24` | — | cooler |
| `0x22` | `0x00` | `red, green, blue` | absolute RGB value |
| `0x23` | `0x00` | — | night mode |
| `0x28` | variant-dependent | — | pairing |

On the tested light, RGB and white CCT channels are mutually exclusive: `0x22`
switches the light to RGB and turns white off, while `0x21` returns it to white
mode.

The Matter bridge treats white brightness as total output power. For example, a
neutral white level of 200 is divided between the cold and warm channels instead
of sending 200 to each channel. This prevents brightness clipping in the middle
of the color-temperature range.

## v1a packet format

The complete advertising payload is 31 bytes long:

| Offset | Length | Contents before bit reversal/whitening |
| ---: | ---: | --- |
| `0` | 7 | advertising header `02 01 02 1B 03 77 F8` |
| `7` | 8 | protocol header `AA 98 43 AF 0B 46 46 46` |
| `15` | 1 | command |
| `16` | 2 | wire group, little-endian |
| `18` | 3 | `arg0`, `arg1`, `arg2` |
| `21` | 1 | non-zero rolling TX counter |
| `22` | 1 | parameter |
| `23` | 4 | random seed fields |
| `27` | 2 | inner CRC16 |
| `29` | 2 | outer CRC16 |

CRC-16 with polynomial `0x1021` is used. After the service fields are filled,
the bits in each protocol byte are reversed and whitening is performed by an
LFSR with seed `83`. The exact implementation is in
[`matter/main/lampsmart_nimble.cpp`](../matter/main/lampsmart_nimble.cpp).

## Reproducing the reverse engineering

1. Flash `sniffer/` by following the [installation guide](INSTALL.md).
2. Do not re-pair the light; press a button on the already working remote.
3. Record `variant`, `id`, `index`, `command`, `param`, and all three `args`.
4. Capture power on, power off, cold, warm, several brightness levels, and pure
   RGB colors separately.
5. Verify each hypothesis with the test firmware in the repository root.
6. Use pairing only when the existing ID cannot be captured.

Example decoded line:

```text
FOUND variant=v1a id=0x5018 index=1 wire-group=0x5118 command=rgb(0x22) param=0 args=[255,0,0] tx=37
```

## Pairing and risks

Pairing changes the controller ID accepted by the light and may unpair the
existing remote or app. For an already working setup, passively capturing the
current ID is safer.

If pairing is still required:

1. Turn the light off at the wall switch.
2. Turn power back on.
3. Send `pair-all` from the test firmware within approximately five seconds.
4. Wait for the light to blink and test `v3`, `v2`, `v1a`, and `v1b`.

The current Matter bridge cannot transmit `v1b`, `v2`, or `v3`. If one of these
variants is detected, the encoder must be extended before Matter integration.
