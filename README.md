# STM32 UART Bootloader

A bare-metal firmware update bootloader for the STM32F446RE (Nucleo-64), written from
scratch in C using only CMSIS. No HAL, no vendor middleware. It lives in a protected
region of flash, decides on every reset whether to stay in bootloader mode or jump into
the existing application, and can receive a full firmware image over a plain UART
connection with no external programmer attached.

Everything here was built and validated on real hardware: the CRC implementation was
checked against the STM32's own hardware CRC peripheral, the vector table relocation
was traced through a debugger, and the full receive-and-jump pipeline was tested with
intentional CRC corruption to confirm the retry path actually works. The pure
decision logic (vector table checks, marker checks) is also unit tested on a PC at
its boundary values, and each test was confirmed able to fail by running it against
deliberately broken versions of the code.

## What it does

- Boots into one of two modes based on a physical button, sampled once at reset.
- In bootloader mode, receives a firmware image over UART in 1KB chunks, verifying
  each chunk with a hardware CRC before committing it to flash.
- In application mode, checks that the stored image is sane and complete before
  tearing anything down: the vector table (stack pointer and reset handler) and a
  marker record whose CRC must match the image in flash. Only then does it relocate
  the vector table and jump. If any check fails it stays in bootloader mode.
- Keeps a torn image marker, so a power cut in the middle of an update can never
  leave a half-written image that the bootloader would jump into.
- Ships with a host-side Python tool for sending a compiled `.bin` over serial.

## Hardware

- STM32 Nucleo-F446RE. No additional hardware required; the board's own user
  button (B1, PC13) and ST-Link USB-serial bridge are all this project uses.

## Memory Map

512 KB of flash is split into three regions:

| Region       | Address range             | Size   | Sectors |
|--------------|---------------------------|--------|---------|
| Bootloader   | `0x08000000`-`0x08003FFF` | 16 KB  | 0       |
| Image marker | `0x08004000`-`0x08007FFF` | 16 KB  | 1       |
| Application  | `0x08008000`-`0x0807FFFF` | 480 KB | 2-7     |

The bootloader's linker region is limited to sector 0, so if the code ever grows
past 16 KB the link fails instead of spilling into the marker sector. Flash erases
whole sectors, so the marker gets a sector of its own and can be erased without
touching the bootloader or the image. Only 12 bytes of it are used.

The bootloader and the application each have their own linker script, so each
program's compiled addresses (vector table, stack pointer, reset handler) are
correct for wherever it will actually be flashed.

## Boot selection

On every reset, before the UART or SysTick are configured, the bootloader enables
the CRC clock and samples the user button (PC13/B1, active-low, internal pull-up):

- **Held down**: stays in bootloader mode and enters the UART receive state
  machine, waiting for a firmware image.
- **Not held**: runs two checks, in this order, before touching any hardware state:
  1. **Vector table.** The initial stack pointer must lie in
     `0x20000000`-`0x20020000` (upper bound inclusive). The reset handler word must
     have bit 0 set (the Thumb bit) and, with that bit cleared, point inside the
     application's flash: `0x08008000` inclusive to `0x08080000` exclusive.
  2. **Image marker.** The marker must have the right magic and a sane size, and
     the CRC of the image in flash must match the CRC stored in the marker (see
     Torn image marker below).

  If both pass, the bootloader jumps into the application. If either fails, for
  example on a blank chip, it stays in bootloader mode and waits for an image,
  so a blank chip can be programmed without holding the button.

### The jump

Validation happens before anything is torn down, so a failed check can still be
reported while the UART is alive. Once the checks pass, the jump runs in this order:

1. Disable interrupts.
2. Disable USART2.
3. Stop SysTick, then clear its pending exception with `SCB->ICSR` `PENDSTCLR`.
   SysTick is a core exception, not an NVIC interrupt, so clearing the NVIC
   pending bits does not reach it. The write to `ICSR` is a plain `=` and not
   `|=`, because several `ICSR` bits are write-1-to-act and read back live status.
4. Disable and clear the NVIC interrupts.
5. Point `SCB->VTOR` at the application.
6. Run a small inline assembly block: `msr msp`, `cpsie i`, `bx`. Both values are
   passed in registers, so the compiler cannot place anything on the stack between
   setting the stack pointer and branching.

The handoff is a pretend reset: it does what the hardware does at reset, loading
the stack pointer and the reset handler from the first two words of the vector
table. Bit 0 of the reset handler word tells `bx` to stay in Thumb state. The CRC
clock is left on, like GPIOC from the button read. The application sets its own
clocks with `|=`, so that is harmless.

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

### Update sequence

1. The host sends the total length. The bootloader checks it without changing
   flash: it must be non-zero, a multiple of 4, and fit the application region
   (up to 480 KB inclusive). A bad length gets a NAK and nothing is erased.
2. The bootloader erases the marker sector first, then the image sectors, and ACKs.
3. Chunks arrive and are programmed as described above.
4. After the last chunk, while the UART is still alive, the bootloader checks the
   vector table, computes a CRC over the whole image read back from flash, writes
   the marker, and only then sends the final ACK and jumps.

