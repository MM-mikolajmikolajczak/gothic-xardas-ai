"""Identity and failure-path tests; never launch the real model."""
import io
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import MagicMock, patch
import start_backend as startup

class StartupTests(unittest.TestCase):
    def test_dead_worker_with_recent_heartbeat_is_not_reused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            row={'ready':True,'pid':12345,'heartbeat_utc':startup.time.time()}
            (root/'worker_ready.json').write_text(json.dumps(row))
            with patch.object(startup,'ROOT',root):
                with patch.object(startup,'worker_alive',return_value=False):self.assertIsNone(startup.current_worker())
                with patch.object(startup,'worker_alive',return_value=True):self.assertEqual(startup.current_worker(),row)

    def test_worker_process_listing_matches_pid_and_python(self):
        for listing,expected in [('"python.exe","12345","Console","1","20 K"',True),
                ('"python.exe","54321","Console","1","20 K"',False),
                ('"other.exe","12345","Console","1","20 K"',False),('INFO: no tasks',False)]:
            with patch.object(startup.subprocess,'run',return_value=MagicMock(stdout=listing)):
                self.assertEqual(startup.worker_alive(12345),expected)
        self.assertFalse(startup.worker_alive(True))

    def test_endpoint_identity(self):
        def response(alias):
            return io.BytesIO(json.dumps({'model_path':str(startup.MODEL),'model_alias':alias}).encode())
        with patch.object(startup.urllib.request,'urlopen',return_value=response('nemotron-bolt')):
            self.assertTrue(startup.model_ready())
        with patch.object(startup.urllib.request,'urlopen',return_value=response('wrong')):
            with self.assertRaises(RuntimeError):startup.model_ready()

    def test_timeout_refusal_require_available_port(self):
        for reason in (TimeoutError('timeout'),ConnectionRefusedError()):
            with patch.object(startup.urllib.request,'urlopen',side_effect=urllib.error.URLError(reason)):
                with patch.object(startup,'port_available',return_value=True):self.assertFalse(startup.model_ready())
                with patch.object(startup,'port_available',return_value=False):
                    with self.assertRaises(RuntimeError):startup.model_ready()

    def test_http_error_and_unknown_error_fail_closed(self):
        error=urllib.error.HTTPError('url',503,'busy',{},None)
        with patch.object(startup.urllib.request,'urlopen',side_effect=error):
            with self.assertRaises(RuntimeError):startup.model_ready()
            self.assertFalse(startup.model_ready(starting=True))
        with patch.object(startup.urllib.request,'urlopen',side_effect=urllib.error.URLError('unknown')):
            with self.assertRaises(RuntimeError):startup.model_ready()

    def test_wrong_model_hash_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            model=Path(tmp)/'model';model.write_bytes(b'wrong')
            with patch.object(startup,'MODEL',model):
                with self.assertRaisesRegex(RuntimeError,'hash mismatch'):startup.verify_model()

    def test_spawn_pid_arguments_and_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);exe=root/'runner'/'llama-server.exe';model=root/'models'/'model.gguf'
            exe.parent.mkdir();model.parent.mkdir();exe.touch();model.touch()
            child=MagicMock(pid=12345);child.poll.return_value=None
            with patch.object(startup,'HERE',root),patch.object(startup,'RUNNER',exe),patch.object(startup,'MODEL',model),patch.object(startup,'model_ready',side_effect=[False,True]),patch.object(startup.subprocess,'Popen',return_value=child) as pop:
                startup.start_model();args=pop.call_args.args[0]
                self.assertEqual(args[:5],[str(exe),'-m',str(model),'--alias','nemotron-bolt'])
                self.assertIn('--skip-chat-parsing',args);self.assertEqual((root/'runner.pid').read_text(),'12345')
            with patch.object(startup,'model_ready',return_value=True),patch.object(startup.subprocess,'Popen') as pop:
                startup.start_model();pop.assert_not_called()

if __name__=='__main__':unittest.main()
