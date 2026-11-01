import http.server
import json
from pathlib import Path
import socket
import subprocess
import sys
import threading
import unittest
import urllib.parse
import uuid


APP = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
METADATA = {}
for path in (ROOT / 'examples' / 'metadata').glob('*.json'):
    if path.name != 'sources.json':
        document = json.loads(path.read_text(encoding='utf-8'))
        versions = METADATA.setdefault(document['name'], {})
        versions[document['version']] = document


class RegistryHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format_string, *args):
        pass

    def do_GET(self):
        parts = self.path.strip('/').split('/')
        mode = parts.pop(0) if parts[0] in {'broken', 'wrong', 'error'} else ''
        if mode == 'error':
            self.send_error(503)
            return
        name = urllib.parse.unquote(parts[0])
        versions = METADATA.get(name)
        if not versions or (len(parts) > 1 and parts[1] not in versions):
            self.send_error(404)
            return
        document = (versions[parts[1]] if len(parts) > 1 else
                    {'name': name, 'versions': versions})
        if mode == 'wrong':
            document = dict(document, version='999.0.0')
        data = (b'{' if mode == 'broken' else
                json.dumps(document).encode('utf-8'))
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


class CliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = APP.parent / ('graph-test-' + uuid.uuid4().hex)
        cls.directory.mkdir()
        cls.server = http.server.ThreadingHTTPServer(
            ('127.0.0.1', 0), RegistryHandler)
        cls.thread = threading.Thread(target=cls.server.serve_forever)
        cls.thread.start()
        cls.url = f'http://127.0.0.1:{cls.server.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.thread.join()
        cls.server.server_close()
        if cls.directory.resolve().is_relative_to(APP.parent):
            for file in cls.directory.iterdir():
                file.unlink()
            cls.directory.rmdir()

    def invoke(self, *arguments):
        return subprocess.run(
            [str(APP), *arguments], cwd=self.directory,
            text=True, encoding='utf-8', capture_output=True, timeout=30)

    def npm(self, name='chalk', version='4.1.2', suffix=''):
        return self.invoke('--package', name, '--version', version,
                           '--repository', self.url + suffix, '--direct')

    def test_direct_dependencies(self):
        result = self.npm()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, '')
        self.assertEqual(result.stdout, 'package=chalk@4.1.2\n'
                         'direct_dependencies=2\nansi-styles=^4.1.0\n'
                         'supports-color=^7.1.0\n')

    def test_leaf(self):
        result = self.npm('is-number', '7.0.0')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('direct_dependencies=0', result.stdout)

    def test_scoped_package(self):
        result = self.npm('@types/is-number', '7.0.5')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('package=@types/is-number@7.0.5', result.stdout)

    def test_missing_package_or_version(self):
        for name, version in [('absent', '1.0.0'), ('chalk', '999.0.0')]:
            with self.subTest(name=name, version=version):
                result = self.npm(name, version)
                self.assertEqual(result.returncode, 2)
                self.assertIn('HTTP 404', result.stderr)
                self.assertEqual(result.stdout, '')

    def test_server_errors(self):
        for suffix in ['/broken', '/wrong', '/error']:
            with self.subTest(suffix=suffix):
                result = self.npm(suffix=suffix)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, '')
                self.assertTrue(result.stderr)

    def test_test_repository_and_unicode_path(self):
        file = self.directory / 'граф с пробелами.json'
        file.write_text('{"A":["B"],"B":[]}', encoding='utf-8')
        result = self.invoke('--package', 'A', '--version', '1.0.0',
                             '--test-mode', '--repository', str(file),
                             '--direct')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('B=1.0.0', result.stdout)

    def test_invalid_test_repositories(self):
        file = self.directory / 'invalid.json'
        for text in ['{', '{"A":["B"]}', '{"a":[]}', '{"A":{}}']:
            with self.subTest(text=text):
                file.write_text(text, encoding='utf-8')
                result = self.invoke('--package', 'A', '--version', '1.0.0',
                                     '--test-mode', '--repository', str(file))
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, '')

    def test_help_and_validation(self):
        result = self.invoke('--help')
        self.assertEqual(result.returncode, 0)
        self.assertIn('--package', result.stdout)
        result = self.invoke('--package', 'chalk', '--version', 'invalid')
        self.assertEqual(result.returncode, 2)
        self.assertIn('--version', result.stderr)
        self.assertEqual(self.invoke('--direct=false').returncode, 2)

    def test_unavailable_registry(self):
        with socket.socket() as reserved:
            reserved.bind(('127.0.0.1', 0))
            port = reserved.getsockname()[1]
            result = self.invoke('--package', 'chalk', '--version', '4.1.2',
                                 '--repository', f'http://127.0.0.1:{port}')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, '')
        self.assertTrue(result.stderr)

    def graph(self, file, *arguments):
        return self.invoke('--package', 'A', '--version', '1.0.0',
                           '--test-mode', '--repository',
                           str(ROOT / 'examples' / file), *arguments)

    def test_recursive_bfs_and_diamond(self):
        result = self.graph('branching.json')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('nodes=4\nedges=4\n', result.stdout)
        self.assertIn('bfs=A@1.0.0,B@1.0.0,C@1.0.0,D@1.0.0', result.stdout)

    def test_cycles_and_isolated_vertices(self):
        for file, nodes, edges in [('cycle.json', 3, 3),
                                   ('self-loop.json', 1, 1),
                                   ('isolated.json', 1, 0)]:
            with self.subTest(file=file):
                result = self.graph(file)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f'nodes={nodes}\nedges={edges}', result.stdout)

    def test_filter(self):
        result = self.graph('branching.json', '--filter', 'B')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('nodes=3\nedges=2', result.stdout)
        self.assertNotIn('B@1.0.0', result.stdout)
        self.assertIn('D@1.0.0', result.stdout)

    def test_filtered_root_does_not_contact_registry(self):
        result = self.invoke('--package', 'absent', '--version', '1.0.0',
                             '--repository', self.url, '--filter', 'abs')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('nodes=0\nedges=0', result.stdout)

    def test_npm_graph_from_local_http_metadata(self):
        result = self.invoke('--package', 'chalk', '--version', '4.1.2',
                             '--repository', self.url)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('nodes=6\nedges=5', result.stdout)
        self.assertIn('color-name@1.1.4 -> -', result.stdout)

    def test_stage_five_outputs_are_not_created(self):
        output = self.directory / 'граф.svg'
        result = self.graph('cycle.json', '--ascii', '--output', str(output))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
