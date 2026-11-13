#!/usr/bin/env python3
"""Build and package the Linux i386 VSP using only an HL2SDK-L4D2 checkout (headers and lib/linux)."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def linker_accepts(cxx, flag):
    with tempfile.TemporaryDirectory(prefix='l4fixes-probe-') as directory:
        command = [cxx, '-m32', '-shared', '-x', 'c++', '-', '-o', str(Path(directory) / 'probe.so'), flag]
        return subprocess.run(command, input=b'int probe;\n', capture_output=True).returncode == 0


def run(command):
    print(shlex.join(str(argument) for argument in command), flush=True)
    subprocess.run([str(argument) for argument in command], check=True)


def prepare_sdk_headers(sdk, build):
    # Older SDKs spell a copy constructor with a template-id, which is invalid
    # in C++20. Correct only that declaration in a build-local header overlay.
    header = sdk / 'public/tier0/threadtools.h'
    original = header.read_bytes()
    legacy_declaration = b'CAutoLockT<MUTEX_TYPE>( const CAutoLockT<MUTEX_TYPE> & );'
    corrected_declaration = b'CAutoLockT( const CAutoLockT<MUTEX_TYPE> & );'
    if original.count(legacy_declaration) > 1:
        raise RuntimeError('Ambiguous CAutoLockT copy constructor in {}'.format(header))
    overlay = build / 'sdk_compat'
    destination = overlay / 'tier0/threadtools.h'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(original.replace(legacy_declaration, corrected_declaration))
    return overlay


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path, help='HL2SDK checkout on its l4d2 branch')
    parser.add_argument('--build-dir', default=Path('build'), type=Path)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--std', choices=['c++20', 'c++23'], default='c++20')
    parser.add_argument('--shared-runtime', action='store_true',
                        help='Link libstdc++ and libgcc dynamically (default: static)')
    parser.add_argument('--tests', action='store_true', help='Build and run sanitized regression tests')
    options = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('Only Linux builds are supported')
    repository = Path(__file__).resolve().parent
    sdk = options.sdk.resolve()
    build = options.build_dir.resolve()
    if not (sdk / 'public/eiface.h').is_file():
        parser.error('--sdk must point to an HL2SDK-L4D2 source checkout')
    build.mkdir(parents=True, exist_ok=True)
    flags = [
        '-m32', '-std=' + options.std, '-fPIC', '-fvisibility=hidden',
        '-fno-strict-aliasing', '-Wall', '-Wextra', '-Werror',
        '-DLINUX', '-D_LINUX', '-DPOSIX', '-DGNUC', '-DNO_MALLOC_OVERRIDE',
        '-DNO_HOOK_MALLOC', '-Dstricmp=strcasecmp', '-D_stricmp=strcasecmp',
        '-D_snprintf=snprintf', '-D_vsnprintf=vsnprintf', '-I' + str(repository / 'src'),
    ]
    compiler_version = subprocess.run([options.cxx, '--version'], check=True, capture_output=True, text=True).stdout
    if 'clang' in compiler_version.lower():
        # Recent Clang rejects unused legacy SDK container templates eagerly.
        flags += ['-fdelayed-template-parsing', '-Wno-delayed-template-parsing-in-cxx20',
                  '-Wno-unknown-warning-option']
    flags += ['-isystem', str(prepare_sdk_headers(sdk, build))]
    for directory in ['public', 'public/engine', 'public/tier0', 'public/tier1', 'public/game/server', 'common']:
        flags += ['-isystem', str(sdk / directory)]
    # te_reliable_sound is a tier1 ConCommand. tier1 is linked statically; tier0 and vstdlib are the
    # libraries srcds has already loaded, so the plugin only records their names.
    sdk_libraries = sdk / 'lib/linux'
    sdk_link = [sdk_libraries / 'tier1_i486.a', '-L' + str(sdk_libraries),
                '-l:libtier0_srv.so', '-l:libvstdlib_srv.so']
    if options.tests:
        run([sys.executable, repository / 'tests/test_build.py'])
    sources = ['module', 'patch', 'fixes', 'reliablesound', 'plugin']
    objects = []
    for name in sources:
        target = build / (name + '.o')
        run([options.cxx] + flags + ['-O2', '-DNDEBUG', '-c', repository / 'src' / (name + '.cpp'), '-o', target])
        objects.append(target)
    package = build / 'package'
    library_directory = package / 'addons/l4fixes_vsp'
    library_directory.mkdir(parents=True, exist_ok=True)
    library = library_directory / 'l4fixes_vsp.so'
    runtime_flags = ['-shared-libgcc'] if options.shared_runtime else ['-static-libstdc++', '-static-libgcc']
    # binutils >= 2.45 linking against glibc >= 2.42 adds a GLIBC_ABI_GNU_TLS version dependency to
    # i386 objects using TLS (the static libstdc++ does); servers with an older glibc then refuse to
    # load the plugin. Builds made with older binutils never have it.
    if linker_accepts(options.cxx, '-Wl,--no-gnu-tls-tag'):
        runtime_flags.append('-Wl,--no-gnu-tls-tag')
    run([options.cxx, '-m32', '-shared', '-Wl,-z,defs', '-Wl,-z,relro,-z,now',
         '-Wl,--version-script=' + str(repository / 'src/exports.map'),
         '-o', library] + objects + sdk_link + runtime_flags + ['-ldl'])
    shutil.copy2(repository / 'extra/l4fixes_vsp.vdf', package / 'addons/l4fixes_vsp.vdf')
    shutil.copy2(repository / 'LICENSE.txt', package / 'LICENSE.txt')
    shutil.copy2(repository / 'README.MD', package / 'README.MD')
    if options.tests:
        fixture = build / 'fixes_fixture.so'
        run([options.cxx, '-m32', '-shared', '-fPIC', '-Wl,-z,noexecstack',
             repository / 'tests/fixes_fixture.S', '-o', fixture])
        executable = build / 'runtime_tests'
        test_sources = [repository / 'src' / (name + '.cpp') for name in sources if name != 'plugin']
        run([options.cxx] + flags + ['-g', '-fsanitize=undefined', '-fno-sanitize-recover=all',
             repository / 'tests/runtime_tests.cpp', repository / 'tests/reliablesound_tests.cpp']
            + test_sources + sdk_link + ['-Wl,-rpath,' + str(sdk_libraries), '-ldl', '-o', executable])
        run([executable, fixture, library])
    print('Package: {}'.format(package))


if __name__ == '__main__':
    main()
