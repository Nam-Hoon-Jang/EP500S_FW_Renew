"""실제 SSH/지우기를 실행하지 않고, 원격 플래시의 중단 조건을 검사한다."""
import ast
import hashlib
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch, MagicMock


tree=ast.parse((Path(__file__).parents[1]/'tools/flash_remote.py').read_text(encoding='utf-8-sig'))
remote_code=next(n.value.value for n in ast.walk(tree) if isinstance(n,ast.Assign)
    and any(isinstance(t,ast.Name) and t.id=='code' for t in n.targets))
compile(remote_code,'remote-flash','exec')


class FlashTests(unittest.TestCase):
    def run_remote(self, *, bad_hash=False, busy=False, wrong_chip=False, missing_port=False):
        with tempfile.TemporaryDirectory() as temporary:
            image=Path(temporary)/'fw.bin'
            image.write_bytes(b'test firmware')
            digest=hashlib.sha256(image.read_bytes()).hexdigest()
            serial_mock=MagicMock()
            def run(command,**kwargs):
                return types.SimpleNamespace(returncode=0 if busy and command[0]=='fuser' else 1)
            with patch.object(sys,'argv',['remote',str(image),'wrong' if bad_hash else digest,'/dev/ttyACM0']), \
                 patch('subprocess.run',side_effect=run) as execute, \
                 patch('subprocess.check_output',return_value='Device : '+('ATSAM4S' if wrong_chip else 'ATSAM3X8')), \
                 patch('shutil.which',side_effect=lambda name:'/usr/bin/'+name), \
                 patch('os.path.exists',return_value=not missing_port), \
                 patch('time.sleep'), \
                 patch.dict(sys.modules,{'serial':types.SimpleNamespace(Serial=serial_mock)}):
                failure=None
                try:
                    exec(compile(remote_code,'remote-flash','exec'),{})
                except SystemExit as error:
                    failure=str(error)
                calls=[c.args[0] for c in execute.call_args_list]
                return failure,calls,serial_mock.call_count

    def test_hash_mismatch_never_stops_or_erases(self):
        failure,calls,opens=self.run_remote(bad_hash=True)
        self.assertIn('SHA256',failure)
        self.assertEqual(calls,[])
        self.assertEqual(opens,0)

    def test_missing_port_never_stops_or_erases(self):
        failure,calls,opens=self.run_remote(missing_port=True)
        self.assertIn('시리얼',failure)
        self.assertEqual(calls,[])
        self.assertEqual(opens,0)

    def test_busy_port_never_opens_for_erase(self):
        failure,calls,opens=self.run_remote(busy=True)
        self.assertIn('사용 중',failure)
        self.assertEqual(opens,0)
        self.assertFalse(any('-w' in command for command in calls))

    def test_wrong_chip_never_writes_image(self):
        failure,calls,opens=self.run_remote(wrong_chip=True)
        self.assertIn('대상 확인',failure)
        self.assertEqual(opens,1)
        self.assertFalse(any('-w' in command for command in calls))

    def test_success_uses_verified_file_and_keeps_klippy_stopped(self):
        failure,calls,opens=self.run_remote()
        self.assertIsNone(failure)
        self.assertEqual(opens,1)
        writes=[command for command in calls if '-w' in command]
        self.assertEqual(len(writes),1)
        self.assertIn('-v',writes[0])
        self.assertIn('-b',writes[0])
        self.assertIn('-R',writes[0])
        self.assertFalse(any('start' in command or 'restart' in command for command in calls))


if __name__=='__main__':
    unittest.main()
