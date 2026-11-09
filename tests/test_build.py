import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from build import prepare_sdk_headers


LEGACY_HEADER = (
    b'// Legacy SDK copyright \xa9\r\n'
    b'template <class MUTEX_TYPE> class CAutoLockT {\r\n'
    b'CAutoLockT<MUTEX_TYPE>( const CAutoLockT<MUTEX_TYPE> & );\r\n'
    b'CAutoLockT<MUTEX_TYPE> &operator=( const CAutoLockT<MUTEX_TYPE> & );\r\n'
    b'};\r\n'
)
CORRECTED_HEADER = LEGACY_HEADER.replace(
    b'CAutoLockT<MUTEX_TYPE>( const', b'CAutoLockT( const'
)


class SdkHeaderCompatibilityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='l4fixes-sdk-test-')
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.sdk = self.directory / 'sdk'
        self.build = self.directory / 'build'
        self.header = self.sdk / 'public/tier0/threadtools.h'
        self.header.parent.mkdir(parents=True)
        self.header.write_bytes(LEGACY_HEADER)

    def test_overlay_preserves_sdk_and_unrelated_declarations(self):
        overlay = prepare_sdk_headers(self.sdk, self.build)
        self.assertEqual(self.header.read_bytes(), LEGACY_HEADER)
        self.assertEqual((overlay / 'tier0/threadtools.h').read_bytes(), CORRECTED_HEADER)

    def test_current_sdk_replaces_stale_overlay_without_changes(self):
        overlay = prepare_sdk_headers(self.sdk, self.build)
        current_header = CORRECTED_HEADER + b'// Upstream update\r\n'
        self.header.write_bytes(current_header)
        self.assertEqual(prepare_sdk_headers(self.sdk, self.build), overlay)
        self.assertEqual((overlay / 'tier0/threadtools.h').read_bytes(), current_header)
        self.assertEqual(self.header.read_bytes(), current_header)

    def test_duplicate_legacy_declaration_is_rejected(self):
        self.header.write_bytes(LEGACY_HEADER + LEGACY_HEADER)
        with self.assertRaisesRegex(RuntimeError, 'Ambiguous CAutoLockT'):
            prepare_sdk_headers(self.sdk, self.build)

    def test_strict_cpp20_and_cpp23_constructor_syntax(self):
        overlay = prepare_sdk_headers(self.sdk, self.build)
        corrected_header = (overlay / 'tier0/threadtools.h').read_bytes()
        compiler = os.environ.get('CXX', 'c++')
        for standard in ['c++20', 'c++23']:
            with self.subTest(standard=standard):
                command = [compiler, '-std=' + standard, '-pedantic-errors',
                           '-x', 'c++', '-fsyntax-only', '-']
                legacy = subprocess.run(command, input=LEGACY_HEADER, capture_output=True)
                self.assertNotEqual(legacy.returncode, 0, 'Legacy declaration must reproduce the CI failure')
                corrected = subprocess.run(command, input=corrected_header, capture_output=True)
                self.assertEqual(corrected.returncode, 0, corrected.stderr.decode(errors='replace'))


if __name__ == '__main__':
    unittest.main()
