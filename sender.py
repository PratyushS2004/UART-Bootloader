"""Host-side firmware sender for the CMSIS UART bootloader (STM32F446RE).

Frame: [total_len u32][chunk_len u32][chunk payload][chunk crc u32] x N
CRC-32/MPEG-2: poly=0x04C11DB7 init=0xFFFFFFFF refin=false refout=false xorout=0
"""

import struct
import sys
import serial

CHUNK_PAYLOAD_SIZE = 1024
ACK = 0x06
NAK = 0x15
POLYNOMIAL = 0x04C11DB7
CRC_INIT = 0xFFFFFFFF

MAX_CHUNK_RETRIES = 5
CHUNK_ACK_TIMEOUT = 2.0
TOTAL_LENGTH_TIMEOUT = 10.0


class TransferAborted(Exception):
    """Raised when the bootloader pipeline fails."""
    pass


def _crc32_mpeg2_byte(byte_val: int, crc: int) -> int:
    # XOR into MSB (bits 31..24) to match STM32 HW shift order
    crc = (crc ^ (byte_val << 24)) & 0xFFFFFFFF
    for _ in range(8):
        top_bit = crc & 0x80000000
        crc = (crc << 1) & 0xFFFFFFFF
        if top_bit:
            crc ^= POLYNOMIAL
    return crc


def _crc32_mpeg2_word(word: int, crc: int) -> int:
    # Process 32-bit register MSB-first
    for shift in (24, 16, 8, 0):
        crc = _crc32_mpeg2_byte((word >> shift) & 0xFF, crc)
    return crc


def compute_chunk_crc(padded_chunk: bytes) -> int:
    # Unpack Little-Endian (<I) to match STM32 bytes_to_word() before HW CRC write
    crc = CRC_INIT
    for i in range(0, len(padded_chunk), 4):
        word = struct.unpack("<I", padded_chunk[i:i + 4])[0]
        crc = _crc32_mpeg2_word(word, crc)
    return crc


def pad_to_word(data: bytes) -> bytes:
    # Pad to 4-byte boundary using 0xFF (erased flash state)
    remainder = len(data) % 4
    return data if remainder == 0 else data + b"\xFF" * (4 - remainder)


def build_chunks(image: bytes):
    for i in range(0, len(image), CHUNK_PAYLOAD_SIZE):
        yield pad_to_word(image[i:i + CHUNK_PAYLOAD_SIZE])


def send_firmware(port: str, baudrate: int, bin_path: str):
    with open(bin_path, "rb") as f:
        image = f.read()

    chunks = list(build_chunks(image))
    total_length = sum(len(c) for c in chunks)

    print(f"{bin_path}: {len(image)} bytes raw, {total_length} padded, {len(chunks)} chunks")

    # Long initial timeout allows STM32 sector erasure to complete
    with serial.Serial(port, baudrate, timeout=TOTAL_LENGTH_TIMEOUT) as ser:
        ser.reset_input_buffer()   
        ser.reset_output_buffer()  

        ser.write(struct.pack("<I", total_length))
        
        resp = ser.read(1)
        if len(resp) != 1 or resp[0] != ACK:
            raise TransferAborted(f"total length rejected or no response: {resp!r}")
        print("total length accepted, erase complete")

        # Switch to shorter timeout for per-chunk streaming
        ser.timeout = CHUNK_ACK_TIMEOUT
        for idx, chunk in enumerate(chunks):
            crc = compute_chunk_crc(chunk)

            for attempt in range(1, MAX_CHUNK_RETRIES + 1):
                ser.write(struct.pack("<I", len(chunk)))
                ser.write(chunk)
                ser.write(struct.pack("<I", crc))

                resp = ser.read(1)
                if len(resp) == 1 and resp[0] == ACK:
                    print(f"chunk {idx + 1}/{len(chunks)}: ACK")
                    break
                print(f"chunk {idx + 1}: retry {attempt}/{MAX_CHUNK_RETRIES} ({resp!r})")
            else:
                raise TransferAborted(f"chunk {idx + 1} failed after {MAX_CHUNK_RETRIES} retries")

        # Wait for final bootloader verification ACK
        resp = ser.read(1)
        if len(resp) != 1 or resp[0] != ACK:
            raise TransferAborted(f"expected final ACK, got {resp!r}")
        print("transfer complete")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <port> <baudrate> <firmware.bin>")
        sys.exit(1)
    try:
        send_firmware(sys.argv[1], int(sys.argv[2]), sys.argv[3])
    except TransferAborted as e:
        print(f"ABORTED: {e}")
        sys.exit(1)