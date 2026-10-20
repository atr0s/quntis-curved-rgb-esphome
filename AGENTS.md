# AGENTS.md

Guidance for AI coding agents helping someone set up this project on their own machine and
hardware. Read [README.md](README.md) for the user-facing overview and
[docs/protocol.md](docs/protocol.md) for the protocol before changing any code.

## What the person needs

- ESP32-C3 Super Mini, nRF24L01+ module, 7 female–female dupont wires (wiring: [README](README.md#wiring), [docs/wiring.svg](docs/wiring.svg))
- The Quntis curved RGB light bar and its remote, within reach
- ESPHome: installed locally or in a container (`ghcr.io/esphome/esphome`)
- Python 3, plus a way to run PlatformIO and esptool. `uvx` works without installing anything:
  `uvx platformio …`, `uvx --from esptool esptool …`, `uvx --with pyserial python …`

Ask before installing anything, before flashing (it replaces whatever firmware is on the ESP),
and before pushing or publishing anything.

## The person is part of the loop

Captures only work while someone presses the remote. Plan for that:

- Start long captures as **background** commands, then tell the person exactly what to press,
  how often and for how long, *before* the window starts.
- Use slow, counted sequences (one tap every 2 s, start from one extreme, two extra taps at the
  end). The packet counter increments per press, so each packet can be matched to a known tap.
- The first tap after the remote has been idle only wakes it; tell them to tap once extra first.
- Ask what the bar did after each transmit test. Only the person can see the light.
- The PA+LNA radio can be overloaded by a remote held right against it; 30–50 cm works.

## Workflow

### 1. Find the serial port and check the board

```sh
ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null
uvx --from esptool esptool --port <port> chip-id    # expect ESP32-C3
```

### 2. Flash the research firmware and check the radio

```sh
cd tools/rf-tool && uvx platformio run -t upload --upload-port <port>
```

On boot it prints `[ready] …` or `[ERROR] NRF24L01 not found, check wiring`. Talk to it with
[`tools/analysis/rfcmd.py`](tools/analysis/rfcmd.py), which sends commands and records output for a
fixed time:

```sh
uvx --with pyserial python tools/analysis/rfcmd.py <port> <seconds> <logfile> <command…>
```

The rf-tool commands are listed at the top of [`tools/rf-tool/src/main.cpp`](tools/rf-tool/src/main.cpp).

### 3. Get the person's address and remote ID

These are probably unique per remote, so never assume the values in the example config.

```sh
# optional: confirm the channels (person taps power ~1-2x per second for ~4 minutes)
uvx --with pyserial python tools/analysis/rfcmd.py <port> 240 sweep.log z
grep packets= sweep.log        # the remote's channels show many repeats

# capture on one of them while the person presses a few buttons
uvx --with pyserial python tools/analysis/rfcmd.py <port> 60 capture.log "q 53"
python3 tools/analysis/decode.py capture.log
```

Use the `addr` and `id` fields from `decode.py` as `address` and `remote_id`. If the channels
differ from 19/53/80, set `channels` and `rx_channel` in the config.

### 4. Optional: prove transmit before building

```sh
uvx --with pyserial python tools/analysis/rfcmd.py <port> 2 tx.log "a <address10hex> <id8hex>" "t 31 a9 47 00 81 10"
```

That sends "main light on, brightness level 3, backlight off". Ask the person whether the bar reacted.

### 5. Configure, build and flash ESPHome

Start from [`example-esphome-config.yaml`](example-esphome-config.yaml). Follow the person's
existing ESPHome conventions (secrets names, API key and OTA password style, static IPs) if they
have other device configs. Never write their secrets into this repository.

- Compile: `esphome compile <config>.yaml` (or `docker exec <container> esphome compile /config/<config>.yaml`)
- **The first flash must go over USB.** Docker on macOS cannot reach USB serial ports, so compile in
  the container and flash the factory image from the host:
  ```sh
  uvx --from esptool esptool --port <port> write-flash 0x0 <build-dir>/firmware.factory.bin
  ```
  The build directory is `.esphome/build/<name>/build/` next to the config.
- Later updates can go over wifi: `esphome run <config>.yaml --device <name>.local --no-logs`
- Check: `esphome logs <config>.yaml --device <name>.local`. Look for the `quntis_rgb` config block and no `[E]` lines.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `NRF24L01 not found` | wiring. On the PA+LNA module the labels are on the back, and the two header rows are easy to swap. GND is the square pad on the edge row. V+ must be on 3V3. |
| Radio found, but sweep sees only scattered single packets | not this protocol on those settings, the remote too close, or no taps during the window |
| Packets on the right channels but `decode.py` finds no CRC-valid frames | different frame layout on another model: run `tools/analysis/crcsolve.py capture.log` |
| Radio flaky / drops out | the PA+LNA module's current bursts: add 10–100 µF across V+ and GND, keep wires short |
| HA doesn't follow the remote | wrong `rx_channel`, or `address` / `remote_id` don't match this remote |
| API key rejected when adding to Home Assistant | check the key against the device directly (e.g. `aioesphomeapi` with `noise_psk`) before assuming the device is wrong; stale HA discovery entries are a common cause |

The signal-strength (RPD) register did not work on the PA+LNA module during development, so don't
rely on it for channel finding; listen for the XN297 preamble instead (`z`).

## Changing the code

- [`components/quntis_rgb`](components/quntis_rgb): ESPHome external component (hub + light, select, switch platforms)
- [`tools/rf-tool`](tools/rf-tool): standalone PlatformIO research firmware
- [`tools/analysis`](tools/analysis): capture decoding and CRC solving

Every packet carries the full bar state. Keep that model: entities edit the shared `BarState` and
the hub transmits it. Validate protocol changes against real captures with `decode.py`, and
confirm transmit changes by asking the person what the bar did. Before committing, check that
`esphome config` passes for the example config and that `tools/rf-tool` still builds.
