# Firmware protocol and programming algorithm

Version 1 uses USB CDC `Serial` at 115200 baud, 8N1, with DTR asserted. Do not
use 1200 baud: the Leonardo reserves that for bootloader entry. Firmware uses
D2=TDI, D3=TCK, D4=TDO, D12=TMS and portable Arduino GPIO calls with at least
2 microseconds per clock half-cycle (actual clock speed is lower due to GPIO
call overhead).

## Framing

One outstanding command at a time; ASCII lines:

```text
<sequence> <command and arguments>*<CRC16>\n
<sequence> OK[ <payload>]*<CRC16>\r\n
<sequence> ERR <reason>*<CRC16>\r\n
```

Sequence numbers are decimal 0–65535. CRC is four hex digits, CRC-16/CCITT-FALSE
(polynomial 0x1021, initial 0xFFFF, no reflection, no final XOR), calculated over
every byte before `*`. Firmware has a 96-byte input buffer. The host rejects
oversized replies, incorrect CRCs and mismatched sequences. Sequences correlate
replies; they do **not** provide deduplication. The host never automatically
retries an erase or write. Invalid framing/CRC receives no reply and aborts the
active programming session.

Row addresses are hexadecimal. Word payloads are byte pairs in increasing byte
order, **least-significant byte first**. Within each byte bit zero shifts first.
Unused high bits in the last byte must be zero: an erased 86-bit word is ten
`FF` bytes followed by `3F`; an erased JTAG word is `0F`.

| Command | Payload after `OK` | Behavior |
| --- | --- | --- |
| `HELLO` | `ATF1502 1 v0.1.0` | Reset session and report protocol and firmware versions |
| `ID` | eight hexadecimal digits | Reset TAP, read IDCODE, release pins |
| `BEGIN` | none | Check exact IDCODE, enter programming mode |
| `ERASE` | none | Erase; requires active session |
| `WRITE <row> <hexbytes>` | none | Program one word; requires active session |
| `READ <row>` | word bytes | Read one word; requires active session |
| `END` | none | Exit programming mode and release pins |

Error reasons include `DEVICE`, `SESSION`, `ADDRESS`, `LENGTH`, `HEX`,
`PROTECTED`, and `COMMAND`. `BEGIN` refuses a device other than ID 0150203F.
Only mapped addresses are accepted. Writes to row 200 must contain 0F.

Partial lines time out after one second; sessions time out after five seconds
without a valid command. Long or malformed lines terminate sessions. Firmware
commands acknowledge only after their programming/read delay completes. The
host allows three seconds for a reply and two seconds for writes; no valid
command takes close to those limits. If the host loses contact, firmware's
session timeout provides eventual cleanup. Cleanup cannot recover a design
whose Flash was partially programmed; retry the complete flash operation.

## JTAG and fuse packing

The portable `jtag.h` engine handles the real TAP transitions and waits; the
host handles JEDEC parsing, mapping, erase/blank-check, staging, verification and
activation. IR is 10 bits and each scan starts/ends in Run-Test/Idle. Firmware
resets with six TMS-high clocks and one TMS-low clock.

The implementation follows Project Bureau's `util/device.py`, `util/fuseconv.py`
and configuration database at commit
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

The 212 mapped words are 0x000–0x06B and 0x080–0x0E4 (86 bits each), 0x100
(32 bits), 0x200 (4 bits), and 0x300 (16 bits). Unmapped columns in the 86-bit
words are initialized to one. Six reserved JEDEC fuses 16802–16807 have no
physical mapping and must be zero in accepted input files.

The upstream prose memory table has different endpoints from its mapping code.
This implementation uses the mapping code's endpoints above, which cover all
16,802 non-reserved JEDEC fuses; it does not invent writes to additional rows.

JEDEC fuse 16750 maps to row 0x100 bit 31, the arming switch. Staging holds that
bit at one; after all rows verify, the host writes the image's final configuration
word. Flash programming ANDs new data with existing data, so this final write
only clears the arming bit. Fuse 16782 controls read protection and 16783
controls JTAG multiplexing; both remain one. Verification compares every mapped
word with the staged image and then compares the final activation word. No
vendor tools are invoked at build time or runtime.
