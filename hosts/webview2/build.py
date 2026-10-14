"""StarryFish standalone Windows build. Never opens user profiles."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
APP = ROOT.parents[1] / 'app'
SDK = ROOT / '.sdk/1.0.4258.31'
BUILD = ROOT / '.build'
SDK_MEMBERS = {
    'WebView2.h': 'dff1e3181ec7ec203a34ef6efa966590e0ef0ba1a5c3fe3b69da6508c2f8a02e',
    'WebView2EnvironmentOptions.h': '73566559e49911301b9c34db59b547343c81ab06ac6428a39ec0204956b37118',
    'WebView2LoaderStatic.lib': 'e66284970876a616c82e7dcda75a17fd46d04827f9de81d4299b8862d34d25d6',
    'LICENSE.txt': '0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55',
}
sys.path.insert(0, str(APP / 'tools'))
import component_package as package
import component_stage as observation
import payload_directory as publication


def vs_root():
    finder = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    path = subprocess.check_output([str(finder), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not path: raise ValueError('Existing VS2022 C++ x64 tools required')
    return Path(path)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def sha(path):
    path = Path(path)
    observation._local_path(path)
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def source_inputs():
    values = {p.relative_to(ROOT).as_posix(): sha(p) for p in
              [ROOT / 'CMakeLists.txt', ROOT / 'build.py', *sorted((ROOT / 'src').rglob('*')), *sorted((ROOT / 'setup').rglob('*')),
               *sorted((ROOT / 'checks').rglob('*'))] if p.is_file()}
    # Capture the executed verification/publication code as well as the linked
    # C++ library. The CEF receipt alone is not an identity of today's helpers.
    for path in [*sorted((APP / 'tools').glob('*.py')), *sorted((APP / 'tests').glob('*'))]:
        values['app/' + path.relative_to(APP).as_posix()] = sha(path)
    for name, expected in SDK_MEMBERS.items():
        if sha(SDK / name) != expected:
            raise ValueError('official SDK identity changed')
        values['sdk/' + name] = expected
    for path in [APP / 'CMakeLists.txt', *sorted((APP / 'src').rglob('*')), *sorted((ROOT.parents[1] / 'assets').glob('*')), *sorted((ROOT.parents[1] / 'licenses').glob('*')), ROOT.parents[1] / 'VERSION', ROOT.parents[1] / 'LICENSE', ROOT.parents[1] / 'THIRD_PARTY_NOTICES.md']:
        if path.is_file(): values['project/' + path.relative_to(ROOT.parents[1]).as_posix()] = sha(path)
    values['shared-proxy-script'] = sha(APP / 'src/proxy_initialization.js')
    for name in ('gbf-bookmark-0.2.8.user.js', 'gbf-translation-2.9.5.user.js', 'GBF-Bookmark-Config.json'):
        values['shared-script/' + name] = sha(APP / 'src/bundled_assets' / name)
    return values


def fingerprint_inputs():
    values = source_inputs()
    for name in ('components.h', 'proxy_scripts.h'):
        values[name] = sha(BUILD / 'generated' / name)
    return values


def verify():
    record = json.loads((BUILD / 'build-receipt.json').read_text())
    package.require(record['inputs'] == fingerprint_inputs(), 'native candidate inputs changed; rebuild required')
    package.require(sha(BUILD / 'native/Release/StarryFish.exe') == record['executable_sha256'],
                    'native executable identity changed')
    package.require(sha(BUILD / 'native/Release/NativeSourceChecks.exe') == record['source_checks_sha256'],
                    'native source check identity changed')
    package.require(sha(BUILD / 'native/Release/UiThemePreview.exe') == record['ui_preview_sha256'],
                    'native UI drawing check identity changed')
    package.require(sha(BUILD / 'setup/StarryFishSetup.exe') == record['setup_sha256'] and
                    sha(BUILD / 'setup/SetupChecks.exe') == record['setup_checks_sha256'] and
                    sha(BUILD / 'generated/SetupInputs.cs') == record['setup_inputs_sha256'], 'setup inputs/output identity changed')
    package.require(setup_inventory() == record['setup_inventory'], 'setup payload differs from compiled installer expectations')
    print('VERIFIED native compiled file identity only; runtime/performance/install acceptance NOT_EVALUATED')


def package_candidate():
    """Portable fixed-byte review package, not the final latest-at-install setup."""
    verify()
    root = BUILD / 'native/Release'
    known = prepare_components()  # Also reads back all checked output members.
    package.require(known == root, 'unexpected component output root')
    package.require({path.name for path in (root / 'Components').iterdir()} ==
                    {'zeroomega', 'tarou', 'arcarum-dungeon'}, 'unknown packaged component retained; not included')
    files, directories = {}, {'Components', 'Scripts'}
    readers = {}
    selected = [root / 'StarryFish.exe', *sorted((root / 'Components').rglob('*')), *sorted((root / 'Scripts').rglob('*'))]
    for path in selected:
        if path.is_dir():
            observation._directory_identity(path)
            directories.add(path.relative_to(root).as_posix())
        elif path.is_file():
            digest, size = sha(path), path.stat().st_size
            observation._read_regular(path, size, digest, size)
            relative = path.relative_to(root).as_posix()
            files[relative] = (size, digest)
            readers[relative] = path
        else:
            raise ValueError('candidate contains an unsupported member')
    license = SDK / 'LICENSE.txt'
    digest, size = sha(license), license.stat().st_size
    package.require(digest == SDK_MEMBERS['LICENSE.txt'], 'SDK license identity changed')
    files['WebView2-SDK-LICENSE.txt'] = (size, digest)
    readers['WebView2-SDK-LICENSE.txt'] = license
    record = {'schema': 1, 'kind': 'FIXED_BYTE_NATIVE_REVIEW_CANDIDATE',
              'inputs': json.loads((BUILD / 'build-receipt.json').read_text()),
              'runtime': 'EXISTING_WEBVIEW2_REQUIRED_NOT_BUNDLED',
              'first_install_latest': 'NOT_CONNECTED', 'cef_migration': 'NOT_PERFORMED',
              'browser_acceptance': 'NOT_RUN',
              'files': {name: {'bytes': identity[0], 'sha256': identity[1]} for name, identity in files.items()}}
    encoded = (json.dumps(record, sort_keys=True, indent=2) + '\n').encode('utf-8')
    files['candidate-record.json'] = (len(encoded), hashlib.sha256(encoded).hexdigest())
    def read(name):
        if name == 'candidate-record.json':
            return encoded
        size, digest = files[name]
        return observation._read_regular(readers[name], size, digest, size, capture=True)
    destination = 'c-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S%f')
    publication.publish_tree(BUILD, destination, files, directories, read)
    verify()
    print('Fixed native review package:', BUILD / destination)
    print('Files:', len(files), 'bytes:', sum(size for size, _ in files.values()))
    print('Latest-at-install/runtime/performance/release acceptance NOT_EVALUATED; existing data retained')


def test():
    verify()
    node = shutil.which('node')
    package.require(node is not None, 'use the existing Node runtime; do not install a new toolchain')
    subprocess.run([node, str(ROOT / 'checks/scripts.js')], check=True)
    subprocess.run([node, str(ROOT / 'checks/user_scripts.js')], check=True)
    subprocess.run([node, str(ROOT / 'checks/proxy_profiles.cjs')], check=True)
    subprocess.run([node, str(ROOT / 'checks/loading_display.cjs')], check=True)
    subprocess.run([node, str(ROOT / 'checks/login_resume.cjs')], check=True)
    subprocess.run([node, str(APP / 'tests/proxy_initialization_js_tests.cjs')], check=True)
    directory = tempfile.mkdtemp(prefix='directory-check-', dir=BUILD)
    subprocess.run([str(BUILD / 'native/Release/NativeSourceChecks.exe'), directory], check=True)
    print('Retained native directory check evidence:', directory)
    for dpi in (96, 144, 192):
        preview = Path(directory) / f'ui-{dpi}'
        subprocess.run([str(BUILD / 'native/Release/UiThemePreview.exe'), str(preview),
                        str(BUILD / 'native/Release/Components'), str(dpi)], check=True)
    setup_directory = tempfile.mkdtemp(prefix='setup-check-', dir=BUILD)
    fixture = Path(tempfile.mkdtemp(prefix='setup-input-', dir=BUILD))
    # Build output, fixed Assets and the SDK license have different source
    # locations. Assemble the exact installer inventory, never infer one folder.
    for name, source in setup_members().items():
        digest, size = sha(source), source.stat().st_size
        data = observation._read_regular(source, size, digest, size, capture=True)
        target = fixture / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as output:
            output.write(data)
    print('Retained setup fixture input:', fixture, flush=True)
    subprocess.run([str(BUILD / 'setup/SetupChecks.exe'), setup_directory, str(fixture)], check=True)
    subprocess.run([str(BUILD / 'native/Release/NativeSourceChecks.exe'), '--catalogue', setup_directory], check=True)
    subprocess.run([str(BUILD / 'setup/SetupChecks.exe'), '--archives', setup_directory,
                    str(ROOT / '.deps/tarou.zip'),
                    str(ROOT / '.deps/arcarum-dungeon.zip')], check=True)
    print('Retained setup source check evidence:', setup_directory)


def setup_members():
    root = BUILD / 'native/Release'
    return {'StarryFish.exe': root / 'StarryFish.exe',
            'Licenses/ZeroOmega-v3.5.2-source.zip': ROOT / '.deps/zeroomega-source.zip',
            'WebView2-SDK-LICENSE.txt': SDK / 'LICENSE.txt',
            'LICENSE.txt': ROOT.parents[1] / 'LICENSE',
            'THIRD_PARTY_NOTICES.md': ROOT.parents[1] / 'THIRD_PARTY_NOTICES.md',
            **{'Licenses/' + path.name: path for path in sorted((ROOT.parents[1] / 'licenses').glob('*')) if path.is_file()},
            **{'Assets/' + path.name: path for path in sorted((APP / 'src/bundled_assets').glob('*')) if path.is_file()},
            **{path.relative_to(root).as_posix(): path for path in sorted((root / 'Components/zeroomega').rglob('*')) if path.is_file()},
            **{path.relative_to(root).as_posix(): path for path in sorted((root / 'Scripts').rglob('*')) if path.is_file()}}


def compile_setup(fingerprint, output):
    """Build-time existing Roslyn only; no compiler/Python in the setup package."""
    members = setup_members()
    inventory = setup_inventory()
    definitions = []
    for name, path in members.items():
        size, digest = inventory[name]['bytes'], inventory[name]['sha256']
        observation._read_regular(path, size, digest, size)
        definitions.append('new Member(' + json.dumps(name) + ',' + str(size) + ',' + json.dumps(digest) + ')')
    text = 'namespace Gbrowser.Setup { internal sealed class Member { internal readonly string Name, Hash; internal readonly int Bytes; internal Member(string name,int bytes,string hash) { Name=name; Bytes=bytes; Hash=hash; } }\n'
    text += 'internal static class SetupInputs { internal const string Version=' + json.dumps((ROOT.parents[1] / 'VERSION').read_text().strip()) + '; internal const string HostFingerprint=' + json.dumps(fingerprint) + ';\n'
    text += 'internal const string ZeroTree="68cc0ca912f033f7841633d5dcf50c29687c6f6e76617c05cbeddb49f530caa5";\n'
    text += 'internal static readonly Member[] Files={' + ','.join(definitions) + '}; } }\n'
    generated = BUILD / 'generated/SetupInputs.cs'
    generated.write_text(text, encoding='utf-8')
    embedded = BUILD / 'generated/setup-payload.zip'
    import zipfile
    with zipfile.ZipFile(embedded, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in sorted(members.items()):
            archive.writestr(name, path.read_bytes())
    destination = BUILD / 'setup'
    destination.mkdir(exist_ok=True)
    compiler = vs_root() / 'MSBuild/Current/Bin/Roslyn/csc.exe'
    common = [str(compiler), '/nologo', '/target:exe', '/platform:x64', '/optimize+', '/warnaserror+',
              '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll', '/r:System.Security.dll',
              '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll',
               '/win32icon:' + str(ROOT.parents[1] / 'assets/starryfish.ico'), str(ROOT / 'setup/Source.cs'), str(ROOT / 'setup/Setup.cs'), str(ROOT / 'setup/ManualReload.cs'),
               str(ROOT / 'setup/ApplicationUpdate.cs'), str(ROOT / 'setup/SetupWindow.cs'), str(ROOT / 'setup/SetupOptions.cs'),
               str(ROOT / 'setup/EmbeddedPackage.cs'), str(ROOT / 'setup/Integration.cs'), str(ROOT / 'setup/CleanUninstall.cs'), str(generated)]
    subprocess.run([*common, '/target:winexe', '/resource:' + str(embedded) + ',StarryFish.Payload.zip', '/main:Gbrowser.Setup.Program', '/out:' + str(destination / 'StarryFishSetup.exe')],
                   stdout=output, stderr=subprocess.STDOUT, check=True)
    subprocess.run([*common, str(ROOT / 'checks/SetupChecks.cs'), '/main:Gbrowser.Setup.SetupChecks',
                    '/out:' + str(destination / 'SetupChecks.exe')], stdout=output, stderr=subprocess.STDOUT, check=True)
    return inventory


def setup_inventory():
    return {name: {'bytes': path.stat().st_size, 'sha256': sha(path)} for name, path in setup_members().items()}


def package_setup():
    """Setup review package: fixed ZeroOmega + host, checked online/local helpers."""
    verify()
    readers = setup_members()
    readers['StarryFishSetup.exe'] = BUILD / 'setup/StarryFishSetup.exe'
    files, directories = {}, set()
    for name, path in readers.items():
        size, digest = path.stat().st_size, sha(path)
        files[name] = (size, digest)
        folder = name
        while '/' in folder:
            folder = folder.rsplit('/', 1)[0]
            directories.add(folder)
    record = {'schema': 1, 'kind': 'STARRYFISH_WINDOWS_INSTALLER',
              'runtime': 'EXISTING_WEBVIEW2_AND_WINDOWS_NET_FRAMEWORK_REQUIRED',
              'first_install_helpers': 'PUBLIC_HTTPS_LATEST_RELEASE_OR_ALLOWED_DEVELOPMENT_COMMIT_OR_VALIDATED_LOCAL_ZIP',
              'install_browser_acceptance': 'NOT_RUN', 'source_signature': 'NOT_EVALUATED',
              'files': {name: {'bytes': size, 'sha256': digest} for name, (size, digest) in files.items()}}
    encoded = (json.dumps(record, sort_keys=True, indent=2) + '\n').encode('ascii')
    files['setup-record.json'] = len(encoded), hashlib.sha256(encoded).hexdigest()
    def read(name):
        if name == 'setup-record.json':
            return encoded
        size, digest = files[name]
        return observation._read_regular(readers[name], size, digest, size, capture=True)
    destination = 'setup-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S%f')
    publication.publish_tree(BUILD, destination, files, directories, read)
    verify()
    print('First-install review package:', BUILD / destination)
    print('Files:', len(files), 'bytes:', sum(size for size, _ in files.values()))
    print('Actual first install/native browser/performance/release acceptance NOT_RUN')


def prepare_components():
    """Use already acquired evidence for this candidate, not a latest-at-install claim."""
    sources = {
        'zeroomega': ROOT / '.deps/zeroomega',
        'tarou': ROOT / '.deps/tarou',
        'arcarum-dungeon': ROOT / '.deps/arcarum-dungeon',
    }
    trees = ['68cc0ca912f033f7841633d5dcf50c29687c6f6e76617c05cbeddb49f530caa5',
             '6e524626b55985d934f4f06510a488ca675dd1e0bec76d20674739d73957d248',
             'd1f848a1a6ef0fecd675cd808de6436e4ef4813cdaf2f9ad2afbb3d263689495']
    entries = ['options.html', 'src/views/sidePanel/main.html', 'src/panel/index.html']
    definitions = []
    output = BUILD / 'native/Release'
    output.mkdir(parents=True, exist_ok=True)
    for index, (name, source) in enumerate(sources.items()):
        files = {}
        directories = set()
        total = 0
        for current, dirs, names in os.walk(source, followlinks=False):
            current = Path(current)
            observation._directory_identity(current)
            for part in dirs:
                observation._directory_identity(current / part)
                directories.add((current / part).relative_to(source).as_posix())
            for part in names:
                original = current / part
                digest = sha(original)
                size = original.stat().st_size
                observation._read_regular(original, size, digest, size)
                total += size
                files[original.relative_to(source).as_posix()] = (size, digest)
        tree = hashlib.sha256(json.dumps(sorted((name, identity[1]) for name, identity in files.items()),
            ensure_ascii=True, separators=(',', ':')).encode('ascii')).hexdigest()
        package.require(tree == trees[index] and 0 < len(files) <= 1024 and total <= 32*1024*1024,
                        'previously acquired component identity changed')
        def reader(relative):
            size, digest = files[relative]
            return observation._read_regular(source / relative, size, digest, size, capture=True)
        destination = output / 'Components' / name
        if destination.exists():
            publication.verify_payload(destination, files, directories)
        else:
            publication.publish_tree(output, 'Components/' + name, files, directories, reader)
        definitions.append('{L"' + name + '","' + tree + '",' + str(len(files)) + ',' + str(total) + ',L"' + entries[index] + '"}')
    generated = BUILD / 'generated'
    generated.mkdir(exist_ok=True)
    script_definition = prepare_user_scripts(output)
    (generated / 'components.h').write_text(
        '#pragma once\nstruct ComponentInput { const wchar_t* name; const char* tree; size_t files, bytes; const wchar_t* entry; };\n'
        'inline constexpr ComponentInput kComponents[] = {' + ','.join(definitions) + '};\n'
        'inline constexpr ComponentInput kScriptsComponent = ' + script_definition + ';\n', encoding='utf-8')
    return output


def prepare_user_scripts(output):
    """Fixed product extension; reuse reviewed script bytes, never a remote loader."""
    asset_hashes = {
        'gbf-bookmark-0.2.8.user.js': 'd4ffe05127f6c16542f6a762b3223fa94543582bc8c75732b1b1e876133a4bf7',
        'gbf-translation-2.9.5.user.js': '28f54d61fe176238df1d6eec61344287937e9b2b62a4e5a6f794cc0bf95bc985',
        'GBF-Bookmark-Config.json': 'a3ccc704034302836636561e4008c639065819e700ceb64e0976cdaffcd1bc0f',
    }
    raw = {}
    for name, digest in asset_hashes.items():
        path = APP / 'src/bundled_assets' / name
        raw[name] = observation._read_regular(path, 1024*1024, digest, path.stat().st_size, capture=True)
    configuration = module('bookmark_source', APP / 'tools/bookmark_config.py')
    initial = configuration.strict_json(raw['GBF-Bookmark-Config.json'])
    configuration.validate_config(initial)
    members = {path.name: path.read_bytes() for path in sorted((ROOT / 'src/scripts').iterdir()) if path.is_file()}
    seed = members['bookmark_seed.js'].decode('utf-8')
    members['bookmark_seed.js'] = (seed + '(' + json.dumps(initial, ensure_ascii=True, separators=(',', ':')) + ');\n').encode('utf-8')
    members['gbf-bookmark.js'] = raw['gbf-bookmark-0.2.8.user.js']
    members['gbf-translation.js'] = raw['gbf-translation-2.9.5.user.js']
    manifest = json.loads(members['manifest.json'])
    references = [manifest['background']['service_worker'], 'request_policy.js',
                  *[name for group in manifest['content_scripts'] for name in group['js']]]
    package.require(all(name in members for name in references), 'script manifest references an absent file')
    files = {name: (len(value), hashlib.sha256(value).hexdigest()) for name, value in members.items()}
    tree = hashlib.sha256(json.dumps(sorted((name, identity[1]) for name, identity in files.items()),
        ensure_ascii=True, separators=(',', ':')).encode('ascii')).hexdigest()
    destination = output / 'Scripts'
    # Build artifacts may change between builds, never an installed source tree.
    # Write only known files beneath this resolved private build directory; do
    # not delete unknown members or replace symlinks/reparse paths.
    package.require(destination.resolve().parent == (BUILD / 'native/Release').resolve(), 'unexpected script output root')
    if destination.exists():
        observation._directory_identity(destination)
        package.require({p.name for p in destination.iterdir()} <= set(members), 'unknown script output member retained')
        for name, value in members.items():
            target = destination / name
            if not target.exists():
                # A newly added known adapter may extend build output. Use
                # no-replace creation; unknown existing members remain rejected.
                with target.open('xb') as new_file:
                    new_file.write(value)
                continue
            observation._read_regular(target, 1024*1024, sha(target), target.stat().st_size)
            if target.read_bytes() != value:
                target.write_bytes(value)
        publication.verify_payload(destination, files, set())
    else:
        publication.publish_tree(output, 'Scripts', files, set(), lambda name: members[name])
    return '{L"Scripts","' + tree + '",' + str(len(files)) + ',' + str(sum(len(v) for v in members.values())) + ',L"status.html"}'


def generate_scripts():
    scripts = {'kProxyScript': APP / 'src/proxy_initialization.js', 'kProxyBridge': ROOT / 'src/proxy_bridge.js',
               'kProxyProfilesScript': ROOT / 'src/proxy_profiles.js', 'kProxyMenuScript': ROOT / 'src/proxy_menu.js',
               'kTargetScript': ROOT / 'src/target_binding.js', 'kGameUiScript': ROOT / 'src/game_ui.js',
               'kArcarumBindingScript': ROOT / 'src/arcarum_binding.js', 'kPanePointerScript': ROOT / 'src/pane_pointer.js',
               'kGameRoleScript': ROOT / 'src/game_role.js',
               'kLoadingDisplayScript': ROOT / 'src/loading_display.js'}
    header = '#pragma once\n'
    for name, path in scripts.items():
        text = path.read_text(encoding='utf-8')
        if ')wvjs"' in text:
            raise ValueError('embedded script delimiter collision')
        header += 'inline constexpr char ' + name + '[] = ' + '\n'.join(
            'R"wvjs(' + text[offset:offset + 4000] + ')wvjs"' for offset in range(0, len(text), 4000)) + ';\n'
    (BUILD / 'generated/proxy_scripts.h').write_text(header, encoding='utf-8')


def build():
    BUILD.mkdir(exist_ok=True)
    payload = prepare_components()
    generate_scripts()
    version = (ROOT.parents[1] / 'VERSION').read_text().strip()
    fields = version.split('.')
    if len(fields) != 3 or not all(v.isdigit() and int(v) <= 65535 for v in fields):
        raise ValueError('Invalid product version')
    resource = (ROOT / 'src/version.rc.in').read_text().replace('@VERSION_COMMAS@', ','.join(fields + ['0'])).replace('@VERSION@', version)
    (BUILD / 'generated/version.rc').write_text(resource,encoding='utf-8')
    generated = BUILD / 'native/generated'
    generated.mkdir(parents=True, exist_ok=True)
    raw = (APP / 'src/proxy_presets.config').read_text(encoding='utf-8')
    chunks = '\n'.join('R"json(' + raw[i:i+4000] + ')json"' for i in range(0,len(raw),4000))
    (generated / 'proxy_preset_assets.h').write_text('#pragma once\nnamespace gbrowser { inline constexpr char kInitialProxyOptions[] =\n' + chunks + '; }\n',encoding='utf-8')
    raw = (APP / 'src/storage_probe.js').read_text(encoding='utf-8')
    (generated / 'storage_probe_assets.h').write_text('#pragma once\nnamespace gbrowser { inline constexpr char kStorageProbeJavaScript[] = R"GBPROBE(' + raw + ')GBPROBE"; }\n',encoding='utf-8')
    before = fingerprint_inputs()
    fingerprint = hashlib.sha256(json.dumps(before, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    cmake = vs_root() / 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    environment = os.environ.copy()
    environment['CL'] = '/utf-8'
    for name in ('_CL_', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CMAKE_TOOLCHAIN_FILE'):
        environment.pop(name, None)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    log = BUILD / ('build-' + stamp + '.log')
    with log.open('x', encoding='utf-8') as output:
        for command in ([str(cmake), '-S', str(ROOT), '-B', str(BUILD / 'native'), '-G',
                         'Visual Studio 17 2022', '-A', 'x64', '-DGBROWSER_HOST_FINGERPRINT=' + fingerprint],
                        [str(cmake), '--build', str(BUILD / 'native'), '--config', 'Release']):
            subprocess.run(command, env=environment, stdout=output, stderr=subprocess.STDOUT, check=True)
        setup_inputs = compile_setup(fingerprint, output)
    after = fingerprint_inputs()
    if after != before:
        raise ValueError('host source/dependency changed during build')
    package.require(setup_inventory() == setup_inputs, 'setup payload changed while compiling installer')
    executable = BUILD / 'native/Release/StarryFish.exe'
    record = {'status': 'COMPILED_NOT_RUNTIME_ACCEPTANCE', 'input_fingerprint': fingerprint,
              'inputs': before, 'executable_sha256': sha(executable), 'cef_migration': 'NOT_PERFORMED',
              'source_checks_sha256': sha(BUILD / 'native/Release/NativeSourceChecks.exe'),
              'ui_preview_sha256': sha(BUILD / 'native/Release/UiThemePreview.exe'),
              'setup_sha256': sha(BUILD / 'setup/StarryFishSetup.exe'),
              'setup_checks_sha256': sha(BUILD / 'setup/SetupChecks.exe'),
              'setup_inputs_sha256': sha(BUILD / 'generated/SetupInputs.cs'),
              'setup_inventory': setup_inputs,
              'first_install_latest': 'SETUP_IMPLEMENTED_NOT_INSTALL_ACCEPTED', 'component_origin': 'EARLIER_ACQUIRED_CANDIDATE_BYTES'}
    (BUILD / 'build-receipt.json').write_text(json.dumps(record, indent=2) + '\n')
    (BUILD / ('build-receipt-' + stamp + '.json')).write_text(json.dumps(record, indent=2) + '\n')
    print('Native candidate compiled:', executable)
    print('Component payload:', payload)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument('command', choices=('build', 'test', 'verify', 'package', 'package-setup'))
    args = parser.parse_args()
    try:
        {'build': build, 'test': test, 'verify': verify, 'package': package_candidate, 'package-setup': package_setup}[args.command]()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print('STOP:', error, file=sys.stderr)
        sys.exit(1)
