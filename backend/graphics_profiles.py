"""Select a graphics profile only while Gothic is closed; retain other settings."""
from pathlib import Path
import csv
from contextlib import contextmanager
import datetime
import hashlib
import json
import os
import re
import subprocess

HERE = Path(__file__).resolve().parent
GAME = Path(os.environ.get('XARDAS_GAME_DIR', r'C:\Games\Gothic II'))  # folder Gothica II z Union
PROFILES = {
    'ai': {'label': 'DX11 + rozmowy AI', 'rt': 0, 'dlss5': 0},
    'rt': {'label': 'DX11 + RT + rozmowy AI', 'rt': 1, 'dlss5': 0},
    'full': {'label': 'DX11 + RT + DLSS 5 + rozmowy AI', 'rt': 1, 'dlss5': 1},
}
RENDERER_FILES = ('system/GothicModernRenderer.ini', 'GMR/SettingsProfiles/Default.g2GMRprofile')
FEEDER_FILE = 'system/dlss5-feed.cfg'


@contextmanager
def launch_lock(path=None):
    """One launcher owns profile changes and process creation, including preflight."""
    import msvcrt
    path = path or HERE / 'evidence/launcher.lock'
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b'0'); stream.flush()
        stream.seek(0)
        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise RuntimeError('Inne uruchomienie Gothica właśnie przygotowuje modele i grafikę.') from error
        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def gothic_running():
    result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Gothic2.exe', '/FO', 'CSV', '/NH'],
                            capture_output=True, text=True, encoding='ascii', errors='replace',
                            creationflags=subprocess.CREATE_NO_WINDOW, check=True)
    return any(row and row[0].lower() == 'gothic2.exe' for row in csv.reader(result.stdout.splitlines()))


def replace_setting(data, name, value):
    # Byte replacement preserves comments, encoding and unrelated user settings.
    pattern = rb'(?m)^(' + name.encode('ascii') + rb'=)[^\r\n]*'
    changed, count = re.subn(pattern, lambda m: m[1] + str(value).encode('ascii'), data)
    if count != 1:
        raise RuntimeError(f'Oczekiwano jednego ustawienia {name}, znaleziono {count}. Nie zmieniono profilu.')
    return changed


def atomic_write(path, data):
    temporary = path.with_name(path.name + '.xardas-profile.tmp')
    try:
        temporary.write_bytes(data)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def apply_profile(name, game=GAME, evidence=None):
    if name not in PROFILES:
        raise ValueError('Nieznany profil grafiki')
    if gothic_running():
        raise RuntimeError('Zamknij Gothica przed zmianą profilu grafiki.')
    selected = PROFILES[name]
    originals = {rel: (game / rel).read_bytes() for rel in (*RENDERER_FILES, FEEDER_FILE)}
    proposed = {}
    for rel in RENDERER_FILES:
        data = replace_setting(originals[rel], 'Lighting', selected['rt'])
        proposed[rel] = replace_setting(data, 'OutdoorGi', selected['rt'])
    proposed[FEEDER_FILE] = replace_setting(originals[FEEDER_FILE], 'enabled', selected['dlss5'])
    evidence = evidence or HERE / 'evidence/graphics-profiles'
    evidence.mkdir(parents=True, exist_ok=True)
    # A timestamped snapshot permits restoring precisely the settings before a switch.
    snapshot = evidence / datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    snapshot.mkdir()
    for rel, data in originals.items():
        target = snapshot / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    if gothic_running():
        raise RuntimeError('Gothic został uruchomiony podczas przygotowania profilu. Nie zmieniono ustawień.')
    record = {'profile': name, **selected, 'snapshot': str(snapshot),
              'files': {rel: hashlib.sha256(data).hexdigest() for rel, data in proposed.items()},
              'note': 'Wszystkie profile zachowują DX11, rozdzielczość i pozostałe ustawienia.'}
    written = []
    try:
        for rel, data in proposed.items():
            atomic_write(game / rel, data)
            written.append(rel)
        atomic_write(evidence / 'current.json', json.dumps(record, ensure_ascii=False, indent=2).encode('utf8'))
    except Exception:
        for rel in reversed(written):
            atomic_write(game / rel, originals[rel])
        raise
    return record
