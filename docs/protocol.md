# Quntis curved RGB light bar: radio protocol

Reverse-engineered from the touch remote (3×AA) of the Quntis curved RGB monitor light bar
([Amazon AU B0F4DFJKTQ](https://www.amazon.com.au/dp/B0F4DFJKTQ)). Everything below was measured
with an nRF24L01+ listening to a real remote, and confirmed by transmitting generated packets to the bar.

## Radio layer

| Property | Value |
|---|---|
| Chip family | XN297 (nRF24-compatible GFSK with scrambling) |
| Data rate | 1 Mbps |
| Channels | 19, 53 and 80 (2419 / 2453 / 2480 MHz); every press is repeated on all three, possibly also 83 |
| Repeats | roughly 30–40 copies of each packet per channel per press |
| Address | 5 bytes, probably unique per remote; capture yours with `tools/analysis/decode.py` |
| Payload | 11 bytes |
| CRC | CRC-16-CCITT (poly 0x1021), init `0xB5D2`, final XOR `0xF494`, over the scrambled address + payload |

The final XOR value differs from the commonly published XN297 table (which gives `0xAD16` for this
length in normal mode). It was found empirically with [`tools/analysis/crcsolve.py`](../tools/analysis/crcsolve.py)
and validated on every clean frame captured.

### Frame on air (after the XN297 preamble `71 0F 55`)

```
[ 5 bytes scrambled address ] [ 11 bytes scrambled payload ] [ 2 bytes CRC ]
```

- Address: byte `i` on air = `address[4 - i] ^ SCRAMBLE[i]` (sent reversed)
- Payload: byte `i` on air = `bit_reverse(payload[i]) ^ SCRAMBLE[5 + i]`
- CRC: computed over the 16 on-air bytes, sent MSB first

```
SCRAMBLE = e3 b1 4b ea 85 bc e5 66 0d ae 8c 88 12 69 ee 1f
```

An nRF24L01 can receive and send this by using `55 0F 71` (in RF24 library byte order) as a 3-byte
address with CRC and auto-ack disabled, and carrying the 18 bytes above as its payload.

## Payload

```
 0  1  2  3 |  4  |   5    |  6  |     7     |  8   |  9  | 10
 remote ID  | cmd | bright | cct | backlight | mode | ctr | sum
```

| Byte | Field | Values |
|---|---|---|
| 0–3 | Remote ID | constant per remote |
| 4 | Command | see below |
| 5 | Main light brightness | `00` = main light off; `01`–`FF` stepless (the remote uses `1A 55 A9 FF`) |
| 6 | Colour temperature | `00` warmest … `FF` coolest, stepless (the remote uses `00 47 A2 FF`) |
| 7 | Backlight | `FF` on, `00` off |
| 8 | Backlight mode | `0x80 | n`, n = 1…15 (see below) |
| 9 | Counter | +1 per press, wraps; the bar accepted out-of-order values in testing |
| 10 | Checksum | sum of bytes 0–9, mod 256 |

**Every packet carries the complete state.** The command byte says which button was pressed, but
the bar applies the absolute values. So a controller can set any brightness or colour directly,
with no stepping and no state tracking.

### Commands

| cmd | Button | Notes |
|---|---|---|
| `30` | first tap after wake | seen once when the remote wakes; not needed for control |
| `31` | power | remote sends brightness `00` and backlight `00` for "off" |
| `32` | backlight button | short press: next mode. "Off" is the same command with byte 7 = `00` |
| `35` | brightness + | |
| `36` | brightness − | |
| `39` | cooler | |
| `3A` | warmer | |
| `3E` | auto-brightness off | |
| `40` | auto-brightness on | uses the light sensor on top of the bar |

Auto-brightness on/off are separate commands, not a toggle, and the remote alternates between them.

### Backlight modes

| n | Mode | n | Mode | n | Mode |
|---|---|---|---|---|---|
| 1 | White (unconfirmed, looks light grey) | 6 | Blue / Magenta | 11 | Green |
| 2 | Green / Red | 7 | Cyan / Magenta | 12 | Blue |
| 3 | Green / Blue | 8 | Rainbow | 13 | Yellow |
| 4 | Yellow / Blue | 9 | Colour cycle (holds each colour) | 14 | Cyan |
| 5 | Yellow / Cyan | 10 | Red | 15 | Magenta |

### Example

For a made-up remote with address `12 34 56 78 9A` and remote ID `A1 B2 C3 D4`: power on, brightness `55`,
colour temperature `A2`, backlight on in mode 9, counter `31`:

```
payload  A1 B2 C3 D4 31 55 A2 FF 89 31 CB
on air   79 C9 1D DE 97 | 39 A8 A5 26 22 26 CD ED F8 62 CC | 88 9D
         address        | payload                          | CRC
```

## Open questions

- Whether the address and remote ID differ between units. Quntis documents a pairing procedure
  (power-cycle the bar, then hold the remote's power button for about 4 s within 12 s), which suggests
  the bar learns each remote's identity. The pairing packet has not been captured yet; if it can be
  replayed, an ESP could pair as its own remote without needing an original one
- Whether the bar ever rejects a repeated or old counter value
- What switching the main light off while the backlight is on does, when sent with byte 7 = `FF`
- The real colour temperature range. Quntis says 3000–6500K; a phone app measured about 4000–6000K
