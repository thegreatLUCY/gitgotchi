#!/usr/bin/env python3
"""Capture Gitgotchi's real 128x64 framebuffer through its USB CLI command."""
import argparse
import pathlib
import re
import struct
import time
import zlib
import serial

def write_png(path, bitmap, scale=6):
    width, height = 128 * scale, 64 * scale
    pixels = bytearray()
    for y in range(64):
        row = bytearray()
        for x in range(128):
            dark = not ((bitmap[(y // 8) * 128 + x] >> (y % 8)) & 1)
            color = bytes((40, 61, 43) if dark else (198, 214, 160))
            row.extend(color * scale)
        pixels.extend((b'\0' + row) * scale)
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(pixels))) + chunk(b'IEND', b'')
    pathlib.Path(path).write_bytes(png)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--action', choices=['pet', 'feed', 'sleep', 'wake'])
    parser.add_argument('--wait', type=float, default=1)
    args = parser.parse_args()
    with serial.Serial(args.serial, 230400, timeout=.2, write_timeout=2) as device:
        time.sleep(.3)
        device.reset_input_buffer()
        if args.action:
            device.write(('gitgotchi ' + args.action + '\r').encode())
            time.sleep(args.wait)
            device.reset_input_buffer()
        device.write(b'gitgotchi screenshot\r')
        data = b''
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            data += device.read(device.in_waiting or 1)
            match = re.search(rb'PIP_SCREEN ([0-9a-f]{2048})', data)
            if match:
                write_png(args.output, bytes.fromhex(match[1].decode()))
                print('Saved real Flipper screen:', args.output)
                return
        raise SystemExit('No screen received. Open Gitgotchi and close other serial clients first.')

if __name__ == '__main__':
    main()
