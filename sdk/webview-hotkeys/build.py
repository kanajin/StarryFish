from pathlib import Path
import argparse, subprocess, shutil, os, hashlib, json
ROOT=Path(__file__).resolve().parent
B=ROOT/'.build'
def run(*args): subprocess.run([str(x) for x in args],cwd=ROOT,check=True)
def fingerprint():
    paths=[ROOT/'CMakeLists.txt',ROOT/'build.py']
    for folder in ['native','include','vendor','checks','examples']:
        paths.extend(p for p in (ROOT/folder).rglob('*') if p.is_file() and not {'bin','obj','__pycache__'}.intersection(p.relative_to(ROOT).parts))
    digest=hashlib.sha256()
    for p in sorted(paths):
        digest.update(p.relative_to(ROOT).as_posix().encode());digest.update(p.read_bytes())
    return digest.hexdigest()
def verify():
    saved=json.loads((B/'hashes.json').read_text())
    if saved['source']!=fingerprint():raise SystemExit('Source changed after build; rebuild first')
    for n,expected in saved['binaries'].items():
        if hashlib.sha256((B/'native/Release'/n).read_bytes()).hexdigest()!=expected:raise SystemExit('Binary changed: '+n)
    print('Build source and binary identity verified')
def build():
    finder=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    vs=subprocess.check_output([str(finder),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    cmake=Path(vs)/'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    (B/'generated').mkdir(parents=True,exist_ok=True)
    probe=(ROOT/'vendor/character_shortcuts.js').read_text(encoding='utf-8')
    (B/'generated/probe.h').write_text('#pragma once\nconstexpr const char* kProbe=R"GBF('+probe+')GBF";\n',encoding='utf-8')
    run(cmake,'-S',ROOT,'-B',B/'native','-G','Visual Studio 17 2022','-A','x64')
    run(cmake,'--build',B/'native','--config','Release')
    files=['gbf_hotkeys.dll','gbf_hotkeys.lib']
    (B/'hashes.json').write_text(json.dumps({'source':fingerprint(),'binaries':{n:hashlib.sha256((B/'native/Release'/n).read_bytes()).hexdigest() for n in files}},indent=2))
def test():
    verify()
    node=shutil.which('node')
    if not node: raise SystemExit('Node.js required')
    run(node,'checks/character_shortcuts.cjs')
    run(B/'native/Release/sdk_checks.exe')
p=argparse.ArgumentParser();p.add_argument('action',choices=['build','test','verify']);a=p.parse_args()
{'build':build,'test':test,'verify':verify}[a.action]()
