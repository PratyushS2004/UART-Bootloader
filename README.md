# STM32 UART Bootloader

A bare-metal firmware update bootloader for the STM32F446RE (Nucleo-64), written from
scratch in C using only CMSIS. No HAL, no vendor middleware. It lives in a protected
region of flash, decides on every reset whether to stay in bootloader mode or jump into
the existing application, and can receive a full firmware image over a plain UART
connection with no external programmer attached.

Everything here was built and validated on real hardware: the CRC implementation was
checked against the STM32's own hardware CRC peripheral, the vector table relocation
was traced through a debugger, and the full receive-and-jump pipeline was tested with
intentional CRC corruption to confirm the retry path actually works.

## What it does

- Boots into one of two modes based on a physical button, sampled once at reset.
- In bootloader mode, receives a firmware image over UART in 1KB chunks, verifying
  each chunk with a hardware CRC before committing it to flash.
- In application mode, relocates the vector table and jumps straight into the
  existing application, with a sanity check on the target's stack pointer before
  committing to the jump.
- Ships with a host-side Python tool for sending a compiled `.bin` over serial.

## Hardware

- STM32 Nucleo-F446RE. No additional hardware required; the board's own user
  button (B1, PC13) and ST-Link USB-serial bridge are all this project uses.

## Memory Map

512 KB of flash is split into two protected regions:

| Region      | Address range           | Size   | Sectors |
|-------------|--------------------------|--------|---------|
| Bootloader  | `0x08000000`-`0x08007FFF` | 32 KB  | 0-1     |
| Application | `0x08008000`-`0x0807FFFF` | 480 KB | 2-7     |

Each region has its own linker script, so each program's compiled addresses
(vector table, stack pointer, reset handler) are correct for wherever it will
actually be flashed.

## Boot selection

On every reset, before any peripheral is configured, the bootloader samples the
user button (PC13/B1, active-low, internal pull-up):

- **Held down**: stays in bootloader mode and enters the UART receive state
  machine, waiting for a firmware image.
- **Not held**: validates the application's initial stack pointer against the
  known SRAM range (`0x20000000`-`0x20020000`). If it looks valid, the bootloader
  disables interrupts, relocates `SCB->VTOR` to `0x08008000`, restores the stack
  pointer, and branches directly into the application's reset handler. If it
  doesn't look valid, for example if nothing has ever been flashed there, it
  forces a full system reset rather than jumping into garbage.

## Transfer protocol

Firmware images are sent over UART (115200 baud, 8N1) in two layers: a single
4-byte total-length header, followed by repeated chunks of up to 1024 bytes,
each with its own length prefix and CRC:

```
[Total length: 4 bytes]
--- repeated per chunk ---
[Chunk length: 4 bytes]
[Chunk payload: up to 1024 bytes]
[Chunk CRC: 4 bytes]
```

Every chunk is fully received into a buffer and CRC-verified before anything is
written to flash, so a corrupted or dropped chunk never gets partially programmed.
The bootloader ACKs (`0x06`) after each successful step and NAKs (`0x15`) on
failure; the host waits for a response before sending the next chunk, and retries
a failed chunk rather than restarting the whole transfer.

If the host goes silent mid-transfer, whether it crashed, got unplugged, or
anything else, a per-byte receive timeout on the STM32 side triggers a full
system reset rather than leaving the bootloader waiting forever.

### CRC

Chunk integrity is checked with the STM32's hardware CRC peripheral, which
implements a specific, non-default CRC-32 variant: polynomial `0x04C11DB7`,
initial value `0xFFFFFFFF`, no input or output bit reflection, and no final
XOR (sometimes called CRC-32/MPEG-2). This is not the same algorithm as
Python's `zlib.crc32` or most "CRC-32" implementations you'll find online, which
default to the reflected variant used in Ethernet/zip/PNG. The host-side sender
implements the STM32's exact variant in software, verified byte-for-byte against
the hardware peripheral's own output before ever being used against real
firmware.

## Flash driver

Erase is sector-table driven: given a total image length, the driver looks up
exactly how many sectors need erasing and erases only those, once, up front,
independent of chunk size. Programming is word-aligned with hardware busy-flag
polling (with its own timeout) and explicit clearing of flash error status flags
after every operation.

## Host tool: sender.py

A Python script that takes a compiled `.bin` and streams it to the board:

```
python sender.py <port> <baudrate> <firmware.bin>
```

It pads the file to a 4-byte boundary, splits it into 1024-byte chunks, computes
a matching CRC for each one, and sends them with a bounded number of retries per
chunk. If a chunk still fails after all retries, the script aborts outright. It
never reports a transfer as successful unless every chunk was actually
acknowledged.

## Demo application: Application/main.c

A minimal test program, linked to run at `0x08008000`, included so the whole
pipeline can actually be exercised end to end. It doubles as a small hardware
proof of its own: it blinks an LED quickly for about a second, then a SysTick
interrupt permanently slows the blink rate. Since that interrupt only fires
correctly if the bootloader relocated the vector table before jumping, a
successful rate change is direct hardware confirmation that the hand-off worked,
not just that some code is running at the jump target.

## Building

Requires the `arm-none-eabi` GCC toolchain, CMake, and Ninja.

```
cmake --preset Debug
cmake --build --preset Debug
```

This builds two targets: `bootloader.elf` (flashed at `0x08000000`) and
`app.elf` (the demo application, flashed at `0x08008000`).

## Flashing and testing

1. Flash `bootloader.elf` with OpenOCD (or your debugger of choice).
2. Hold the user button through reset to enter bootloader mode.
3. Run `sender.py` against a compiled `app.bin` to transfer it into the
   application region.
4. Reset without holding the button. The bootloader should jump straight into
   the application.

## Design notes

A few decisions worth calling out, since they weren't obvious going in:

- **Chunk-then-CRC, not stream-then-write.** Chunks are fully buffered and
  CRC-checked before any flash write happens, specifically so a failed chunk
  never leaves a partial, unverified write sitting in flash.
- **The host must wait for each ACK/NAK before sending the next chunk.** This is
  a deliberate scope boundary: CRC only protects the payload it covers, not the
  length field that frames it, so a host that doesn't wait could desync the
  whole stream in a way CRC can't catch. Since the host is code under the same
  developer's control, defending against a host that ignores this contract was
  judged not worth the added complexity of interrupt-driven UART reception.
- **The SRAM bounds check uses an inclusive upper bound at exactly the top of
  RAM.** A Cortex-M stack pointer's initial value is conventionally set to one
  address past the last usable byte, since the first push decrements before
  writing. A valid application's stack pointer will legitimately equal that
  boundary address, not just fall somewhere below it.