If anything in steps 2 or 4 fails, the bootloader sends a NAK and returns to its
idle state to wait for a new image, instead of resetting, so the host always
learns that the update did not take. A final ACK means the image passed its
checks, the marker is written, and the jump is starting. It does not prove the
application is running.

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

## Torn image marker

A 12-byte record at the start of sector 1 (`0x08004000`), three 32-bit words:

| Offset | Field   | Meaning                                                               |
|--------|---------|-----------------------------------------------------------------------|
| 0      | `magic` | `0x89ABCDEF`. Tells a written record from erased flash (`0xFFFFFFFF`). |
| 4      | `size`  | Image length in bytes as programmed, including the host's `0xFF` padding. |
| 8      | `crc`   | CRC over that many bytes of the image, read back from flash.          |

- **Erased first.** The marker is erased before the image, so a power cut can never
  leave a marker that describes a different image than the one in flash.
- **Written last.** It is written only after the vector table check and the CRC
  read-back, in the order magic, size, crc. The CRC word is the commit.
- **Trusted only after recomputing.** At boot the size is range checked before it
  is used as a read length, because a torn record can have a valid magic and a size
  that is still `0xFFFFFFFF`. Then the CRC over the image must match.

Where a power cut leaves the board, by the point it happens:

| Power lost                                          | Next boot                                  |
|-----------------------------------------------------|--------------------------------------------|
| Before the marker is erased                         | Old application boots, nothing changed     |
| After the marker erase, during the image erase      | Stays in bootloader                        |
| During the chunks                                   | Stays in bootloader                        |
| After the last chunk, before the marker is written  | Stays in bootloader (image good, unproven) |
| Between the marker's words                          | Stays in bootloader                        |
| After the marker is written, before the ACK is seen | Application boots, host reports a failure  |

These cases come from working through the sequence. They have not all been
reproduced on hardware, and a cut inside a single flash operation (a half-erased
sector, a partly programmed word) cannot be reproduced with a debugger at all.

The last case is a real limitation: if the final ACK is lost, the host cannot tell
a failed update from a successful one, so it should treat that as unknown and check
the device. Also, a CRC gives integrity but not authentication: it catches
accidental corruption, but anyone who can write an image can write a matching
marker.

## Flash driver

Erase is sector-table driven: given a total image length, the driver looks up
exactly how many sectors need erasing and erases only those, once, up front,
independent of chunk size. The table starts at sector 2, so that erase can never
reach the bootloader or the marker. Sector 1 is reachable only through a separate
`erase_marker()`, `is_length_valid()` lets the state machine reject a bad length
before anything is erased, and `write_marker()` programs the record in order.
Programming is word-aligned with hardware busy-flag polling (with its own
timeout) and explicit clearing of flash error status flags after every operation.

## Host tool: sender.py

A Python script that takes a compiled `.bin` and streams it to the board:

```
python sender.py <port> <baudrate> <firmware.bin>
```

It pads the file to a 4-byte boundary, splits it into 1024-byte chunks, computes
a matching CRC for each one, and sends them with a bounded number of retries per
chunk. If a chunk still fails after all retries, the script aborts outright. It
never reports a transfer as successful unless every chunk was actually
acknowledged and the bootloader sent the final ACK. A final NAK means the image
was rejected, for example because its vector table was invalid, and the script
exits with an error. The board stays in bootloader mode and accepts a new upload
without a reset.

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

An application without a marker will not boot. The first time you flash a
bootloader with the marker check, or after any mass erase, upload the application
once through `sender.py` so the marker is written.

To see the marker, read three words from the start of sector 1:

```
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c init -c "mdw 0x08004000 3" -c shutdown
```

After a good upload they should read the magic, the padded size, and the CRC.
After a rejected upload they should read `0xFFFFFFFF` three times, because the
marker is erased at the start and only written once every check has passed.

### Host tests

The pure checks compile on a PC with no STM32 headers:

```
gcc -IBootloader tests/vector_check_test.c Bootloader/vector_check.c -o vector_check_test
gcc -IBootloader tests/marker_test.c Bootloader/marker.c -o marker_test
```

Each prints the number of failing cases and returns a nonzero exit code if any fail.

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
- **Validate before tearing down.** The jump can no longer fail halfway. Every check
  runs first, while the UART still works, so a bad image gets a NAK instead of a
  reset the host cannot explain. A blank chip falls through to the bootloader instead
  of resetting in a loop.
- **Do every harmless check before the first destructive step.** The length is
  validated before the marker is erased, because a bad length would otherwise cost a
  good marker, leaving a working image that the bootloader refuses to boot.
- **The marker is written last, and only for an image that passed everything.** An
  image rejected for a bad vector table never gets a marker, since the marker means
  "complete and verified."
- **The reset handler check.** The Cortex-M only runs Thumb code, and bit 0 of a
  branch target selects Thumb state. A stored reset handler with bit 0 clear would
  fault on the first instruction, with nothing set up to report it, so the
  bootloader rejects it while it still can.
- **The SRAM bounds check uses an inclusive upper bound at exactly the top of
  RAM.** A Cortex-M stack pointer's initial value is conventionally set to one
  address past the last usable byte, since the first push decrements before
  writing. A valid application's stack pointer will legitimately equal that
  boundary address, not just fall somewhere below it.
