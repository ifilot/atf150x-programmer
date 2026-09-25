# Firmware protocol and programming algorithm

Version 2 uses USB CDC `Serial` at 115200 baud, 8N1, with DTR asserted. Do not
use 1200 baud: the Leonardo reserves that for bootloader entry. Firmware uses
D2=TDI, D3=TCK, D4=TDO, D12=TMS and portable Arduino GPIO calls with at least
2 microseconds per clock half-cycle. The actual clock speed is lower because of
GPIO call overhead.

## Framing

One command may be outstanding at a time. Lines have these forms:

```text
<sequence> <command and arguments>*<CRC16>\n
<sequence> OK[ <payload>]*<CRC16>\r\n
<sequence> ERR <reason>*<CRC16>\r\n
```

Sequence numbers are decimal 0 through 65535. CRC is four hexadecimal digits
using CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no reflection,
and no final XOR. It covers every byte before `*`. Firmware has a 96-byte input
buffer. The host rejects oversized replies, incorrect CRCs, and mismatched
sequences. Sequence values correlate replies; they do not provide
deduplication. The host never automatically retries an erase or write. Invalid
framing or CRC receives no reply and aborts the active programming session.

Row addresses and IDCODE arguments are hexadecimal. Word payloads contain byte
pairs in increasing byte order, least-significant byte first. Within each byte,
bit zero shifts first. Unused high bits in the last byte must be zero. An erased
86-bit word is ten `FF` bytes followed by `3F`; an erased 166-bit word is
twenty `FF` bytes followed by `3F`; an erased JTAG word is `0F`.

| Command | Payload after `OK` | Behavior |
| --- | --- | --- |
| `HELLO` | `ATF15XX 2 v0.2.0` | Reset the session and report protocol and firmware versions |
| `ID` | Eight hexadecimal digits | Reset the TAP, read IDCODE, and release pins |
| `BEGIN <IDCODE>` | None | Require that exact supported IDCODE and enter programming mode |
| `ERASE` | None | Erase the selected supported device; requires an active session |
| `WRITE <row> <hexbytes>` | None | Program one supported-device word; requires an active session |
| `READ <row>` | Word bytes | Read one supported-device word; requires an active session |
| `END` | None | Exit programming mode and release pins |

Error reasons include `DEVICE`, `SESSION`, `ADDRESS`, `LENGTH`, `HEX`,
`PROTECTED`, and `COMMAND`. `BEGIN` compares the requested IDCODE with the
physical device and accepts `0150203F` or `0150403F`. Only mapped addresses
are accepted. A write to row 200 must contain `0F`.

Partial lines time out after one second; sessions time out after five seconds
without a valid command. Long or malformed lines terminate sessions. Firmware
acknowledges commands only after their programming or read delay completes. The
host allows three seconds for a reply and two seconds for writes. If the host
loses contact, the firmware timeout provides eventual cleanup. Cleanup cannot
recover a device whose Flash was partially programmed; repeat the complete
flash operation.

On USB disconnect, malformed framing, or a session timeout, firmware exits
programming mode and releases the driven pins. USB failures are reported to the
user and are not retried automatically. A new connection starts with a fresh
handshake.

## JEDEC parsing and safeguards

The parser accepts STX/ETX-framed files with a design header terminated by `*`,
`QF16808` or `QF34192`, `F0` or `F1`, sparse or multiline `L` records,
and a required `C` fuse checksum. `N`/`NOTE`, `QP`, `QV`, and `J`
metadata are ignored; `G0` is accepted. Test vectors and other unsupported
record types are rejected.

Transmission checksums are verified when nonzero. The standard `0000` trailer
disables that check, as in the supplied WinCUPL examples. Files are read in
binary mode so Windows CRLF bytes remain available for transmission-checksum
validation.

Overlapping or out-of-range fuse data, wrong fuse counts, invalid checksums,
nonzero reserved fuses, and locking requests are rejected. The four
JTAG/security fuses at 16782 through 16785 for ATF1502AS, or 34166 through
34169 for ATF1504AS, must remain `1111`. The host does not silently alter the
final image. An image that leaves its arming switch safe remains safe after
flashing.

## JTAG and fuse packing

The portable `jtag.h` engine handles TAP transitions and waits. The host handles
JEDEC parsing, mapping, erase and blank checking, staging, verification, and
activation. IR is 10 bits and each scan starts and ends in Run-Test/Idle.
Firmware resets with six TMS-high clocks and one TMS-low clock.

The implementation follows Project Bureau's `util/device.py`,
`util/fuseconv.py`, and configuration database at commit
`8b8a97122ec238b6a09868a4bc7def20acef798c`. See the linked upstream sources in
[THIRD_PARTY.md](THIRD_PARTY.md).

| Operation | Sequence |
| --- | --- |
| Identify | IR 059, DR 32-bit IDCODE |
| Enable | IR 280, DR 10-bit 1B9 |
| Disable | IR 280, DR 10-bit 000, reset TAP |
| Erase | IR 2B3, IR 29E, wait 210 ms, IR 2BF |
| Program | IR 2A1, DR 11-bit address, IR 290 OR (address >> 8), DR data, IR 29E, wait 30 ms, IR 2BF |
| Read | IR 2A1, DR 11-bit address, IR 28C, wait 20 ms, IR 290 OR (address >> 8), DR data |

The ATF1502AS has 212 mapped words: rows 0x000 through 0x06B and 0x080
through 0x0E4 contain 86 bits, followed by rows 0x100 (32 bits), 0x200
(4 bits), and 0x300 (16 bits). Six JEDEC fuses 16802 through 16807 are reserved
and must be zero.

The ATF1504AS has 216 mapped words: rows 0x000 through 0x06B and 0x080
through 0x0E8 contain 166 bits, followed by the same three special rows. Six
JEDEC fuses 34186 through 34191 are reserved and must be zero. Unmapped
positions in packed physical rows are initialized to one for both devices.

The upstream prose memory tables have different endpoints from their mapping
code. This implementation uses the mapping code endpoints, covering every
non-reserved fuse without inventing additional rows.

The arming-switch fuse is 16750 on ATF1502AS and 34134 on ATF1504AS. The four
JTAG and protection fuses begin at 16782 and 34166 respectively. Accepted files
must leave all four at one. Staging holds the arming bit at one; after all words
verify, the host writes the final configuration word. Flash programming only
clears bits, so this last write can clear the arming bit. No vendor tool is
invoked at build time or runtime.
