# MeshCore on Linux (portduino + CH341A USB-SPI)

Runs a MeshCore node as a regular Linux executable — no microcontroller.
The LoRa radio (e.g. Ebyte E22-900M30S, SX1262 + 1W PA) is attached to the PC
over USB via a CH341A USB-SPI adapter, using the same userspace driver stack
as Meshtastic's `meshtasticd` (portduino + `libch341-spi-userspace`, libusb —
no kernel module required).

## Host requirements

- Linux, gcc/g++
- `libusb-1.0` headers (Arch: `libusb`, Debian: `libusb-1.0-0-dev`)
- `i2c-tools` / `libi2c-dev` (for the portduino core's `<i2c/smbus.h>`)
- USB access to the adapter (run as root, or add a udev rule):

```
# /etc/udev/rules.d/99-ch341.rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="5512", MODE="0666"
```

## Wiring (E22-900M30S)

| CH341A line (SOP-28 pin) | E22-900M30S |
|--------------------------|-------------|
| D0 / CS0#  (15)          | NSS         |
| D3 / DCK   (18)          | SCK         |
| D5 / DOUT  (20)          | MOSI        |
| D7 / DIN   (22)          | MISO        |
| D1 / CS1#  (16)          | RXEN        |
| D2 / CS2#  (17)          | NRST        |
| D4 / DOUT2 (19)          | BUSY        |
| D6 / DIN2  (21)          | DIO1        |
| —                        | TXEN → DIO2 (jumper on the module side) |

TXEN is driven by the radio itself via DIO2 (`SX126X_DIO2_AS_RF_SWITCH`), so
only RXEN needs a CH341 GPIO. On cheap CH341A programmer boards D1/D2/D4/D6
are often not broken out — wire directly to the chip pins if needed.

Power: the E22 is a 3.3V device and the PA draws >600mA on TX — power it from
a separate 3.3V supply (common GND with the adapter), not from the CH341A's
3.3V regulator.

## Build & run

```bash
pio run -e Native_CH341_repeater       # or Native_CH341_room_server
.pio/build/Native_CH341_repeater/program
```

### Companion mode (phone app over WiFi/LAN)

```bash
pio run -e Native_CH341_companion_wifi
.pio/build/Native_CH341_companion_wifi/program
```

Runs a companion node with a TCP server on port 5000. In the MeshCore phone
app add a node via **WiFi/IP**, entering this machine's LAN IP and port 5000
(the phone must be on the same network; open the port in the firewall if any).

The node's filesystem (identity, prefs) lives in `~/.portduino/default`.
Useful flags / environment:

- `--fsdir <dir>` — use a different filesystem directory (multiple nodes)
- `--erase` — wipe the filesystem before starting
- `MESHCORE_CH341_SERIAL=xxxxxxxx` — select a specific adapter by USB serial

The repeater/room-server CLI is on stdin/stdout, same commands as on MCU
targets (`help`, `set freq ...`, `password ...`, etc).

## Notes

- Radio pin numbers in `platformio.ini` are CH341 GPIO line numbers (D0–D7),
  not host GPIOs.
- DIO1 interrupts are delivered by a polling thread inside
  `libch341-spi-userspace`; latency is a few ms, which is fine for LoRa.
- `erase` CLI command is not supported on this target — use `--erase`.
