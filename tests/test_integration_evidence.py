# SPDX-License-Identifier: MIT
"""Recording-tool checks of the real public shell entries, not firmware tests."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class IntegrationEvidenceLifecycle(unittest.TestCase):
    def tool_environment(self, work):
        tools = work / 'bin'
        tools.mkdir()
        log = work / 'commands.jsonl'
        west = tools / 'west'
        west.write_text(
            '#!/usr/bin/env python3\n'
            'import json,os,sys\n'
            'with open(os.environ["RECORDING_TOOL_LOG"],"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n'
            'if sys.argv[1:2]==["manifest"]:\n'
            ' if os.environ.get("CONTROLLED_FREEZE_FAIL"): print("controlled freeze error",file=sys.stderr); sys.exit(23)\n'
            ' print("manifest: {projects: [{name: controlled, revision: 0123456789abcdef}]}")\n'
            'if sys.argv[1:2]==["build"]: print("controlled build failure",file=sys.stderr); sys.exit(19)\n',
            encoding='utf-8')
        west.chmod(0o755)
        dependencies = work / 'dependencies'
        dependencies.mkdir()
        env = dict(os.environ, RECORDING_TOOL_LOG=str(log), TMPDIR=str(dependencies),
                   PYTHONDONTWRITEBYTECODE='1')
        for name in ('ZMK_TEST_WORKSPACE', 'ZMK_TEST_REUSE_WORKSPACE',
                     'ZMK_TEST_OFFLINE_WORKSPACE', 'ZMK_TEST_BUILD_ROOT',
                     'ZMK_TEST_REUSE_BUILDS', 'ZMK_TEST_WORKSPACE_VOLUME',
                     'ZMK_TEST_LED_DEPLOY_KEY'):
            env.pop(name, None)
        env['PATH'] = str(tools) + os.pathsep + env['PATH']
        return tools, log, dependencies, env

    def run_entry(self, script, env, variant='upstream'):
        return subprocess.run(['bash', str(ROOT / script), variant], env=env,
                              capture_output=True, text=True, timeout=20)

    def test_wrapper_maps_fresh_output_and_rejects_reuse(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            tools, log, _dependencies, env = self.tool_environment(work)
            docker = tools / 'docker'
            docker.write_text(
                '#!/usr/bin/env python3\nimport json,os,sys\n'
                'with open(os.environ["RECORDING_TOOL_LOG"],"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n',
                encoding='utf-8')
            docker.chmod(0o755)
            output = work / 'retained'
            env['ZMK_TEST_OUTPUT'] = str(output)
            result = self.run_entry('tests/run-integration-docker.sh', env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            args = json.loads(log.read_text(encoding='utf-8').splitlines()[0])
            self.assertIn(str(output) + ':/results', args, args)
            self.assertIn('ZMK_TEST_OUTPUT=/results/build', args, args)
            self.assertTrue(output.is_dir())
            before = log.read_bytes()
            result = self.run_entry('tests/run-integration-docker.sh', env)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(log.read_bytes(), before, 'existing output invoked Docker')

    def test_online_failed_build_retains_graph_source_and_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            _tools, log, dependencies, env = self.tool_environment(work)
            output = work / 'retained'
            env['ZMK_TEST_OUTPUT'] = str(output)
            result = self.run_entry('tests/integration/run.sh', env)
            self.assertTrue(output.is_dir(), result.stdout + result.stderr)
            self.assertEqual(result.returncode, 19, result.stdout + result.stderr)
            self.assertIn('controlled build failure', (output / 'firmware.log').read_text())
            self.assertIn('0123456789abcdef', (output / 'west-manifest.yml').read_text())
            identity = json.loads((output / 'source-identity.json').read_text())
            self.assertIn('src/display_72x40.c', identity['files'])
            self.assertEqual(len(identity['files']['src/display_72x40.c']), 64)
            self.assertIn('west build', (output / 'commands.log').read_text())
            self.assertEqual(list(dependencies.iterdir()), [], 'temporary dependencies leaked')
            calls = [json.loads(row) for row in log.read_text().splitlines()]
            self.assertIn(['manifest', '--freeze', '--active-only'], calls)
            before = log.read_bytes()
            result = self.run_entry('tests/integration/run.sh', env)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(log.read_bytes(), before, 'existing evidence invoked west')

    def test_failed_graph_freeze_stops_before_build(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            _tools, log, dependencies, env = self.tool_environment(work)
            output = work / 'retained'
            env.update(ZMK_TEST_OUTPUT=str(output), CONTROLLED_FREEZE_FAIL='1')
            result = self.run_entry('tests/integration/run.sh', env)
            self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
            calls = [json.loads(row) for row in log.read_text().splitlines()]
            self.assertFalse(any(row[:1] == ['build'] for row in calls), calls)
            self.assertFalse((output / 'west-manifest.yml').exists())
            self.assertIn('controlled freeze error', (output / 'graph-freeze.stderr').read_text())
            self.assertEqual(list(dependencies.iterdir()), [])


    def test_public_dya_wrapper_needs_no_credentials(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            tools, log, _dependencies, env = self.tool_environment(work)
            docker = tools / 'docker'
            docker.write_text(
                '#!/usr/bin/env python3\nimport json,os,sys\n'
                'with open(os.environ["RECORDING_TOOL_LOG"],"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n',
                encoding='utf-8')
            docker.chmod(0o755)
            result = self.run_entry('tests/run-integration-docker.sh', env, 'dya')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            args = json.loads(log.read_text().splitlines()[0])
            self.assertEqual(args[-1], 'dya')
            self.assertFalse(any('/run/credentials/' in arg or
                                 arg.startswith(('GIT_SSH_COMMAND=', 'GIT_CONFIG_'))
                                 for arg in args), args)

    def test_private_key_mounts_are_repository_scoped_and_read_only(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            tools, log, _dependencies, env = self.tool_environment(work)
            docker = tools / 'docker'
            docker.write_text(
                '#!/usr/bin/env python3\nimport json,os,sys\n'
                'with open(os.environ["RECORDING_TOOL_LOG"],"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n',
                encoding='utf-8')
            docker.chmod(0o755)
            key = work / 'read-key'
            key.write_text('controlled noncredential sentinel', encoding='utf-8')
            output = work / 'retained'
            env.update(ZMK_TEST_LED_DEPLOY_KEY=str(key), ZMK_TEST_OUTPUT=str(output))
            result = self.run_entry('tests/run-integration-docker.sh', env, 'dya')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            args = json.loads(log.read_text().splitlines()[0])
            self.assertIn(str(key) + ':/run/credentials/led-key:ro', args)
            self.assertIn(str(ROOT / 'tests/integration/github-known-hosts') +
                          ':/run/credentials/known_hosts:ro', args)
            self.assertIn('GIT_CONFIG_VALUE_0=https://github.com/amgskobo/zmk-led-patterns', args)
            self.assertIn('GIT_CONFIG_KEY_0=url.ssh://git@github.com/amgskobo/zmk-led-patterns.insteadOf', args)
            ssh = next(arg for arg in args if arg.startswith('GIT_SSH_COMMAND='))
            self.assertIn('StrictHostKeyChecking=yes', ssh)
            self.assertIn('IdentitiesOnly=yes', ssh)
            self.assertNotIn('controlled noncredential sentinel',
                             result.stdout + result.stderr + json.dumps(args) +
                             (output / 'docker-command.txt').read_text())

    def test_missing_private_key_stops_before_docker(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            tools, log, _dependencies, env = self.tool_environment(work)
            docker = tools / 'docker'
            docker.write_text(
                '#!/usr/bin/env python3\nimport os\n'
                'open(os.environ["RECORDING_TOOL_LOG"],"w").write("unexpected Docker invocation")\n',
                encoding='utf-8')
            docker.chmod(0o755)
            env.update(ZMK_TEST_LED_DEPLOY_KEY=str(work / 'absent-key'))
            result = self.run_entry('tests/run-integration-docker.sh', env, 'dya')
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(log.exists(), 'missing key invoked Docker/network')


if __name__ == '__main__':
    unittest.main()
