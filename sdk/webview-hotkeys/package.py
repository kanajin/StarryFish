"""Package only the reviewed WebView SDK inventory; no browser extension/host."""
from pathlib import Path
import hashlib, json, subprocess, sys, zipfile
ROOT=Path(__file__).resolve().parent
PROJECT=ROOT.parents[1]
SOURCES=['README.md','LICENSE','CMakeLists.txt','build.py','package.py',
 'include/gbf_hotkeys.h','native/core.cpp','docs/SDK.md',
 'examples/cpp/session.h','examples/dotnet/GbfHotkeys.csproj','examples/dotnet/HotkeysSession.cs',
 'vendor/character_shortcuts.h','vendor/character_shortcuts.js','vendor/provenance.json',
 'checks/character_shortcuts.cjs','checks/sdk.cpp']

def package():
    subprocess.run([sys.executable,'-B',str(ROOT/'build.py'),'verify'],cwd=ROOT,check=True)
    version=(PROJECT/'VERSION').read_text().strip()
    files={name:ROOT/name for name in SOURCES}
    for name in ('gbf_hotkeys.dll','gbf_hotkeys.lib'):
        files['bin/'+name]=ROOT/'.build/native/Release'/name
    output=PROJECT/'dist'/f'StarryFish-WebView-Hotkeys-{version}-windows-x64.zip'
    output.parent.mkdir(exist_ok=True)
    hashes={}
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,path in sorted(files.items()):
            data=path.read_bytes()
            prefix=b'C:'+bytes([92])+b'Users'+bytes([92])
            for marker in (prefix, prefix.decode().encode('utf-16le')):
                if marker.lower() in data.lower():raise ValueError('Personal path in SDK member: '+name)
            hashes[name]=hashlib.sha256(data).hexdigest()
            archive.writestr(name,data)
        archive.writestr('SHA256SUMS.txt',''.join(f'{value}  {name}\n' for name,value in sorted(hashes.items())))
    with zipfile.ZipFile(output) as archive:
        if set(archive.namelist())!=set(files)|{'SHA256SUMS.txt'}:raise ValueError('SDK inventory differs')
        for name,value in hashes.items():
            if hashlib.sha256(archive.read(name)).hexdigest()!=value:raise ValueError('SDK readback differs')
    print('WebView-only SDK packaged:',output.name)
    return output
if __name__=='__main__':package()
