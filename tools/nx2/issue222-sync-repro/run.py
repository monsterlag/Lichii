"""Run only the task-created executable in an isolated portable profile.

The output directory must be fresh. Nothing is deleted, and no system setting
or existing profile is changed. subprocess timeout applies only to our child.
"""
from pathlib import Path
import json
import os
import subprocess
import sys

executable = Path(sys.argv[1]).resolve(strict=True)
portable = executable.parent / "user"
portable.mkdir()  # Refuse reuse of any existing directory/profile.
environment = os.environ.copy()
for variable in ("HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_CONFIG_HOME"):
    destination = portable / variable.lower()
    destination.mkdir()
    environment[variable] = str(destination)
command = [str(executable), str(portable)]
output = executable.parent
(output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
try:
    result = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                            text=True, timeout=15)
except subprocess.TimeoutExpired as error:
    (output / "result.json").write_text(json.dumps({"timeout_seconds": 15}) + "\n")
    raise SystemExit("Private child exceeded 15s; no observed reproduction result") from error
(output / "stdout.log").write_text(result.stdout)
(output / "stderr.log").write_text(result.stderr)
(output / "result.json").write_text(json.dumps({"exit_code": result.returncode,
    "source_base": "01442dca1e8539da8b59061c5583bfcfba5d2c7a",
    "diagnostic_build_head": environment.get("LICHII_DIAGNOSTIC_SOURCE_HEAD")}, indent=2) + "\n")
print(result.stdout, end="")
print(result.stderr, end="", file=sys.stderr)
raise SystemExit(result.returncode)
