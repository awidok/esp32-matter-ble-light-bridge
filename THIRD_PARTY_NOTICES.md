# Third-party software notices

The project itself is licensed under the MIT License in [`LICENSE`](LICENSE).
Third-party software keeps its original license. This file records the exact
top-level dependencies used by the checked-in lock files; it does not relicense
any third-party work.

## Source incorporated into this repository

### QB4-dev/esp-lampsmart-ble 1.0.3

- Source: <https://github.com/QB4-dev/esp-lampsmart-ble>
- Registry package: `qb4-dev/esp-lampsmart-ble` version `1.0.3`
- License: MIT, as declared by the upstream `idf_component.yml`
- Upstream commit recorded by the package: `b4c3f6cada616bc764605fe5c2ee475e61655d8d`

Selected component files are vendored under
`managed_components/qb4-dev__esp-lampsmart-ble/` and have been modified.
The LampSmart packet encoder and decoder in `matter/main/lampsmart_nimble.cpp`
and `sniffer/main/main.c` also incorporate adapted portions of this work.

The upstream package does not provide a separate copyright notice or LICENSE
file. Its explicit MIT declaration and origin are preserved here. The MIT
permission and warranty terms are reproduced in the repository's `LICENSE`.

## SDKs fetched separately during setup

These SDKs are stored below `.tooling/` and are not committed to this
repository.

| Dependency | Pinned version | License and notice |
| --- | --- | --- |
| ESP-IDF | `v5.5.4` | Apache-2.0 for original ESP-IDF code; bundled third-party libraries retain their own licenses |
| ESP-Matter | `177452a85301875f8f5a5865b8836f39d0953092` | Apache-2.0; the accompanying Matter notice is reproduced in [`NOTICE`](NOTICE) |
| connectedhomeip | `efefc94fee39d8d1fbbc3c27b9d7fc9025095887` | Apache-2.0 plus the Matter notice |

ESP-IDF firmware may additionally contain permissively licensed third-party
code such as FreeRTOS (MIT), lwIP (BSD-style), Newlib (BSD-style) and mbedTLS
(Apache-2.0 or GPL-2.0-or-later, with the Apache-2.0 option used by this build).
The license headers and license files shipped with the pinned ESP-IDF checkout
are authoritative.

## ESP Component Registry dependencies

The exact dependency graph and integrity hashes are recorded in
[`dependencies.lock`](dependencies.lock) and
[`matter/dependencies.lock`](matter/dependencies.lock).

| Component | Version | License |
| --- | ---: | --- |
| `qb4-dev/esp-lampsmart-ble` | 1.0.3 | MIT |
| `espressif/button` | 4.2.0 | Apache-2.0 |
| `espressif/cmake_utilities` | 1.1.1 | Apache-2.0 |
| `espressif/cjson` | 1.7.19~2 | MIT; copyright 2009–2017 Dave Gamble and cJSON contributors |
| `espressif/esp_delta_ota` | 1.1.4 | Apache-2.0 |
| `espressif/esp_encrypted_img` | 2.7.0 | Apache-2.0 |
| `espressif/esp_secure_cert_mgr` | 2.9.3 | Apache-2.0 |
| `espressif/led_strip` | 3.0.3 | Apache-2.0 |
| `espressif/mdns` | 1.11.3 | Apache-2.0 |

`esp_delta_ota` contains `detools`, which includes BSD-2-Clause and MIT code,
including work by Colin Percival, Erik Moqvist, Yuta Mori and the HDiffPatch
project. The downloaded component contains the complete corresponding license
texts.

## Binary releases

When distributing a prebuilt firmware image, distribute the applicable license
texts and this `NOTICE` alongside it. Generate an SPDX SBOM from the exact build
to capture only the components linked into that image:

```sh
esp-idf-sbom create --rem-unused --rem-config \
  -o sbom.spdx matter/build/project_description.json
```

Use of the Matter SDK does not make a product Matter-certified and does not
grant permission to use Connectivity Standards Alliance trademarks or logos.
