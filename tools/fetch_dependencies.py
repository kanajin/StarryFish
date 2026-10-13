"""Fetch pinned public build dependencies. No user profiles or installed apps."""
from pathlib import Path, PurePosixPath
import hashlib, io, json, os, stat, urllib.request, zipfile

ROOT=Path(__file__).resolve().parents[1]
HOST=ROOT/'hosts/webview2'
LOCK={
 'webview2':('https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg','56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9'),
 'zeroomega':('https://github.com/zero-peak/ZeroOmega/releases/download/v3.5.2/chromium-release.zip','8e662c1ebc5b4ddb63e65a7c8664709c15cedf27c933c8ba8ff6d99b0c037ab0'),
 'tarou':('https://github.com/Waaatanuki/Chrome-Extension-Tarou/releases/download/3.7.1/Chrome-Extension-Tarou.v3.7.1.zip','87cb6de12fac3cd18d75567198e8404f3d2b84aff724eea4968aefef20ba2b18'),
 'arcarum-dungeon':('https://codeload.github.com/Aureth/arcarumDungeon/zip/c857641f7e77a730d4fda9cfcce947a701eae0d0','fbb8298434b76ca30edb10bc58c57bfbf2c0760d89d89e837ac0deb4a9f44c27'),
}

def fetch(url):
    request=urllib.request.Request(url,headers={'User-Agent':'StarryFish-build/0.1.0'})
    with urllib.request.urlopen(request,timeout=120) as response:
        if not response.url.startswith('https://'): raise ValueError('HTTPS required')
        data=response.read(100*1024*1024+1)
    if len(data)>100*1024*1024:raise ValueError('Download too large')
    return data

def main():
    deps=HOST/'.deps';deps.mkdir(exist_ok=True)
    for role,(url,digest) in LOCK.items():
        archive=deps/(role+'.zip')
        data=archive.read_bytes() if archive.exists() else fetch(url)
        if hashlib.sha256(data).hexdigest()!=digest:raise ValueError(role+' archive hash mismatch')
        if not archive.exists():archive.write_bytes(data)
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            members=[i for i in z.infolist() if not i.is_dir()]
            if len(members)>4096 or sum(i.file_size for i in members)>256*1024*1024:raise ValueError('Archive budget')
            selected={}
            for i in members:
                p=PurePosixPath(i.filename)
                if p.is_absolute() or '..' in p.parts or '\\' in i.filename or ':' in i.filename or stat.S_ISLNK(i.external_attr>>16):raise ValueError('Unsafe ZIP member')
                if role=='webview2':
                    wanted={'build/native/include/WebView2.h':'WebView2.h','build/native/include/WebView2EnvironmentOptions.h':'WebView2EnvironmentOptions.h',
                            'build/native/x64/WebView2LoaderStatic.lib':'WebView2LoaderStatic.lib','LICENSE.txt':'LICENSE.txt'}
                    name=wanted.get(i.filename)
                    if name is None:continue
                    destination=HOST/'.sdk/1.0.4258.31'
                else:
                    name=i.filename
                    if role=='tarou':
                        prefix='Chrome-Extension-Tarou/'
                        if not name.startswith(prefix):raise ValueError('Tarou archive layout changed')
                        name=name[len(prefix):]
                    elif role=='arcarum-dungeon':
                        prefix='arcarumDungeon-c857641f7e77a730d4fda9cfcce947a701eae0d0/'
                        if not name.startswith(prefix):raise ValueError('Arcarum archive layout changed')
                        name=name[len(prefix):]
                    destination=deps/role
                if name.casefold() in selected:raise ValueError('Duplicate member')
                selected[name.casefold()]=name
                target=destination/name
                if not target.resolve().is_relative_to(destination.resolve()):raise ValueError('Escaped extraction')
                raw=z.read(i)
                target.parent.mkdir(parents=True,exist_ok=True)
                if target.exists():
                    if target.is_symlink() or target.read_bytes()!=raw:raise ValueError('Existing dependency differs: '+name)
                else:
                    with target.open('xb') as output:output.write(raw)
            if role=='webview2' and len(selected)!=4:raise ValueError('SDK members missing')
        print(role+': pinned dependency ready')

    source=deps/'zeroomega-source.zip'
    raw=source.read_bytes() if source.exists() else fetch('https://codeload.github.com/zero-peak/ZeroOmega/zip/c367db220e2a3af1c65e5afffa4cf42c949f2908')
    if hashlib.sha256(raw).hexdigest()!='f49f1e64f20aeec59056accb774a0b61afd6bdcae7760e47472be78ae748f553':raise ValueError('ZeroOmega source hash mismatch')
    if not source.exists():source.write_bytes(raw)

if __name__=='__main__':main()
