# SPDX-License-Identifier: MIT
"""Execute the CI dependency-access entry; no network or credential is required."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENTRY = Path(os.environ.get('DISPLAY_LED_ACCESS_ENTRY',
                            ROOT / 'tests/integration/prepare-led-access.sh'))


class LedDependencyAccess(unittest.TestCase):
    def environment(self, work):
        env = dict(os.environ, RUNNER_TEMP=str(work / 'runner'),
                   GITHUB_ENV=str(work / 'github-env'))
        env.pop('LED_READ_KEY', None)
        env.pop('ZMK_TEST_LED_DEPLOY_KEY', None)
        (work / 'runner').mkdir()
        (work / 'github-env').write_text('existing=value\n', encoding='utf-8')
        return env

    def run_entry(self, work, env):
        return subprocess.run(['bash', str(ENTRY)], cwd=work, env=env,
                              capture_output=True, text=True, timeout=10)

    def test_public_no_key(self):
        for supplied in (False, True):
            with self.subTest(empty_key=supplied), tempfile.TemporaryDirectory() as folder:
                work = Path(folder)
                env = self.environment(work)
                if supplied:
                    env['LED_READ_KEY'] = ''
                result = self.run_entry(work, env)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('HTTPS', (work / 'ci-results/dependency-access.txt').read_text())
                self.assertEqual((work / 'github-env').read_text(), 'existing=value\n')
                self.assertEqual(list((work / 'runner').iterdir()), [])

    def test_explicit_key_is_private_and_not_logged(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            env = self.environment(work)
            sentinel = 'controlled noncredential sentinel'
            env['LED_READ_KEY'] = sentinel
            result = self.run_entry(work, env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            key = work / 'runner/display-led-read-key'
            self.assertEqual(key.read_text(), sentinel + '\n')
            self.assertEqual(key.stat().st_mode & 0o777, 0o600)
            self.assertIn('ZMK_TEST_LED_DEPLOY_KEY=' + str(key) + '\n',
                          (work / 'github-env').read_text())
            self.assertNotIn(sentinel, result.stdout + result.stderr +
                             (work / 'github-env').read_text() +
                             (work / 'ci-results/dependency-access.txt').read_text())

    def test_explicit_key_requires_runner_paths(self):
        for missing in ('RUNNER_TEMP', 'GITHUB_ENV'):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as folder:
                work = Path(folder)
                env = self.environment(work)
                sentinel = 'controlled noncredential sentinel'
                env['LED_READ_KEY'] = sentinel
                env.pop(missing)
                result = self.run_entry(work, env)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(missing + ' is required', result.stderr)
                self.assertNotIn(sentinel, result.stdout + result.stderr)
                self.assertEqual(list((work / 'runner').iterdir()), [])
