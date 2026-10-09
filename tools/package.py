#!/usr/bin/env python3
"""Package already-built ESP8266 artifacts; never flashes a device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
from datetime import datetime, timezone
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def verify_arduino_image(firmware):
    # Arduino elf2bin patches two CRC words after writing the segment checksums.
    # Zero those words before verifying the original checksums and whole-image CRC.
    raw = bytearray(firmware)
    size, expected_crc = struct.unpack_from('<II', raw, 4096 + 16)
    assert size == len(raw), 'Arduino CRC image length mismatch'
    raw[4112:4120] = b'\0' * 8
    table = []
    for n in range(256):
        c = n << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04c11db7 if c & 0x80000000 else c << 1) & 0xffffffff
        table.append(c)
    crc = 0xffffffff
    for b in raw:
        crc = ((crc << 8) ^ table[((crc >> 24) ^ b) & 255]) & 0xffffffff
    assert crc == expected_crc, 'Arduino eboot CRC mismatch'
    for start in (0, 4096):
        assert raw[start] == 0xe9
        pos, checksum = start + 8, 0xef
        for _ in range(raw[start + 1]):
            _, length = struct.unpack_from('<II', raw, pos)
            pos += 8
            assert pos + length <= len(raw), 'segment runs beyond image'
            for b in raw[pos:pos + length]: checksum ^= b
            pos += length
        checksum_pos = pos + (15 - (pos - start) % 16)
        assert checksum == raw[checksum_pos], 'original boot/application segment checksum mismatch'
    return hex(crc)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='dist')
    args = parser.parse_args()
    out = ROOT / args.output
    out.mkdir(parents=True, exist_ok=True)
    build = ROOT / '.pio/build/nodemcuv2'
    firmware = (build / 'firmware.bin').read_bytes()
    filesystem = (build / 'littlefs.bin').read_bytes()
    version = re.search(r'FW_VERSION.*?(\d+\.\d+\.\d+[A-Za-z0-9.-]*)', (ROOT / 'platformio.ini').read_text()).group(1)
    fs_start, fs_size = 0x300000, 0xFA000
    assert 0 < len(firmware) <= 1044464 and firmware[0] == 0xE9, 'firmware size/header invalid'
    image_crc = verify_arduino_image(firmware)
    assert len(filesystem) == fs_size, 'LittleFS size does not match 4m1m layout'
    shutil.copy2(build / 'firmware.bin', out / 'firmware.bin')
    shutil.copy2(build / 'littlefs.bin', out / 'littlefs.bin')
    merged = firmware + b'\xff' * (fs_start - len(firmware)) + filesystem
    (out / 'flash-all.bin').write_bytes(merged)
    assert merged[:len(firmware)] == firmware and merged[fs_start:] == filesystem
    assert len(merged) == 0x3FA000
    shutil.copy2(ROOT / 'FLASHING.md', out / '刷写说明.md')
    shutil.copy2(ROOT / 'LICENSE', out / 'LICENSE-glimmer.txt')
    shutil.copy2(ROOT / 'tests/reference/LICENSE', out / 'LICENSE-gallery.txt')
    commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    tracked_dirty = bool(subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True).strip())
    manifest = {
        'version':version, 'source_commit':commit, 'source_dirty':tracked_dirty,
        'built_at_utc':datetime.now(timezone.utc).isoformat(),
        'hardware':{'mcu':'ESP8266','flash_bytes':4194304,'panel':'SmallTV-Ultra ST7789 240x240','cpu_mhz':160},
        'images':{}, 'flash_order':['firmware.bin','littlefs.bin'],
        'filesystem_update_erases_config':True,
        'gallery_reference_commit':'db934a9d60397db8c0a3bbefb1f007bb3a6616e0',
        'verification':{'upstream_comparison_seeds':64,'hardware_connected':False,'arduino_eboot_crc':image_crc,'boot_and_application_segment_checksums':'valid'}
    }
    for name,offset,kind in [('firmware.bin',0,'OTA firmware / UART'),('littlefs.bin',fs_start,'OTA filesystem / UART'),('flash-all.bin',0,'UART only')]:
        p = out / name
        manifest['images'][name] = {'bytes':p.stat().st_size,'offset':hex(offset),'usage':kind,'sha256':sha(p)}
    (out / 'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    files=['firmware.bin','littlefs.bin','flash-all.bin','刷写说明.md','LICENSE-glimmer.txt','LICENSE-gallery.txt','manifest.json']
    (out / 'SHA256SUMS').write_text(''.join(f'{sha(out/name)}  {name}\n' for name in files))
    archive = out / f'glimmer-{version}.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for name in files+['SHA256SUMS']: z.write(out / name,name)
    print(json.dumps({'archive':str(archive),'archive_sha256':sha(archive),'images':manifest['images'],'source_commit':commit,'source_dirty':tracked_dirty},ensure_ascii=False,indent=2))
if __name__ == '__main__': main()
