#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Pre-build esp_board_manager board codes for a single ESP-IDF example (CI).

Mirrors esp-gmf/tools/ci/esp_bmgr_pre_build.py for one app path:

  1. idf.py [--preview] set-target <target>   (downloads managed_components,
     writes build/project_description.json)
  2. if the app depends on esp_board_manager:
         idf.py [--preview] bmgr -b <board>    (generates components/gen_bmgr_codes)

The bmgr action is provided by managed_components/espressif__esp_board_manager
via IDF_EXTRA_ACTIONS_PATH. As in esp-gmf, no -B/-D build flags are passed to
set-target or bmgr: they run in the app's default build dir, and gen_bmgr_codes
is emitted into the source tree so any later build dir (e.g. build_<target>)
picks it up.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

PREVIEW_TARGETS = frozenset({'esp32s31'})
BMGR_ALIASES = ('esp_board_manager', 'espressif__esp_board_manager')


def _idf_py_cmd(target: str, *idf_args: str) -> list[str]:
    if target in PREVIEW_TARGETS:
        return ['idf.py', '--preview', *idf_args]
    return ['idf.py', *idf_args]


def _find_bmgr_actions_path(app_path: Path) -> Path | None:
    for name in BMGR_ALIASES:
        path = app_path / 'managed_components' / name
        if (path / 'idf_ext.py').is_file():
            return path
    return None


def _run(app_path: Path, cmd: list[str], env: dict[str, str] | None = None) -> None:
    print(f'Running: {" ".join(cmd)} (cwd={app_path})')
    result = subprocess.run(cmd, cwd=str(app_path), env=env, check=False)
    if result.returncode != 0:
        sys.exit(result.returncode)


def _depends_on_bmgr(app_path: Path) -> bool:
    project_desc = app_path / 'build' / 'project_description.json'
    if not project_desc.is_file():
        print(f'Warning: {project_desc} not found, skip bmgr')
        return False

    with open(project_desc, encoding='utf-8') as handle:
        build_components = json.load(handle).get('build_components', [])

    if any(alias in build_components for alias in BMGR_ALIASES):
        return True

    print('Info: esp_board_manager not in build_components, skip bmgr')
    return False


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Run set-target and bmgr for one ESP-IDF example before CI build',
    )
    parser.add_argument('--target', required=True, help='IDF target chip (e.g. esp32s31)')
    parser.add_argument('--board', required=True, help='Board name (e.g. esp32_s31_korvo_1)')
    parser.add_argument('--app-path', required=True, help='Absolute path to the example project')
    args = parser.parse_args()

    app_path = Path(args.app_path).resolve()
    if not app_path.is_dir():
        print(f'Error: app path does not exist: {app_path}', file=sys.stderr)
        sys.exit(1)

    _run(app_path, _idf_py_cmd(args.target, 'set-target', args.target))

    if not _depends_on_bmgr(app_path):
        return

    bmgr_path = _find_bmgr_actions_path(app_path)
    if bmgr_path is None:
        print(
            'Error: esp_board_manager is required but managed_components/.../idf_ext.py '
            'was not found. Ensure set-target completed and dependencies were downloaded.',
            file=sys.stderr,
        )
        sys.exit(1)

    env = os.environ.copy()
    env['IDF_EXTRA_ACTIONS_PATH'] = str(bmgr_path)
    print(f'IDF_EXTRA_ACTIONS_PATH={bmgr_path}')

    _run(app_path, _idf_py_cmd(args.target, 'bmgr', '-b', args.board), env=env)


if __name__ == '__main__':
    main()
