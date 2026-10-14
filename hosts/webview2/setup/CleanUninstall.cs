using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace Gbrowser.Setup {
// Root-bound ownership of external maintenance files. The installation root is
// never recursively deleted; all immutable removals require exact byte identity.
internal static class CleanUninstall {
    internal const string Ledger = "maintenance-owner.bin";
    sealed class CleanupLock : IDisposable {
        readonly Mutex mutex; bool held;
        internal CleanupLock(string root,int wait) {
            mutex=new Mutex(false,"Local\\StarryFish.Uninstall."+Path.GetFileName(Integration.StatePath(root)));
            try { held=mutex.WaitOne(wait); } catch(AbandonedMutexException) { held=true; }
            if(!held) { mutex.Dispose(); throw new IOException("另一个卸载过程正在运行，请稍后重试。"); }
        }
        public void Dispose() { if(held) { mutex.ReleaseMutex(); held=false; } mutex.Dispose(); }
    }
    internal static IDisposable Lock(string root,int wait=0) { return new CleanupLock(root,wait); }
    internal static string CacheRoot(string root) {
        return Paths.Local(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "StarryFish", "SetupCache", Path.GetFileName(Integration.StatePath(root))));
    }
    static string MenuRoot() { return Paths.Local(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "StarryFish")); }
    static byte[] Entropy(string root) { return Encoding.UTF8.GetBytes("StarryFish.MaintenanceOwnership.v1\n" + Paths.Local(root).ToLowerInvariant()); }
    internal static void RecordState(string root) {
        var state=Integration.StatePath(root); var path=Path.Combine(state,"state-owner.bin");
        using (var guard=new Paths()) {
            guard.CreateTree(state);
            if (!File.Exists(path)) Paths.WriteNew(path,ProtectedData.Protect(Encoding.ASCII.GetBytes("STARRYFISH-STATE-1"),Entropy(root),DataProtectionScope.CurrentUser));
            VerifyState(root);
        }
    }
    internal static void VerifyState(string root) {
        var path=Path.Combine(Integration.StatePath(root),"state-owner.bin");
        // Earlier installations predate this marker; their root-bound setup
        // inventory still establishes this deterministic private state namespace.
        if (!File.Exists(path)) return;
        using (var read=new Paths()) Rules.Need(Encoding.ASCII.GetString(ProtectedData.Unprotect(read.Read(path,4096),Entropy(root),DataProtectionScope.CurrentUser))=="STARRYFISH-STATE-1","资料归属记录不匹配。");
    }
    static bool Allowed(string root, Member member) {
        var path = Paths.Local(member.Name);
        var menu = MenuRoot();
        if (path == Path.Combine(menu,"StarryFish.lnk") || path == Path.Combine(menu,"卸载 StarryFish.lnk")) return member.Bytes <= 65536;
        var shared = Path.GetDirectoryName(CacheRoot(root));
        var parent = Path.GetDirectoryName(path); var name = Path.GetFileName(parent);
        // Legacy cache is admitted only from this root's exact registered
        // UninstallString during upgrade; new caches are isolated by root hash.
        return Path.GetFileName(path) == "StarryFishSetup.exe" && member.Bytes > 0 && member.Bytes <= 64 * 1024 * 1024 &&
            name == member.Hash.Substring(0,16) && (Path.GetDirectoryName(parent) == shared || Path.GetDirectoryName(parent) == CacheRoot(root));
    }
    static void CheckFiles(string root, IList<Member> files) {
        Rules.Need(files.Count <= 512 && files.Select(x=>x.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() == files.Count, "维护归属记录超限或重复。");
        foreach(var file in files) Rules.Need(file.Bytes>=0 && Rules.Match(file.Hash,"[0-9a-f]{64}") && Allowed(root,file),"维护路径不属于此安装。");
    }
    internal static byte[] Encode(string root, IEnumerable<Member> values) {
        var files = values.OrderBy(x=>x.Name,StringComparer.Ordinal).ToArray(); CheckFiles(root,files);
        using (var memory = new MemoryStream()) using (var writer = new BinaryWriter(memory,Encoding.UTF8,true)) {
            writer.Write(files.Length);
            foreach (var file in files) { writer.Write(file.Name); writer.Write(file.Bytes); writer.Write(file.Hash); }
            writer.Flush(); return ProtectedData.Protect(memory.ToArray(),Entropy(root),DataProtectionScope.CurrentUser);
        }
    }
    internal static List<Member> Decode(string root, byte[] bytes) {
        Rules.Need(bytes.Length <= 256*1024, "维护记录超限。"); var files = new List<Member>();
        using (var memory = new MemoryStream(ProtectedData.Unprotect(bytes,Entropy(root),DataProtectionScope.CurrentUser))) using (var reader = new BinaryReader(memory,Encoding.UTF8)) {
            int count = reader.ReadInt32(); Rules.Need(count >= 0 && count <= 512,"维护记录数量异常。");
            for (int i=0;i<count;++i) files.Add(new Member(reader.ReadString(),reader.ReadInt32(),reader.ReadString()));
            Rules.Need(memory.Position == memory.Length,"维护记录有多余数据。");
        }
        CheckFiles(root,files); return files;
    }
    internal static List<Member> ReadLedger(string root) {
        var path = Path.Combine(root,Ledger); if (!File.Exists(path)) return new List<Member>();
        using (var read = new Paths()) return Decode(root,read.Read(path,256*1024));
    }
    internal static void Record(string root, IEnumerable<string> paths) {
        var files = ReadLedger(root).ToDictionary(x=>x.Name,StringComparer.OrdinalIgnoreCase);
        using (var guards = new Paths()) {
            guards.Ancestors(root);
            foreach (var path in paths.Where(p=>p!=null)) {
                guards.Ancestors(Path.GetDirectoryName(path)); var bytes=guards.Read(path,64*1024*1024);
                var file=new Member(path,bytes.Length,Rules.Hash(bytes)); Rules.Need(Allowed(root,file),"维护文件路径不匹配。"); files[path]=file;
            }
            var target=Path.Combine(root,Ledger); var pending=target+"."+Guid.NewGuid().ToString("N"); Paths.WriteNew(pending,Encode(root,files.Values));
            if (File.Exists(target)) File.Replace(pending,target,null); else File.Move(pending,target);
        }
    }
    static Member Identity(string path, int limit) { using (var read=new Paths()) { var bytes=read.Read(path,limit); return new Member(path,bytes.Length,Rules.Hash(bytes)); } }
    static bool Matches(Member file) {
        if (Paths.MissingFile(file.Name)) return false;
        using (var read=new Paths()) { var bytes=read.Read(file.Name,64*1024*1024); return bytes.Length == file.Bytes && Rules.Hash(bytes)==file.Hash; }
    }
    internal static void EmptyParents(string path, string stop) {
        path=Paths.Local(path); stop=Paths.Local(stop);
        Rules.Need(path.StartsWith(stop.TrimEnd('\\')+"\\",StringComparison.OrdinalIgnoreCase),"空目录清理边界不匹配。");
        for (var parent=Path.GetDirectoryName(path); parent!=null && parent!=stop; parent=Path.GetDirectoryName(parent)) {
            Paths.Local(parent); if (!Directory.Exists(parent)) continue;
            if (Directory.EnumerateFileSystemEntries(parent).Any()) break;
            using (var guards=new Paths()) { guards.Ancestors(Path.GetDirectoryName(parent)); Directory.Delete(parent,false); }
        }
    }
    internal static void RemovePackage(string package) {
        if (package == null) return;
        using (var guards=new Paths()) {
            guards.Ancestors(package);
            var names=SetupInputs.Files.Select(x=>new Member(Path.Combine(package,x.Name.Replace('/','\\')),x.Bytes,x.Hash)).ToList();
            if (EmbeddedPackage.CopiedSetup!=null) names.Add(EmbeddedPackage.CopiedSetup);
            var record=Encoding.ASCII.GetBytes("{\"kind\":\"embedded-verified-payload\"}\n"); names.Add(new Member(Path.Combine(package,"setup-record.json"),record.Length,Rules.Hash(record)));
            foreach (var file in names) if (Matches(file)) Paths.DeleteRegular(file.Name,file);
        }
        foreach (var file in SetupInputs.Files) EmptyParents(Path.Combine(package,file.Name.Replace('/','\\')),Path.GetDirectoryName(package));
        if (Directory.Exists(package) && !Directory.EnumerateFileSystemEntries(package).Any()) Directory.Delete(package,false);
        EmptyParents(package,Path.GetTempPath().TrimEnd('\\'));
    }
    internal static void EmptyTree(string folder) {
        if (!Directory.Exists(folder)) return;
        using (var guard=new Paths()) {
            guard.DirectoryPin(Paths.Local(folder));
            foreach (var child in Directory.EnumerateDirectories(folder)) EmptyTree(child);
        }
        if (!Directory.EnumerateFileSystemEntries(Paths.Local(folder)).Any()) Directory.Delete(folder,false);
    }
    static string attempt;
    static bool MissingOrMatches(Member file) {
        return Paths.MissingFile(file.Name) || Matches(file);
    }
    static void JobPath(string root,string job) {
        Rules.Need(Path.GetDirectoryName(Paths.Local(job))==CacheRoot(root) && Rules.Match(Path.GetFileName(job),"uninstall-[0-9a-f]{32}"),"卸载收尾路径不匹配。");
    }
    static void AtomicJob(string job,string name,byte[] bytes) {
        using(var guard=new Paths()) {
            guard.Ancestors(job); var path=Path.Combine(job,name); var pending=Path.Combine(job,"write-"+Guid.NewGuid().ToString("N"));
            Paths.WriteNew(pending,bytes); if(File.Exists(path)) File.Replace(pending,path,null); else File.Move(pending,path);
        }
    }
    internal static void RecoverOwnership(string root,byte[] inventory,byte[] owner) {
        Integration.Decode(root,inventory); Decode(root,owner);
        using(var guard=new Paths()) {
            guard.CreateTree(root);
            if(!File.Exists(Path.Combine(root,Integration.Inventory))) Paths.WriteNew(Path.Combine(root,Integration.Inventory),inventory);
            if(!File.Exists(Path.Combine(root,Ledger))) Paths.WriteNew(Path.Combine(root,Ledger),owner);
        }
    }
    internal static void RestoreInventory(string root,string job) {
        JobPath(root,job);
        using(var read=new Paths()) { read.Ancestors(job); RecoverOwnership(root,read.Read(Path.Combine(job,"inventory.bin"),2*1024*1024),read.Read(Path.Combine(job,"owner.bin"),256*1024)); }
    }
    internal static string Start(string root) {
        var priorJob=Integration.PendingUninstall();
        if(priorJob!=null) {
            JobPath(root,priorJob);
            using(var read=new Paths()) Decode(root,read.Read(Path.Combine(priorJob,"files.bin"),256*1024));
            RestoreInventory(root,priorJob);
        }
        byte[] inventory; using(var read=new Paths()) inventory=read.Read(Path.Combine(root,Integration.Inventory),2*1024*1024);
        Integration.Decode(root,inventory);
        // Cache a verified copy of this version so a partially completed
        // uninstall always retains a working, current recovery entry point.
        var source=Assembly.GetExecutingAssembly().Location; var sourceId=Identity(source,64*1024*1024);
        var self=Path.Combine(CacheRoot(root),sourceId.Hash.Substring(0,16),"StarryFishSetup.exe");
        using(var guard=new Paths()) {
            guard.CreateTree(Path.GetDirectoryName(self));
            if(!File.Exists(self)) { byte[] bytes; using(var read=new Paths()) bytes=read.Read(source,sourceId.Bytes,sourceId.Hash); Paths.WriteNew(self,bytes); }
            guard.Read(self,sourceId.Bytes,sourceId.Hash);
        }
        var previous=ReadLedger(root); var adopted=new List<string>{self}; var registered=Integration.RegisteredUninstaller();
        if(registered!=null && File.Exists(registered) && !previous.Any(x=>x.Name==registered)) {
            var file=Identity(registered,64*1024*1024); Rules.Need(Allowed(root,file),"登记卸载器不属于本安装的缓存。"); adopted.Add(registered);
        }
        foreach(var name in new[]{"StarryFish.lnk","卸载 StarryFish.lnk"}) {
            var path=Path.Combine(MenuRoot(),name);
            if(!previous.Any(x=>x.Name==path) && File.Exists(path) && Integration.ShortcutMatches(path,name=="StarryFish.lnk"?Path.Combine(root,"StarryFish.exe"):registered,name=="StarryFish.lnk"?"":"--uninstall")) adopted.Add(path);
        }
        Record(root,adopted); var files=ReadLedger(root);
        foreach(var file in files) Rules.Need(MissingOrMatches(file),"维护文件或快捷方式已改动，未删除；卸载尚未开始，请核对此文件。");
        byte[] owner; using(var read=new Paths()) owner=read.Read(Path.Combine(root,Ledger),256*1024);
        var job=priorJob??Paths.Local(Path.Combine(CacheRoot(root),"uninstall-"+Guid.NewGuid().ToString("N")));
        using(var guard=new Paths()) guard.CreateTree(job);
        AtomicJob(job,"files.bin",Encode(root,files)); AtomicJob(job,"inventory.bin",inventory); AtomicJob(job,"owner.bin",owner);
        attempt=Guid.NewGuid().ToString("N"); Integration.UninstallEntry(root,self,job);
        Func<string,string> quote=text=>"'"+text.Replace("'","''")+"'";
        long parentStart=Process.GetCurrentProcess().StartTime.ToUniversalTime().Ticks;
        var script="$b=[IO.File]::ReadAllBytes("+quote(self)+");$s=[Security.Cryptography.SHA256]::Create();$h=[BitConverter]::ToString($s.ComputeHash($b)).Replace('-','').ToLowerInvariant();if($h -cne "+quote(sourceId.Hash)+"){throw 'Uninstall loader identity changed'};$a=[Reflection.Assembly]::Load($b);$m=$a.GetType('Gbrowser.Setup.CleanUninstall',$true).GetMethod('Finish',[Reflection.BindingFlags]'Static,NonPublic');$null=$m.Invoke($null,@("+quote(root)+","+quote(job)+",[int]"+Process.GetCurrentProcess().Id+",[long]"+parentStart+","+quote(self)+","+quote(attempt)+"));";
        var powershell=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0","powershell.exe");
        using(var child=Process.Start(new ProcessStartInfo {FileName=powershell,Arguments="-NoProfile -NonInteractive -EncodedCommand "+Convert.ToBase64String(Encoding.Unicode.GetBytes(script)),UseShellExecute=false,CreateNoWindow=true,WindowStyle=ProcessWindowStyle.Hidden})) {
            Rules.Need(child!=null,"无法启动卸载器收尾进程。");
            var ready=Path.Combine(job,"ready-"+attempt); var timer=Stopwatch.StartNew();
            while(!File.Exists(ready) && !child.HasExited && timer.ElapsedMilliseconds<60000) Thread.Sleep(100);
            using(var read=new Paths()) Rules.Need(Encoding.ASCII.GetString(read.Read(ready,64))=="READY\n"+attempt+"\n","卸载器收尾未就绪；未开始删除。");
        }
        return job;
    }
    internal static void Commit(string job) {
        Rules.Need(attempt!=null && Rules.Match(attempt,"[0-9a-f]{32}"),"卸载提交代次缺失。");
        AtomicJob(Paths.Local(job),"committed",Encoding.ASCII.GetBytes("COMMITTED\n"+attempt+"\n"));
    }
    // Verified assembly bytes are loaded into the system PowerShell process;
    // no executable/script copy needs to remain after the parent has exited.
    static void Finish(string root,string job,int parent,long parentStart,string self,string nonce) {
        byte[] selfBytes=null; Dictionary<string,byte[]> backups=null; bool committed=false;
        try {
            JobPath(root,job); Rules.Need(Rules.Match(nonce,"[0-9a-f]{32}"),"卸载收尾代次异常。");
            backups=new Dictionary<string,byte[]>(); List<Member> files;
            using(var read=new Paths()) {
                read.Ancestors(job);
                foreach(var name in new[]{"files.bin","inventory.bin","owner.bin"}) backups[name]=read.Read(Path.Combine(job,name),name=="inventory.bin"?2*1024*1024:256*1024);
                files=Decode(root,backups["files.bin"]); Integration.Decode(root,backups["inventory.bin"]); Decode(root,backups["owner.bin"]);
                var own=files.Single(x=>x.Name==self); selfBytes=read.Read(self,own.Bytes,own.Hash);
            }
            var readyBytes=Encoding.ASCII.GetBytes("READY\n"+nonce+"\n");
            using(var guard=new Paths()) { guard.Ancestors(job); Paths.WriteNew(Path.Combine(job,"ready-"+nonce),readyBytes); }
            try { using(var process=Process.GetProcessById(parent)) if(process.StartTime.ToUniversalTime().Ticks==parentStart) process.WaitForExit(); } catch(ArgumentException) { }
            using(var mutex=Lock(root,60000)) {
            var commitBytes=Encoding.ASCII.GetBytes("COMMITTED\n"+nonce+"\n");
            if(File.Exists(Path.Combine(job,"committed"))) using(var read=new Paths()) committed=read.Read(Path.Combine(job,"committed"),64).SequenceEqual(commitBytes);
            if(!committed) return; // Keep the authenticated plan and retry registration.
            foreach(var file in files) Rules.Need(MissingOrMatches(file),"收尾文件发生变化，保留记录并停止。");
            var ownerFile=new Member(Path.Combine(root,Ledger),backups["owner.bin"].Length,Rules.Hash(backups["owner.bin"]));
            Rules.Need(MissingOrMatches(ownerFile),"安装归属记录发生变化。");
            foreach(var file in files.Where(x=>x.Name!=self)) if(!Paths.MissingFile(file.Name)) {
                using(var guard=new Paths()) { guard.Ancestors(Path.GetDirectoryName(file.Name)); Paths.DeleteRegular(file.Name,file); }
                EmptyParents(file.Name,file.Name.StartsWith(MenuRoot()+"\\",StringComparison.OrdinalIgnoreCase)?Path.GetDirectoryName(MenuRoot()):Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
            }
            if(File.Exists(ownerFile.Name)) using(var guard=new Paths()) { guard.Ancestors(root); Paths.DeleteRegular(ownerFile.Name,ownerFile); }
            if(Directory.Exists(root) && !Directory.EnumerateFileSystemEntries(Paths.Local(root)).Any()) using(var guard=new Paths()) { guard.Ancestors(Path.GetDirectoryName(root)); Directory.Delete(root,false); }
            Integration.FinishUnregistration(root,job);
            var ownFile=files.Single(x=>x.Name==self);
            using(var guard=new Paths()) { guard.Ancestors(Path.GetDirectoryName(self)); Paths.DeleteRegular(self,ownFile); }
            EmptyParents(self,Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
            foreach(var path in Directory.EnumerateFiles(job).Where(x=>Rules.Match(Path.GetFileName(x),"ready-[0-9a-f]{32}"))) {
                var bytes=Encoding.ASCII.GetBytes("READY\n"+Path.GetFileName(path).Substring(6)+"\n");
                using(var guard=new Paths()) { guard.Ancestors(job); Paths.DeleteRegular(path,new Member(path,bytes.Length,Rules.Hash(bytes))); }
            }
            backups["committed"]=commitBytes;
            foreach(var item in backups) {
                var path=Path.Combine(job,item.Key);
                using(var guard=new Paths()) { guard.Ancestors(job); Paths.DeleteRegular(path,new Member(path,item.Value.Length,Rules.Hash(item.Value))); }
            }
            // Unknown files inserted into the job remain user-owned. Only an
            // empty directory is removed; no recursive deletion is performed.
            if(!Directory.EnumerateFileSystemEntries(Paths.Local(job)).Any()) using(var guard=new Paths()) { guard.Ancestors(Path.GetDirectoryName(job)); Directory.Delete(job,false); }
            EmptyParents(job,Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
            }
        } catch(Exception) {
            if(committed && selfBytes!=null && backups!=null) try {
                using(var guard=new Paths()) {
                    guard.CreateTree(Path.GetDirectoryName(self)); if(!File.Exists(self)) Paths.WriteNew(self,selfBytes); guard.Read(self,selfBytes.Length,Rules.Hash(selfBytes));
                    guard.CreateTree(job);
                    foreach(var item in backups) if(!File.Exists(Path.Combine(job,item.Key))) Paths.WriteNew(Path.Combine(job,item.Key),item.Value);
                }
                RecoverOwnership(root,backups["inventory.bin"],backups["owner.bin"]); Integration.UninstallEntry(root,self,job);
            } catch(Exception) { }
            MessageBox.Show("卸载器收尾未完成；变化或占用文件及归属计划保留。正常关闭相关程序后可再次运行卸载入口。","StarryFish 卸载",MessageBoxButtons.OK,MessageBoxIcon.Information);
        }
    }
}
}
