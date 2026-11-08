"""시험 진입점. tools/ready_smoke.py는 실물 시험에도 사용할 수 있다."""
from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1]/'tools/ready_smoke.py'),
                '--simulate', *sys.argv[1:]], check=True)
