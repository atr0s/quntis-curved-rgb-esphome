# How the protocol was found

This is the path from "the existing project doesn't work with my bar" to a working ESPHome
integration. It's written down so the same steps can be repeated for other Quntis models.

## Starting point

[Lexy1972](https://github.com/Lexy1972/Reverse-Engineer-a-Quntis-Monitor-LED-Bar-Controller)
reverse-engineered the Quntis PRO+ (LI-HY-0208) with a logic analyser and an SDR: an unmarked
XN297 radio on channel 2 at 1 Mbps, sending relative commands (on/off toggle, dim up/down).
[bluemaex](https://github.com/bluemaex/Quntis-LED-Controller) turned that into an ESPHome
component and a sniffer for an ESP32-C3 + nRF24L01.

The curved RGB bar has a different, touch-sensitive remote. With the original sniffer (channel 2,
1 Mbps, XN297 preamble) it received nothing but noise, so the protocol had to be found from scratch.

## 1. Hardware sanity check

The first sniffer run reported `NRF24L01 not found`. A GPIO scan (each pin read with pull-up,
pull-down and driven high/low) showed MISO held hard low, which pointed to a wiring mistake on the
module's header. The NRF24L01+PA+LNA module has its pin labels on the back, and the two header rows
were easy to swap. After rewiring, the radio initialised.

## 2. Finding the channel

Two approaches failed first:

- **Signal-strength sweep** (`RPD` register) never triggered on any channel, even for Wi-Fi.
  It seems unusable on the PA+LNA module.
- **Promiscuous packet counting** (2-byte preamble-style address, ~24 ms per channel) saw only
  noise. Remote bursts are a few milliseconds long, so the chance of being on the right channel
  at the right moment was about 1%.

What worked was assuming an XN297-family chip like the other Quntis bars. Every XN297 frame
starts with the same preamble ending `71 0F 55`, so the rf-tool `z` command listens for exactly
that on each channel for 0.8 s, at each data rate, while the remote's power button is tapped. Noise
almost never matches a 3-byte preamble. Channels 19, 53 and 80 at 1 Mbps lit up immediately, with
dozens of identical packets per press, and they all decoded to the same address.

## 3. Finding the CRC

Decoding with the PRO+ packet layout (6-byte payload, standard XN297 CRC table) failed the CRC
check at every length. The XN297 CRC is CRC-16-CCITT, which is affine. For two frames of the same
length, `crc(A) ^ crc(B) == fieldA ^ fieldB`, whatever the init and final XOR. Testing that for
every candidate length across thousands of frame pairs
([`crcsolve.py`](../tools/analysis/crcsolve.py)) found the answer straight away: the CRC covers
16 bytes (5 address + 11 payload) and sits in bytes 16–17, with a final XOR of `0xF494`.

## 4. Decoding the payload

With framing solved, payloads descrambled cleanly ([`decode.py`](../tools/analysis/decode.py)).
Each control was captured with a scripted sequence of slow, counted taps: start at one extreme,
step to the other one tap every 2 seconds with two extra taps at the end, and step back. Because
the counter increments on every press, each packet lines up with a known tap.

- **Brightness:** byte 5 walked `1A → 55 → A9 → FF` and back. It's an absolute level, not a step command.
- **Colour temperature:** byte 6 walked `00 → 47 → A2 → FF`.
- **Backlight:** byte 8 cycled `81 … 8F` (modes 1–15) with command `32`.
- **Backlight off:** a long press didn't produce a distinct code. Comparing captures showed byte 7
  is backlight on/off and byte 5 = `00` means "main light off"; the power-off packet clears both.
- **Auto-brightness:** commands `40` and `3E` alternated. Tapping from the lowest brightness in a
  dark room showed `40` makes the bar brighten (auto on) and `3E` returns it to manual.

## 5. Proving it by transmitting

The rf-tool `t` command builds a frame from scratch (address, payload, checksum, scrambling, CRC)
and sends it on all three channels. The bar responded to power on/off, absolute brightness
(including a value below the remote's lowest step, which was visibly dimmer: brightness is
stepless), colour temperature (measured stepless with a colour meter app), all backlight modes,
backlight off, and auto-brightness on.

## Tools

- [`tools/rf-tool`](../tools/rf-tool): PlatformIO firmware for ESP32-C3 + nRF24L01 (sweep, capture, transmit)
- [`tools/analysis/rfcmd.py`](../tools/analysis/rfcmd.py): send rf-tool commands and record its serial output
- [`tools/analysis/decode.py`](../tools/analysis/decode.py): one decoded line per button press
- [`tools/analysis/crcsolve.py`](../tools/analysis/crcsolve.py): find CRC position and final XOR from raw captures
