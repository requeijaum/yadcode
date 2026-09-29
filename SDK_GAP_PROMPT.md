# Prompt — Closing the GD-ROM Knowledge Gap

> An English research prompt, for use against a responder with legitimate
> access to Sega Katana SDK material (your own archive, a licensed
> institutional copy, or a model with the documentation in context).
>
> The design goal is stated in §0 of the prompt itself. Read that before
> adapting anything: the prompt's value is mostly in its **evidence
> discipline**, not in the list of questions. If you cut the evidence
> clauses to make it shorter, you get plausible-sounding garbage back.

---

Copy everything below the line.

---

## 0. How to answer — read this first

I am reverse-engineering a Sega Dreamcast GD-ROM optical drive emulator
(an ODE). My device must answer the SH4 host's Sega Packet Interface
(SPI) commands with byte-exact timing and payloads. I have a working
decompilation pipeline and I have already resolved most of the protocol
from public sources, documentation, and disassembly. I have a specific
list of residual gaps.

**I do not want plausible answers. I want evidence.**

For every factual claim you make, cite the specific artifact it came
from:

- **Filename** as it appears in the SDK tree, and the **full path**.
- **Line number or section number** (e.g. `dvr/gdc/gdc.c:412`,
  `GDD §4.3.2`, header macro name).
- For any code that answers a question, the **actual code snippet**, not a
  paraphrase of it. I will read it and check it against my own
  disassembly.
- For anything derived rather than stated, **label it as inference** and
  show the reasoning.

If you cannot cite a source for a claim, write **`UNVERIFIED`** next to it
and move on. **Do not fill gaps with plausible-sounding reconstruction from
general ATA/CD knowledge.** A confident wrong CRC routine costs me more
than an honest blank, because I will encode it and test against real
hardware that does not exist yet.

If the SDK genuinely does not answer a question, say **"the SDK does not
answer this"** and, if possible, say *why* (the information lives in
BIOS-only code, or in the drive's own firmware, or nowhere at all). That
is a useful answer. I will record it as a closed question.

**Do not estimate.** If you are not sure whether a value is 0xB4 or 0xB6,
say so.

## 1. Context you need

- **Host**: Hitachi SH-4 @ 200 MHz. It talks to the GD-ROM over the G1 bus
  (a 50-pin connector, an extended IDE), using ATA-3 task-file registers
  plus a 12-byte command packet interface ("SPI" — Sega Packet Interface,
  *not* serial SPI).
- **The SPI packet**: 12 bytes. Byte 0 is the command. The host issues
  ATA command `0xA0` (PACKET) to arm it, then writes the packet through
  the DATA register.
- **The drive**: a real GD-ROM drive responds on the *slave* side of the
  G1 bus. The host (SH4) is the master. Relevant to device-select
  handling — see Q1.
- **My open questions** concern: the exact byte layout of specific command
  responses, the correct subcode CRC polynomial, several unconfirmed
  constants, and DMA-versus-PIO usage.
- I already have the Sega "GD-ROM Protocol (Sega Packet Interface)
  Specifications" document. **I do not need that document restated.** I
  need the things that document leaves out or contradicts.

## 2. Priority 1 — CD-DA subcode CRC (highest value)

**Question:** When the GD-ROM returns subcode data (SPI command `GET_SCD`,
`0x40`, format `0x00` returning 96 bytes of P–W subcode), what CRC or
checksum accompanies it, if any? If subcode is returned in a 100-byte
response, are the trailing 4 bytes a CRC over the 96 subcode bytes?

Specifically, I need to distinguish between the two candidate schemes
that circulate in public sources:

- **(a) XMODEM CRC** — polynomial `0x1021`, init `0x0000`, no reflection,
  no final XOR, MSB-first.
- **(b) A complemented variant** — same polynomial `0x1021` but the
  transmitted bytes are the one's complement of the computed CRC.

**This is the single question I most need answered, and the one most likely
to be hallucinated.** Please quote the actual routine. If the SDK returns
subcode via a BIOS syscall rather than computing a CRC in library code,
say so explicitly — that is itself a valid and important answer.

## 3. Priority 2 — the boot loader source (IP.BIN / boot)

The Katana SDK is understood to ship the source for the boot ROM, which is
compiled to the `IP.BIN` that retail discs carry. If that source is
present, I want the **GD-ROM read path** from it specifically:

- The **exact command sequence** the boot code issues to read the disc:
  which SPI commands, in what order, with what argument bytes.
- The **response validation**: how it decides a read succeeded, what it
  does on `ABRT` (abort), and any retry/timeout logic.
- Whether it uses **PIO or DMA** for bulk sector reads, and the precise
  condition that selects between them.

