# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Loading the table and the generator on a bare interpreter.

Registration in sys.modules BEFORE exec_module is mandatory: dataclasses resolve string
annotations through sys.modules, and without the registration `from __future__ import
annotations` breaks the load.
"""
import importlib.util
import sys
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent.parent


def _load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="session")
def table():
    return _load("opentherm_ids")


@pytest.fixture(scope="session")
def gen():
    _load("opentherm_ids")
    return _load("generate_registry")
