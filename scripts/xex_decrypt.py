"""Decrypt a retail XEX2 with basic (or no) compression into its memory image.

Usage: python scripts/xex_decrypt.py <in.xex> <out.pe>
The output is laid out as loaded (file offset = address - image base). LZX
("normal") compression is not supported.
"""
import sys
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

RETAIL_KEY = bytes.fromhex("20B185A59D28FDC340583FBB0896BF91")

data = Path(sys.argv[1]).read_bytes()
be = lambda o, n=4: int.from_bytes(data[o:o + n], "big")
pe_offset, security_offset, count = be(8), be(0x10), be(0x14)
headers = {be(0x18 + i * 8): be(0x1C + i * 8) for i in range(count)}
fmt = headers[0x000003FF]
encryption, compression = be(fmt + 4, 2), be(fmt + 6, 2)
if compression not in (0, 1):
    sys.exit(f"compression type {compression} not supported")
payload = data[pe_offset:]
if encryption == 1:
    session = Cipher(algorithms.AES(RETAIL_KEY), modes.ECB()).decryptor()
    file_key = session.update(data[security_offset + 0x150:security_offset + 0x160]) + session.finalize()
    cipher = Cipher(algorithms.AES(file_key), modes.CBC(bytes(16))).decryptor()
    payload = payload[:len(payload) // 16 * 16]
    payload = cipher.update(payload) + cipher.finalize()
if payload[:2] != b"MZ":
    sys.exit("decryption failed (no MZ header)")
image = bytearray()
if compression == 1:
    blocks = (be(fmt) - 8) // 8
    position = 0
    for i in range(blocks):
        size, zeros = be(fmt + 8 + i * 8), be(fmt + 12 + i * 8)
        image += payload[position:position + size] + bytes(zeros)
        position += size
else:
    image = bytearray(payload)
Path(sys.argv[2]).write_bytes(image)
print(f"encryption={encryption} compression={compression} image=0x{len(image):X}")
