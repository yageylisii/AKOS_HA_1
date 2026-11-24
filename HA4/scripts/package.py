#!/usr/bin/env python3
"""Create a source submission without machine-specific build files."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
output = root.parent / 'HA4_submission.zip'
excluded = {'build', 'logs', '__pycache__', '.git', '.DS_Store'}
binaries = {'cc_server', 'cc_operator', 'cc_station', 'cc_cabin', 'cc_observer'}
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if not path.is_file() or excluded.intersection(relative.parts) or relative.name in binaries:
            continue
        if relative.name in {'10-udp.pdf'} or relative.suffix == '.pyc':
            continue
        archive.write(path, Path('HA4') / relative)
print(f'Создан архив: {output}')
