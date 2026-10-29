import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock,patch
import worker_update as update


class WorkerUpdateTests(unittest.TestCase):
    def test_only_exact_project_worker_command_is_accepted(self):
        python=Path(r'C:\Runtime\python.exe');script=Path(r'C:\Project\backend\start_backend.py')
        command=f'"{python}" -X utf8 -u "{script}" --worker --mode live_model'
        self.assertTrue(update.worker_command_matches(str(python),command,python,script))
        for altered in (command.replace('--worker','--other'),command+' extra',command.replace('live_model','other'),command.replace('start_backend.py','other.py')):
            self.assertFalse(update.worker_command_matches(str(python),altered,python,script))
        self.assertFalse(update.worker_command_matches(r'C:\Other\python.exe',command,python,script))

    def test_game_or_pending_dialogue_prevents_process_access(self):
        with tempfile.TemporaryDirectory() as tmp,patch.object(update.ctypes,'WinDLL') as kernel:
            root=Path(tmp);(root/'requests').mkdir()
            with patch.object(update,'gothic_running',return_value=True):
                with self.assertRaises(RuntimeError):update.restart_outdated_worker({'pid':123},root,'python','script')
            (root/'requests/request.json').write_text('{}')
            with patch.object(update,'gothic_running',return_value=False):
                with self.assertRaises(RuntimeError):update.restart_outdated_worker({'pid':123},root,'python','script')
            kernel.assert_not_called()

    def test_venv_base_redirect_requires_its_exact_configured_interpreter(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);venv=root/'venv';base=root/'base';base.mkdir();(base/'python.exe').touch()
            (venv/'Scripts').mkdir(parents=True);python=venv/'Scripts/python.exe';python.touch()
            (venv/'pyvenv.cfg').write_text('home = '+str(base),encoding='utf8')
            script=root/'start_backend.py';command=f'"{base / "python.exe"}" -X utf8 -u "{script}" --worker --mode live_model'
            self.assertTrue(update.worker_command_matches(str(base/'python.exe'),command,python,script))
            self.assertFalse(update.worker_command_matches(str(root/'other/python.exe'),command,python,script))

    def test_verified_handle_is_closed_without_kill_if_game_starts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'requests').mkdir();kernel=MagicMock()
            kernel.OpenProcess.return_value=42
            row=MagicMock(stdout=json.dumps({'ExecutablePath':'python','CommandLine':'script'}))
            with patch.object(update.ctypes,'WinDLL',return_value=kernel),patch.object(update,'gothic_running',side_effect=[False,True]),patch.object(update.subprocess,'run',return_value=row),patch.object(update,'worker_command_matches',return_value=True):
                with self.assertRaises(RuntimeError):update.restart_outdated_worker({'pid':123},root,'python','script')
            kernel.TerminateProcess.assert_not_called();kernel.CloseHandle.assert_called_once_with(42)

    def test_only_verified_idle_worker_is_stopped(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'requests').mkdir();kernel=MagicMock()
            kernel.OpenProcess.return_value=42;kernel.TerminateProcess.return_value=1;kernel.WaitForSingleObject.return_value=0
            row=MagicMock(stdout=json.dumps({'ExecutablePath':'python','CommandLine':'script'}))
            with patch.object(update.ctypes,'WinDLL',return_value=kernel),patch.object(update,'gothic_running',return_value=False),patch.object(update.subprocess,'run',return_value=row),patch.object(update,'worker_command_matches',return_value=True):
                update.restart_outdated_worker({'pid':123},root,'python','script')
            kernel.TerminateProcess.assert_called_once_with(42,0);kernel.CloseHandle.assert_called_once_with(42)

    def test_wrong_identity_and_invalid_pid_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'requests').mkdir();kernel=MagicMock();kernel.OpenProcess.return_value=42
            row=MagicMock(stdout=json.dumps({'ExecutablePath':'wrong','CommandLine':'wrong'}))
            with patch.object(update.ctypes,'WinDLL',return_value=kernel),patch.object(update,'gothic_running',return_value=False),patch.object(update.subprocess,'run',return_value=row),patch.object(update,'worker_command_matches',return_value=False):
                with self.assertRaises(RuntimeError):update.restart_outdated_worker({'pid':123},root,'python','script')
                for value in (True,-1,'123'):
                    with self.assertRaises(RuntimeError):update.restart_outdated_worker({'pid':value},root,'python','script')
            kernel.TerminateProcess.assert_not_called()


if __name__=='__main__':unittest.main()
