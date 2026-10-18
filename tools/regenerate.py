# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""PlatformIO pre-build hook: the registry is regenerated, not taken on trust.

A stale registry does NOT break the build -- it builds and travels to the boiler as
firmware whose entities diverge from the table. Hence regeneration here, not a check.
"""
import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821 -- injected by PlatformIO/SCons

ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
generator = ROOT / "tools" / "generate_registry.py"

# sys.executable, not "python3" from PATH: the same interpreter that started SCons.
result = subprocess.run([sys.executable, str(generator)], cwd=ROOT,
                        capture_output=True, text=True)
if result.returncode != 0:
    print(result.stdout)
    print(result.stderr, file=sys.stderr)
    raise SystemExit("registry generation failed")
print(result.stdout.strip())
