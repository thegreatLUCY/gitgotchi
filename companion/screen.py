"""Encode the tested Flipper framebuffer polarity as a scaled PNG."""
import struct
import zlib

def encode_png(bitmap, scale=6):
    width, height = 128 * scale, 64 * scale
    pixels = bytearray()
    for y in range(64):
        row = bytearray()
        for x in range(128):
            dark = bool((bitmap[(y // 8) * 128 + x] >> (y % 8)) & 1)
            color = bytes((40, 61, 43) if dark else (198, 214, 160))
            row.extend(color * scale)
        pixels.extend((b'\0' + row) * scale)
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(pixels))) + chunk(b'IEND', b'')
    return png

