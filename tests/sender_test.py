"""Host-side tests for the sender's CRC helpers. Run: python -m unittest tests/sender_test.py"""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import sender


def reference_mpeg2(data: bytes) -> int:
    """Plain bytewise CRC-32/MPEG-2, independent of sender.py, to check it against."""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def swap_words(data: bytes) -> bytes:
    """The STM32 CRC unit shifts each 32-bit word in MSB first, so a little-endian word is fed byte reversed."""
    return b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4))


class ReferenceTest(unittest.TestCase):
    def test_reference_matches_published_check_value(self):
        self.assertEqual(reference_mpeg2(b"123456789"), 0x0376E6E7)


class ChunkCrcTest(unittest.TestCase):
    def test_matches_reference_on_word_swapped_data(self):
        data = bytes(range(32))
        self.assertEqual(sender.compute_chunk_crc(data), reference_mpeg2(swap_words(data)))


class ImageCrcTest(unittest.TestCase):
    def setUp(self):
        self.a = bytes([0xA0 + i % 16 for i in range(1024)])
        self.b = bytes([0x10 + i % 16 for i in range(1024)])

    def test_equals_crc_of_whole_padded_image(self):
        self.assertEqual(sender.compute_image_crc([self.a, self.b]), sender.compute_chunk_crc(self.a + self.b))

    def test_chunk_boundaries_do_not_matter(self):
        image = self.a + self.b
        self.assertEqual(sender.compute_image_crc([image[:512], image[512:]]),
                         sender.compute_image_crc([image]))

    def test_duplicated_chunk_is_detected(self):
        self.assertNotEqual(sender.compute_image_crc([self.a, self.a]),
                            sender.compute_image_crc([self.a, self.b]))

    def test_includes_tail_padding(self):
        chunks = list(sender.build_chunks(self.a + b"\x01\x02\x03"))
        self.assertEqual(len(chunks[-1]), 4)
        self.assertEqual(sender.compute_image_crc(chunks),
                         sender.compute_chunk_crc(self.a + b"\x01\x02\x03\xFF"))


if __name__ == "__main__":
    unittest.main()
