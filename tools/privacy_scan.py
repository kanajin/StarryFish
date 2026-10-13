"""Inspect public Git files. Only filenames/rule names are printed on failures."""
from pathlib import Path
import re, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
RULES={
 'private-key':rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
 'github-token':rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})',
 'aws-key':rb'AKIA[0-9A-Z]{16}',
 'service-secret':rb'sk-(?:proj-)?[A-Za-z0-9_-]{32,}',
 'personal-home':rb'[A-Za-z]:[\\/]+Users[\\/]+(?!Public\b|Default\b)[^\s"<>]+',
}
DENIED={'.build','.deps','.sdk','.runtime','userdata','validation','gbfvalidation','teststate','node_modules','__pycache__'}
def main():
    names=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT).decode().split('\0')
    errors=[];count=0
    for name in filter(None,names):
        p=ROOT/name
        if p.is_symlink() or any(x.lower() in DENIED for x in p.relative_to(ROOT).parts):
            errors.append((name,'linked/private/build file'));continue
        if p.suffix.lower() in ('.log','.pdb','.dmp','.sqlite','.db','.crx','.zip','.exe','.dll','.lib') or p.name.lower() in ('cookies','preferences','local state','login data'):
            errors.append((name,'runtime/archive file'))
        if p.stat().st_size>4*1024*1024:errors.append((name,'unexpected large file'))
        data=p.read_bytes();count+=1
        for rule,pattern in RULES.items():
            if re.search(pattern,data):errors.append((name,rule))
    if errors:
        for name,rule in errors:print('STOP',name,rule)
        return 1
    print('PASS public source privacy patterns and file boundaries:',count,'files; no profile access')
    return 0
if __name__=='__main__':sys.exit(main())
