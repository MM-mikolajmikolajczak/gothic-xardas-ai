"""Harmless Python startup-hook demonstration, confined to this directory."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parent
ENV = ROOT / ".venv"
IDENTITY = ROOT / ".lab-identity.json"
OWNER = "codex-xardas-pth-lab-v1"
PYTHON = ENV / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
SITE = ENV / ("Lib/site-packages" if sys.platform == "win32" else f"lib/python{sys.version_info.major}.{sys.version_info.minor}/site-packages")
HOOK = SITE / "cook.pth"
MODULE = SITE / "_codex_pth_demo.py"
PTH_TEXT = "import _codex_pth_demo; _codex_pth_demo.run()\n"
HOOK_SOURCE = '''"""Only prints a constant and records a lab-local event. No secrets or network."""
from pathlib import Path
import json
import os
import sys
import time

_ran = False

def run():
    global _ran
    if _ran:
        return
    prefix = Path(sys.prefix).resolve()
    root = prefix.parent
    identity = root / ".lab-identity.json"
    if prefix.name != ".venv" or not Path(__file__).resolve().is_relative_to(prefix) or not identity.is_file():
        return
    if json.loads(identity.read_text(encoding="utf-8")).get("owner") != "codex-xardas-pth-lab-v1":
        return
    _ran = True
    print("\\n[.pth] KOD WYKONAL SIE PRZY STARCIE PYTHONA.", flush=True)
    print("[LAB] Dane demonstracyjne: FAKE_API_KEY=DEMO-NIE-JEST-SEKRETEM\\n", flush=True)
    event = {"time_ns": time.time_ns(), "pid": os.getpid(), "event": "startup_hook", "synthetic_only": True}
    with (root / "startup-events.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(event) + "\\n")
'''


def require_owned() -> None:
    if not IDENTITY.is_file() or json.loads(IDENTITY.read_text(encoding="utf-8")).get("owner") != OWNER:
        raise RuntimeError("Brak identyfikatora tego laboratorium; niczego nie zmieniono.")
    if ENV.resolve().parent != ROOT or SITE.resolve().is_relative_to(ENV.resolve()) is False:
        raise RuntimeError("Katalog srodowiska wychodzi poza laboratorium.")


def setup() -> None:
    if ENV.exists() and not IDENTITY.exists():
        raise RuntimeError("Istniejace .venv nie nalezy do tego laboratorium.")
    ROOT.mkdir(parents=True, exist_ok=True)
    IDENTITY.write_text(json.dumps({"owner": OWNER, "synthetic_only": True}), encoding="utf-8")
    require_owned()
    if not PYTHON.exists():
        venv.EnvBuilder(with_pip=False, system_site_packages=False).create(ENV)
    if "include-system-site-packages = false" not in (ENV / "pyvenv.cfg").read_text(encoding="utf-8").lower():
        raise RuntimeError("Laboratorium nie moze uzywac globalnego site-packages.")
    SITE.mkdir(parents=True, exist_ok=True)
    for path, content in ((MODULE, HOOK_SOURCE), (HOOK, PTH_TEXT)):
        if path.exists() and path.read_text(encoding="utf-8") != content:
            raise RuntimeError(f"Nie nadpisze innego pliku: {path.name}")
        path.write_text(content, encoding="utf-8")


def cleanup() -> None:
    require_owned()
    for path, content in ((HOOK, PTH_TEXT), (MODULE, HOOK_SOURCE)):
        if path.exists():
            if path.read_text(encoding="utf-8") != content:
                raise RuntimeError(f"Nie usune zmienionego pliku: {path.name}")
            path.unlink()
    # No recursive removal: the interpreter stays usable and the hook is disabled.


def execute(*args: str, capture: bool = False) -> subprocess.CompletedProcess:
    require_owned()
    return subprocess.run([str(PYTHON), *args], cwd=ROOT, text=True, encoding="utf-8", errors="replace", capture_output=capture, check=False)


def test() -> None:
    setup()
    results = {}
    baseline_code = "print('APPLICATION_CODE')"
    normal = execute("-c", baseline_code, capture=True)
    hook_position = normal.stdout.find("[.pth]")
    results["startup_before_application"] = normal.returncode == 0 and 0 <= hook_position < normal.stdout.find("APPLICATION_CODE")
    no_site = execute("-S", "-c", baseline_code, capture=True)
    results["minus_S_suppresses_hook"] = no_site.returncode == 0 and "[.pth]" not in no_site.stdout and "APPLICATION_CODE" in no_site.stdout
    isolated = execute("-I", "-c", baseline_code, capture=True)
    results["minus_I_does_not_suppress_venv_site"] = isolated.returncode == 0 and "[.pth]" in isolated.stdout
    try:
        cleanup()
        clean = execute("-c", baseline_code, capture=True)
        results["cleanup_disables_hook"] = clean.returncode == 0 and "[.pth]" not in clean.stdout and not HOOK.exists() and not MODULE.exists()
    finally:
        setup()
    report = {"python": sys.version, "environment": str(ENV), "synthetic_only": True, "checks": results, "passed": all(results.values()), "normal_stdout": normal.stdout, "minus_S_stdout": no_site.stdout, "minus_I_stdout": isolated.stdout, "after_cleanup_stdout": clean.stdout}
    (ROOT / "test-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "checks": results}, indent=2))
    if not report["passed"]:
        raise SystemExit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("setup", "run", "shell", "compare", "cleanup", "test", "status"))
    args = parser.parse_args()
    if args.command == "setup":
        setup()
        print("Laboratorium gotowe. Zainstalowano tylko lokalny hak demonstracyjny.")
    elif args.command == "cleanup":
        cleanup()
        print("Hak .pth usuniety. Python w laboratorium dziala bez niego.")
    elif args.command == "test":
        test()
    elif args.command == "status":
        print(json.dumps({"python": str(PYTHON), "hook_present": HOOK.exists(), "synthetic_only": True}, indent=2))
    else:
        require_owned()
        if args.command == "shell":
            raise SystemExit(execute("-i").returncode)
        if args.command == "compare":
            raise SystemExit(execute("-S", "-c", "print('[APLIKACJA] Python uruchomiony z -S: hak .pth nie zostal wykonany.')").returncode)
        raise SystemExit(execute("-c", "print('[APLIKACJA] Teraz wykonuje sie pierwszy kod aplikacji.')").returncode)


if __name__ == "__main__":
    main()
