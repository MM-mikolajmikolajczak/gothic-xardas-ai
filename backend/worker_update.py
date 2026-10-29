"""Replace only this project's stale voice worker, while Gothic is closed."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys

PROJECT=Path(__file__).resolve().parents[1]
if str(PROJECT) not in sys.path:sys.path.insert(0,str(PROJECT))
from graphics_profiles import gothic_running


def worker_command_matches(image, command, python, script):
    if not isinstance(image,str) or not isinstance(command,str):return False
    expected=Path(python).resolve();allowed={expected}
    # Windows venv launchers run the exact base interpreter from pyvenv.cfg;
    # CIM reports that executable rather than Scripts/python.exe.
    config=expected.parent.parent/'pyvenv.cfg'
    if expected.parent.name.lower()=='scripts' and config.is_file():
        for line in config.read_text(encoding='utf8').splitlines():
            key,separator,value=line.partition('=')
            if separator and key.strip()=='home':
                base=(Path(value.strip())/'python.exe').resolve()
                if base.is_file():allowed.add(base)
    if Path(image).resolve() not in allowed:return False
    shell=ctypes.WinDLL('shell32',use_last_error=True)
    shell.CommandLineToArgvW.argtypes=[wintypes.LPCWSTR,ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype=ctypes.POINTER(wintypes.LPWSTR)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.LocalFree.argtypes=[wintypes.HLOCAL];kernel.LocalFree.restype=wintypes.HLOCAL
    count=ctypes.c_int();arguments=shell.CommandLineToArgvW(command,ctypes.byref(count))
    if not arguments:return False
    try:args=[arguments[i] for i in range(count.value)]
    finally:kernel.LocalFree(ctypes.cast(arguments,wintypes.HLOCAL))
    if len(args)!=8:return False
    return (Path(args[0]).resolve()==Path(image).resolve()
            and args[1:4]==['-X','utf8','-u']
            and Path(args[4]).resolve()==Path(script).resolve()
            and args[5:7]==['--worker','--mode']
            and args[7] in ('live_model','secure','staged_vulnerable'))


def restart_outdated_worker(ready,root,python,script):
    pid=ready.get('pid')
    if type(pid) is not int or pid<=0:raise RuntimeError('Nieprawidłowy identyfikator usługi głosu.')
    if gothic_running():raise RuntimeError('Zamknij Gothica, aby zaktualizować usługę głosu.')
    if any((Path(root)/'requests').glob('*.json')):
        raise RuntimeError('Usługa głosu kończy odpowiedź. Spróbuj uruchomić grę za chwilę.')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
    kernel.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT];kernel.TerminateProcess.restype=wintypes.BOOL
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.WaitForSingleObject.restype=wintypes.DWORD
    handle=kernel.OpenProcess(0x1000|0x0001|0x100000,False,pid)
    if not handle:raise RuntimeError('Nie udało się sprawdzić starej usługi głosu; nie zatrzymano procesu.')
    try:
        # Keep a process handle from before inspection through termination;
        # never terminate a later process that happens to reuse the PID.
        query=f"Get-CimInstance Win32_Process -Filter 'ProcessId = {pid}' | Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress"
        result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',query],
                              capture_output=True,text=True,encoding='utf8',errors='replace',
                              timeout=15,creationflags=subprocess.CREATE_NO_WINDOW,check=True)
        row=json.loads(result.stdout)
        if not worker_command_matches(row.get('ExecutablePath'),row.get('CommandLine'),python,script):
            raise RuntimeError('Stara usługa głosu ma inną tożsamość; nie zatrzymano procesu.')
        if gothic_running() or any((Path(root)/'requests').glob('*.json')):
            raise RuntimeError('Gra lub rozmowa jest już aktywna; aktualizacja usługi głosu poczeka.')
        if not kernel.TerminateProcess(handle,0):raise RuntimeError('Nie udało się zamknąć starej usługi głosu.')
        if kernel.WaitForSingleObject(handle,5000)!=0:raise RuntimeError('Stara usługa głosu nadal się zamyka.')
    finally:kernel.CloseHandle(handle)
