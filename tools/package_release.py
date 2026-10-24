"""Archive only the verified installer inventory, never a whole build tree."""
from pathlib import Path
import hashlib, json, re, shutil, sys, subprocess
ROOT=Path(__file__).resolve().parents[1]
def main(version):
    if not re.fullmatch(r'\d+\.\d+\.\d+',version) or version!=(ROOT/'VERSION').read_text().strip():
        raise ValueError('Release tag must match VERSION')
    build=ROOT/'hosts/webview2/.build'
    candidates=sorted(p for p in build.glob('setup-*') if (p/'setup-record.json').is_file())
    if not candidates:raise ValueError('Run build.py package-setup first')
    setup=candidates[-1]; record=json.loads((setup/'setup-record.json').read_text())
    allowed=set(record['files'])|{'setup-record.json'}
    actual={p.relative_to(setup).as_posix() for p in setup.rglob('*') if p.is_file()}
    if allowed!=actual:raise ValueError('Setup inventory differs')
    for name,identity in record['files'].items():
        data=(setup/name).read_bytes()
        if len(data)!=identity['bytes'] or hashlib.sha256(data).hexdigest()!=identity['sha256']:raise ValueError('Setup bytes changed')
        # Do not publish PE build paths naming a private desktop user.
        prefix=b'C:'+bytes([92])+b'Users'+bytes([92])
        for marker in (prefix, prefix.decode().encode('utf-16le')):
            if marker.lower() in data.lower():raise ValueError('Personal path in release member: '+name)
    dist=ROOT/'dist';dist.mkdir(exist_ok=True)
    output=dist/f'StarryFish-{version}-windows-x64-setup.exe'
    source=ROOT/'hosts/webview2/.deps/zeroomega-source.zip'
    if hashlib.sha256(source.read_bytes()).hexdigest()!='f49f1e64f20aeec59056accb774a0b61afd6bdcae7760e47472be78ae748f553':raise ValueError('GPL source hash differs')
    if (setup/'Licenses/ZeroOmega-v3.5.2-source.zip').read_bytes()!=source.read_bytes():raise ValueError('Embedded GPL source differs')
    shutil.copyfile(setup/'StarryFishSetup.exe',output)
    subprocess.run([sys.executable,'-B',str(ROOT/'sdk/webview-hotkeys/package.py')],check=True)
    paths=[output,dist/f'StarryFish-WebView-Hotkeys-{version}-windows-x64.zip']
    (dist/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in paths),encoding='ascii')
    print('Release installer ready:',output.name,'members:',len(allowed))
if __name__=='__main__':main(sys.argv[1])
