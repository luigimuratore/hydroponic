#!/usr/bin/env python3
"""Rigenera la dashboard compressa inclusa nel firmware ESP8266."""
from pathlib import Path
import gzip

root = Path(__file__).resolve().parents[1]
folder = root / "firmware" / "Idroponica_D1mini"
source = folder / "dashboard.html"
target = folder / "dashboard.h"
compressed = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)
target.write_text(
    '#pragma once\n#include <Arduino.h>\n'
    'const uint8_t DASHBOARD_GZ[] PROGMEM = {\n'
    + ','.join(map(str, compressed))
    + '\n};\nconst size_t DASHBOARD_GZ_LEN = sizeof(DASHBOARD_GZ);\n'
)
print(f"{source.name}: {source.stat().st_size} byte; gzip: {len(compressed)} byte")