This code is also present, in compiled form, on every retail disc. I have
extracted and decompiled it from six different discs, so **I can
cross-check your answer against real disassembly.** If you can supply the
source, I will diff the two and report where the retail build diverges.
That diff is the most valuable thing you could hand me.

## 4. Priority 3 — the GD-ROM command library

- The function or inline sequence implementing SPI command **`0xA1`
  (IDENTIFY DEVICE)**, including the host's handling of the **80-byte
  blob** and the **abort** that precedes it. Public sources disagree on
  whether the abort is mandatory and on the exact blob length.
- The handling of the undocumented SPI command **`0x71`**: is the response
  6 bytes or 1012 bytes? If the SDK routes it through a length-checked
  buffer, that check is decisive evidence.
- The implementation of **`REQ_MODE` (`0x11`)** and **`SET_MODE` (`0x12`)**,
  particularly the 32-byte mode block: which bytes are writable, and the
  default standby-time value.

## 5. Priority 4 — registers, device select, DMA

- **Device / Head register.** The G1 device-select byte takes values
  `0x00` (master), `0x40` (master + LBA), `0x90` (master, reserved bits
  set), `0xB0` (slave). One public SDK-derived source states the GD-ROM
  "really does not like the reserved bits being set" and that selecting
  it as `0x90` "will not work". **Can you confirm this from the SDK
  headers or driver source?** Which bits are reserved, and does the
  GD-ROM's own firmware reject them?
- **`SB_G1GDRC` (`0x005F74A0`)** — the "set the access wait value when
  reading by a DMA" register. Confirm the address, and what the value
  `0x00001001` (described elsewhere as "Multi Word-DMA Mode 2") selects.
- The **`Features` register bit 0** as the PIO/DMA selector, and the
  **Sector Count** transfer-mode field: the enum values for PIO-flow,
  single-word DMA, and multi-word DMA.
- `BSY` assertion timing. The only figure I have from public
  documentation is **400 ns** from command receipt to `BSY` valid. Is
  there anything more precise in the SDK timing notes?

## 6. Priority 5 — constants

For each of these, the SDK's actual value and its source. Please do not
infer from surrounding code without saying so.

- **`GD_LEADOUT_FAD`** — is `549300` a driver constant, a disc-format
  convention, or specific to high-density GD-ROM images?
- **`GetBaseFAD()`** — a BIOS syscall returning the partition base frame
  address. Is it used by the SDK, and if so for what decision (region
  check, partition selection, something else)? My current value is
  `45150` and I want to know how the BIOS consumes it.
- The **`GDROM` event codes** the host receives on interrupt after a
  command completes, and the **`GDROM_CTR_*`** style status field layout
  in the `REQ_STAT` response.

## 7. Priority 6 — image formats

Does the SDK include tooling or documentation for **GDI** and the
high-density GD-ROM image layout — in particular the relationship between
the `.gdi` track table, the 45000 LBA boundary, and the `45150` frame
address that the area starts at? I want the SDK's own statement, if any,
rather than a reconstruction.

## 8. What I already know, so you do not repeat it

- The Sega SPI specification v1.30, and its command table.
- That the BIOS reaches the GD-ROM through syscalls, and that the BIOS
  syscall command numbering is a **different namespace** from the SPI
  packet numbering. (Example: BIOS `PIOREAD` = 16, while SPI `CD_READ` =
  `0x30`.) So BIOS-level constants will not answer SPI-level questions.
- That the SH4 decompiler handles this code well; I have 2193
  decompiled functions from a retail `1ST_READ.BIN`.
- That **device select `0x90` reportedly fails against a GD-ROM**, from
  one SDK-derived source I have not been able to verify at source.

## 9. Questions that no amount of SDK access will answer

I am asking you to say so plainly if this applies, rather than
manufacture a plausible answer:

- Whether a **real GD-ROM's firmware** actually behaves as the
  documentation describes, in edge cases.
- The **electrical timing** on the G1 bus: when `BSY` falls, how long a
  PIO burst takes, whether the host polls or waits on an interrupt.

These need a scope, a logic analyser, or a drive. If your material
speaks to them, I want to know; if it merely appears to, I want to know
that too.

## 10. Output format

For each of Q1–Q7, give me:

```
QUESTION: <n> — <one line>
ANSWER:   <direct, or "the SDK does not answer this">
EVIDENCE:
  <path>:<line> or <doc> §<section>
  <verbatim snippet, if code>
CONFIDENCE: CITED | INFERRED | UNVERIFIED
NOTES:    <anything that contradicts, or a caveat>
```

Rank your citations by how authoritative they are. A comment in a sample
program is weaker than a driver routine; a driver routine is weaker than
the GDD; the GDD is weaker than the boot ROM source. If two SDK artifacts
disagree with each other, **show me both** rather than picking one.
