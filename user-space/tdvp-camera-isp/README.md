# CPU0 camera ISP integration work

This directory is **not yet enabled in the image**. The first implemented
component is a sensor-registration ABI adapter for an official scalar ISP
candidate. Neither passing its tests nor finding a scalar executable proves
that the physical camera can capture frames.

## Pinned candidate and reason for the adapter

The SDK currently pinned by TDVP is `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7`.
Its ISP executables contain RVV instructions, while Linux executes on CPU0.
The newer official v0.8.1 binary was also inspected: 1,572 vector mnemonics were
found in executable-section disassembly, including unconditional vector stores
in `CamDeviceSensorIsiGetMetadataWindow`. Its official archive SHA256 is
`2ee5f9703eaa466e97b257a016f282d1b6cf1fe9a677e72ea53f22ede3a2eb69`.
See the [upstream v0.8.1 package](https://github.com/kendryte/k230_linux_sdk/blob/ac0ac1e1888f15ce2fe7713e6a587dc1268c57e2/buildroot-overlay/package/vvcam/isp-media-server/isp-media-server.mk).

An alternative exists in official release **v0.6.1**, commit
`155359af908a5fb38e870e81ee82c91bceb30800`:

- [ISP binary](https://github.com/kendryte/k230_linux_sdk/blob/155359af908a5fb38e870e81ee82c91bceb30800/buildroot-overlay/package/vvcam/isp_media_server)
- SHA256: `5d3e7bd8914cd2a3d9ca9da03a2743e7bd8ab24b9cb090929950b632ffb833ce`
- Git blob: `baec321f6f683e455d0a56914af988998e9b5dde` (download matched the pinned Git object)
- ELF architecture: RV64GC; executable-section disassembly found zero vector
  mnemonics. The metadata function uses scalar stores. This is static ISA
  evidence, not a successful camera runtime test.

Do **not** combine that binary with today's `libvvcam.so` unchanged. The
[legacy sensor header](https://github.com/kendryte/k230_linux_sdk/blob/155359af908a5fb38e870e81ee82c91bceb30800/buildroot-overlay/package/vvcam/include/vvcam_sensor.h)
has nine callbacks. The pinned modern header inserted four flip callbacks
before the gain/exposure callbacks without changing `VVCAM_API_VERSION=1`.

| Registration layout on LP64 | Legacy ISP | Current driver |
| --- | ---: | ---: |
| Entire `vvcam_sensor` size | 80 | 112 |
| Analog gain callback byte offset | 56 | 88 |
| Digital gain callback byte offset | 64 | 96 |
| Exposure callback byte offset | 72 | 104 |

`src/tdvp-vvcam-legacy-adapter.c` registers an explicitly mapped, static-lifetime
legacy table for GC2093. It preserves the typed callbacks, return values and
sensor context. It must be linked with the board GC2093 driver **instead of**
vendor `src/lib.c`. It performs no I2C, reset, clock or stream operation during
registration. It does not advertise the newer flip-control interface.

## Verification already performed

```sh
bash buildroot/tools/test-tdvp-camera-legacy-abi.sh
```

The test uses the pinned current vendor header and a mock GC2093. It verifies
registration without side effects, all nine callback slots, context, integer,
boolean and floating-point arguments, return values, and layout assertions.
Modern flip slots deliberately abort if reached. The same test was cross-built
with the actual SDK CPU0 toolchain and passed on the board on 2026-09-07.
Target test SHA256:
`90178e764baed4aead8a30690328557269a13d4aa34e8890861bbd11a5039a76`.

The PR workflow runs the host ABI regression. It does not claim camera
acceptance, install this adapter, or replace the existing ISP binary.

## Remaining integration requirements

1. Build an isolated, pinned scalar-ISP plus matching sensor-plugin package;
   verify dynamic dependencies and kernel event compatibility. The modern
   kernel added crop/selection and sensor controls that the older daemon may
   not implement; basic capture and unsupported operations need real testing.
2. Adapt the board's GC2093 I2C access to the actual I2C4 controller without
   renumbering the touch/keyboard buses or probing unrelated I2C devices.
   Current vendor sensor code hardcodes `/dev/i2c-0`; it must not be deployed
   unchanged on TDVP.
3. Integrate the official board CSI2, GPIO21 reset and MCLK1 requirements using
   a reviewed device tree/module patch. Preserve CPU1's reservation and all
   Linux display, GPU, SD, keyboard and radio ownership.
4. Perform bounded physical chip-ID, stream start, frame acquisition and stop
   tests, then test camera preview through Wayland alongside VGLite and CPU1.
5. Only after this works, enable the service/package, update hardware status
   and produce a complete image. No fake camera node or acceptance marker.

Moving the camera to CPU1 is a separate fallback, not an action taken here.
The donor MPP startup initializes VO, audio, DMA and other shared peripherals;
simply enabling `RT_USING_MPP` would violate the current ownership contract.
