using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32;

namespace Gbrowser.Setup {
internal static class Integration {
    const string Key = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\StarryFish";
    internal const string Inventory = "setup-inventory.bin";
    internal static string Registered() {
        using (var key = Registry.CurrentUser.OpenSubKey(Key)) {
            var path = key == null ? null : key.GetValue("InstallLocation") as string;
            return string.IsNullOrEmpty(path) ? null : Paths.Local(path);
        }
    }
    internal static string StatePath(string root) {
        var digest = Rules.Hash(Encoding.Unicode.GetBytes(Paths.Local(root).ToLowerInvariant())).Substring(0, 16);
        return Paths.Local(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "StarryFish", "Installations", digest));
    }
    internal static string RegisteredUninstaller() {
        using (var key=Registry.CurrentUser.OpenSubKey(Key)) {
            var text=key==null?null:key.GetValue("UninstallString") as string;
            if (text==null || !text.StartsWith("\"",StringComparison.Ordinal) || !text.EndsWith("\" --uninstall",StringComparison.Ordinal)) return null;
            return Paths.Local(text.Substring(1,text.Length-14));
        }
    }
    internal static string PendingUninstall() { using(var key=Registry.CurrentUser.OpenSubKey(Key)) return key==null?null:key.GetValue("PendingCleanup") as string; }
    internal static void UninstallEntry(string root, string executable, string job) {
        var prior=Registered(); Rules.Need(prior==null || prior.Equals(root,StringComparison.OrdinalIgnoreCase),"卸载登记已指向其他安装，停止收尾。");
        using (var key=Registry.CurrentUser.CreateSubKey(Key)) {
            key.SetValue("DisplayName","StarryFish"); key.SetValue("DisplayVersion",SetupInputs.Version); key.SetValue("Publisher","kanajin"); key.SetValue("InstallLocation",root);
            key.SetValue("UninstallString","\""+Paths.Local(executable)+"\" --uninstall");
            key.SetValue("NoModify",1,RegistryValueKind.DWord); key.SetValue("NoRepair",1,RegistryValueKind.DWord);
            if (job!=null) key.SetValue("PendingCleanup",Paths.Local(job));
        }
    }
    internal static void FinishUnregistration(string root,string job) {
        var prior=Registered(); Rules.Need(prior!=null && prior.Equals(root,StringComparison.OrdinalIgnoreCase) && PendingUninstall()==job,"卸载登记已变化，停止收尾。");
        Registry.CurrentUser.DeleteSubKey(Key,false);
    }
    static string MenuFolder() { return Paths.Local(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "StarryFish")); }
    static byte[] Entropy(string root) { return Encoding.UTF8.GetBytes("StarryFish.InstallInventory.v1\n" + Paths.Local(root).ToLowerInvariant()); }
    internal static byte[] Encode(string root, IList<Member> files) {
        using (var data = new MemoryStream()) using (var writer = new BinaryWriter(data, Encoding.UTF8, true)) {
            Rules.Need(files.Count > 0 && files.Count <= 8192, "安装记录数量超限。");
            writer.Write(files.Count);
            foreach (var file in files) { Rules.Member(file.Name); writer.Write(file.Name); writer.Write(file.Bytes); writer.Write(file.Hash); }
            writer.Flush(); Rules.Need(data.Length < 2 * 1024 * 1024 - 4096, "安装记录容量超限。");
            return ProtectedData.Protect(data.ToArray(), Entropy(root), DataProtectionScope.CurrentUser);
        }
    }
    internal static List<Member> Decode(string root, byte[] data) {
        var result = new List<Member>(); var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using (var input = new MemoryStream(ProtectedData.Unprotect(data, Entropy(root), DataProtectionScope.CurrentUser)))
        using (var reader = new BinaryReader(input, Encoding.UTF8)) {
            var count = reader.ReadInt32(); Rules.Need(count > 0 && count <= 8192, "安装记录数量异常。");
            for (var i = 0; i < count; ++i) {
                var name = reader.ReadString(); var size = reader.ReadInt32(); var hash = reader.ReadString(); Rules.Member(name);
                Rules.Need(seen.Add(name) && size >= 0 && size <= 64 * 1024 * 1024 && Rules.Match(hash, "[0-9a-f]{64}"), "安装记录异常。");
                result.Add(new Member(name, size, hash));
            }
            Rules.Need(input.Position == input.Length && seen.Contains("StarryFish.exe"), "安装记录不完整。");
        }
        return result;
    }
    internal static void Capture(string root, bool firstInstall = false) {
        var files = new List<Member>();
        using (var guards = new Paths()) {
            guards.Ancestors(root);
            var application = ApplicationOwner.Decode(root, guards.Read(Path.Combine(root, ApplicationOwner.Name), ApplicationOwner.Limit));
            var expected = application.Files.ToDictionary(file => file.Name, file => file, StringComparer.Ordinal);
            var priorPath = Path.Combine(root, Inventory);
            if (File.Exists(priorPath)) foreach (var file in Decode(root, guards.Read(priorPath, 2 * 1024 * 1024)))
                if (!expected.ContainsKey(file.Name)) expected.Add(file.Name, file);
            var catalogue = ManualReload.Decode(root, guards.Read(Path.Combine(root, "component-catalogue.bin"), 16384));
            var roles = new[] { "zeroomega", "tarou", "arcarum-dungeon" };
            for (var i = 0; i < roles.Length; ++i) {
                var tree = ManualReload.ReadTree(Path.Combine(root, "Components", roles[i]), guards);
                // Only a fully admitted tree may supply new owned members. Extra
                // files invalidate that equality and remain outside our inventory.
                if (tree.Tree != catalogue[i].Tree || tree.Bytes != catalogue[i].Bytes || tree.Files.Count != catalogue[i].Files.Count) continue;
                foreach (var file in tree.Files) {
                    var name = "Components/" + roles[i] + "/" + file.Key;
                    expected[name] = new Member(name, file.Value.Length, Rules.Hash(file.Value));
                }
            }
            if (firstInstall) foreach (var name in new[] { "InstallEvidence/application.json", "InstallEvidence/tarou-record.json", "InstallEvidence/tarou.zip", "InstallEvidence/arcarum-dungeon-record.json", "InstallEvidence/arcarum-dungeon.zip" }) {
                var bytes = guards.Read(Path.Combine(root, name.Replace('/', '\\')), 64 * 1024 * 1024);
                expected[name] = new Member(name, bytes.Length, Rules.Hash(bytes));
            }
            foreach (var file in expected.Values) {
                var path = Path.Combine(root, file.Name.Replace('/', '\\'));
                if (!File.Exists(path)) continue;
                var bytes = guards.Read(path, 64 * 1024 * 1024);
                if (bytes.Length == file.Bytes && Rules.Hash(bytes) == file.Hash) files.Add(file);
            }
            foreach (var file in EvidenceFiles(root,guards)) if (!files.Any(x=>x.Name==file.Name)) files.Add(file);
            // These exact metadata files belong to this installer, not arbitrary
            // files subsequently placed in a component or evidence directory.
            foreach (var name in new[] { ApplicationOwner.Name, "component-catalogue.bin", "install-choice.state" }) {
                if (!File.Exists(Path.Combine(root, name))) continue;
                files.RemoveAll(file => file.Name == name);
                var bytes = guards.Read(Path.Combine(root, name), 2 * 1024 * 1024);
                files.Add(new Member(name, bytes.Length, Rules.Hash(bytes)));
            }
        }
        var inventory = Path.Combine(root, Inventory); var pending = inventory + "." + Guid.NewGuid().ToString("N");
        Paths.WriteNew(pending, Encode(root, files));
        if (File.Exists(inventory)) File.Replace(pending, inventory, null); else File.Move(pending, inventory);
    }
    static void PreflightData(string root) {
        var queue = new Queue<string>(); queue.Enqueue(root); int entries = 0;
        while (queue.Count > 0) foreach (var path in Directory.EnumerateFileSystemEntries(queue.Dequeue())) {
            Rules.Need(++entries <= 100000 && (File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0, "目录包含链接或文件过多；保留现场。");
            Paths.Local(path);
            if (Directory.Exists(path)) queue.Enqueue(path); else Paths.RequireRegular(path);
        }
    }
    internal static void Shortcut(string path, string executable, string arguments) {
        object shell = null, link = null;
        try {
            shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell", true));
            link = shell.GetType().InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            foreach (var item in new Dictionary<string, string> { { "TargetPath", executable }, { "Arguments", arguments },
                { "WorkingDirectory", Path.GetDirectoryName(executable) }, { "IconLocation", executable + ",0" } })
                link.GetType().InvokeMember(item.Key, BindingFlags.SetProperty, null, link, new object[] { item.Value });
            link.GetType().InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
        } finally { if (link != null) Marshal.FinalReleaseComObject(link); if (shell != null) Marshal.FinalReleaseComObject(shell); }
    }
    internal static bool ShortcutMatches(string path, string executable, string arguments) {
        if (executable==null) return false; object shell=null,link=null;
        try {
            Paths.RequireRegular(path); shell=Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell",true));
            link=shell.GetType().InvokeMember("CreateShortcut",BindingFlags.InvokeMethod,null,shell,new object[]{path});
            var target=link.GetType().InvokeMember("TargetPath",BindingFlags.GetProperty,null,link,null) as string;
            var args=link.GetType().InvokeMember("Arguments",BindingFlags.GetProperty,null,link,null) as string;
            return string.Equals(target,executable,StringComparison.OrdinalIgnoreCase) && args==arguments;
        } finally { if(link!=null) Marshal.FinalReleaseComObject(link); if(shell!=null) Marshal.FinalReleaseComObject(shell); }
    }
    internal static void Register(string root) {
        var prior = Registered(); Rules.Need(prior == null || prior.Equals(root, StringComparison.OrdinalIgnoreCase), "已有其他位置的 StarryFish，请先卸载，避免改变原资料位置。");
        using (var guards = new Paths()) {
            guards.Ancestors(root); guards.ExclusiveApplication(root);
            Capture(root);
            // A stable, per-build maintenance cache permits removing installed
            // files without deleting a running executable. It is not user data.
            var bytes = guards.Read(Assembly.GetExecutingAssembly().Location, 64 * 1024 * 1024);
            var oldSetup=RegisteredUninstaller();
            CleanUninstall.RecordState(root);
            var cache = Paths.Local(Path.Combine(CleanUninstall.CacheRoot(root), Rules.Hash(bytes).Substring(0, 16)));
            guards.CreateTree(cache); var setup = Path.Combine(cache, "StarryFishSetup.exe");
            if (!File.Exists(setup)) Paths.WriteNew(setup, bytes);
            guards.Read(setup, bytes.Length, Rules.Hash(bytes));
            var menu = MenuFolder(); guards.CreateTree(menu);
            Shortcut(Path.Combine(menu, "StarryFish.lnk"), Path.Combine(root, "StarryFish.exe"), "");
            Shortcut(Path.Combine(menu, "卸载 StarryFish.lnk"), setup, "--uninstall");
            var known=new List<string>{setup,Path.Combine(menu,"StarryFish.lnk"),Path.Combine(menu,"卸载 StarryFish.lnk")};
            if (oldSetup!=null && File.Exists(oldSetup)) known.Add(oldSetup);
            CleanUninstall.Record(root,known);
            using (var key = Registry.CurrentUser.CreateSubKey(Key)) {
                key.SetValue("DisplayName", "StarryFish"); key.SetValue("DisplayVersion", SetupInputs.Version);
                key.SetValue("Publisher", "kanajin"); key.SetValue("InstallLocation", root);
                key.SetValue("DisplayIcon", Path.Combine(root, "StarryFish.exe") + ",0");
                key.SetValue("UninstallString", "\"" + setup + "\" --uninstall");
                key.SetValue("URLInfoAbout", "https://github.com/kanajin/StarryFish");
                key.SetValue("NoModify", 1, RegistryValueKind.DWord); key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
            }
        }
    }
    internal static int RemoveOwned(string root, bool removeData = false, bool keepInventory = false) {
        var directories = new HashSet<string>(StringComparer.OrdinalIgnoreCase); var retained = 0;
        var histories=new HashSet<string>(StringComparer.Ordinal);
        using (var guard = new Paths()) {
            guard.Ancestors(root); guard.ExclusiveApplication(root);
            List<Member> files;
            using (var read = new Paths()) files = Decode(root, read.Read(Path.Combine(root, Inventory), 2 * 1024 * 1024));
            files.AddRange(StaticFiles(root,guard));
            foreach (var file in EvidenceFiles(root,guard)) if (!files.Any(x=>x.Name==file.Name)) files.Add(file);
            foreach (var file in files.Where(x=>x.Name.StartsWith("ApplicationUpdateEvidence/",StringComparison.Ordinal)||x.Name.StartsWith("ManualReloadEvidence/",StringComparison.Ordinal))) {
                var parts=file.Name.Split('/'); histories.Add(Path.Combine(root,parts[0],parts[1]));
            }
            // Preflight the complete list before deleting anything. Changed or
            // unknown files remain, so uninstall cannot consume user additions.
            var removable = new List<KeyValuePair<string, Member>>();
            foreach (var file in files) {
                var path = Paths.Local(Path.Combine(root, file.Name.Replace('/', '\\')));
                if (Paths.MissingFile(path)) continue;
                // Large caches must not hold every file open at once. Deletion
                // rechecks this exact member on its own non-sharing write handle.
                byte[] bytes; using (var read = new Paths()) bytes = read.Read(path, 64 * 1024 * 1024);
                if (bytes.Length == file.Bytes && Rules.Hash(bytes) == file.Hash) removable.Add(new KeyValuePair<string, Member>(path, file)); else ++retained;
                for (var folder = Path.GetDirectoryName(path); folder != root; folder = Path.GetDirectoryName(folder))
                    if (directories.Add(folder)) guard.DirectoryPin(folder);
            }
            if (removeData) RemoveData(root);
            foreach (var file in removable) Paths.DeleteRegular(file.Key, file.Value);
            if (!keepInventory) Paths.DeleteRegular(Path.Combine(root, Inventory));
        }
        var lockPath=Paths.Local(Path.Combine(root,"process.lock"));
        Paths.DeleteRegular(lockPath,new Member(lockPath,0,Rules.Hash(new byte[0])));
        foreach (var folder in histories) using (var pin=new Paths()) { pin.Ancestors(Path.GetDirectoryName(folder)); CleanUninstall.EmptyTree(folder); }
        foreach (var folder in directories.OrderByDescending(path => path.Length))
            if (Directory.Exists(folder) && !Directory.EnumerateFileSystemEntries(Paths.Local(folder)).Any()) Directory.Delete(folder);
        foreach (var known in new[]{"Cache/Static-v1","Cache","Components/tarou","Components/arcarum-dungeon","Components/zeroomega","Components","Assets","Scripts","Licenses","InstallEvidence","ApplicationUpdateEvidence","ManualReloadEvidence"}) {
            var folder=Paths.Local(Path.Combine(root,known.Replace('/','\\')));
            if (Directory.Exists(folder) && !Directory.EnumerateFileSystemEntries(folder).Any()) Directory.Delete(folder,false);
        }
        if (!Directory.EnumerateFileSystemEntries(Paths.Local(root)).Any()) Directory.Delete(root);
        return retained;
    }
    internal static void RemoveData(string root) {
        var state = StatePath(root); if (!Directory.Exists(state)) return;
        CleanUninstall.VerifyState(root);
        DeleteDataTree(state);
        CleanUninstall.EmptyParents(state,Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
    }
    internal static void DeleteDataTree(string state) {
        state = Paths.Local(state);
        using (var parent = new Paths()) {
            parent.Ancestors(Path.GetDirectoryName(state));
            // Preflight all descendants before deletion; recursion below pins
            // each parent until its child has been removed.
            PreflightData(state);
            DeletePinnedTree(state);
        }
    }
    static void DeletePinnedTree(string directory) {
        using (var pin = new Paths()) {
            pin.DirectoryPin(directory);
            foreach (var path in Directory.EnumerateFileSystemEntries(directory)) {
                Paths.Local(path);
                Rules.Need((File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0, "资料包含链接，删除停止。");
                if (Directory.Exists(path)) DeletePinnedTree(path); else Paths.DeleteRegular(path);
            }
        }
        Directory.Delete(Paths.Local(directory), false);
    }
    internal static string FinishCleanup(string root) {
        // Once program removal and OS unregistration have succeeded, a locked
        // metadata remnant does not make the application installed again.
        try {
            var path=Paths.Local(Path.Combine(root,Inventory));
            if (File.Exists(path)) { byte[] bytes; using (var read=new Paths()) bytes=read.Read(path,2*1024*1024); Decode(root,bytes); Paths.DeleteRegular(path,new Member(path,bytes.Length,Rules.Hash(bytes))); }
            if (!Directory.EnumerateFileSystemEntries(Paths.Local(root)).Any()) Directory.Delete(root, false);
            return "";
        } catch (IOException) { return "\r\n程序已移除；少量卸载记录暂被占用，保留在原目录。"; }
          catch (UnauthorizedAccessException) { return "\r\n程序已移除；少量卸载记录因权限限制保留在原目录。"; }
    }
    internal static SetupResult Uninstall() {
        string root=null,job=null,entry=null;
        IDisposable mutex=null;
        try {
            root = Registered(); Rules.Need(root != null, "没有找到已登记的 StarryFish 安装；未猜测目录或删除文件。");
            mutex=CleanUninstall.Lock(root);
            job=CleanUninstall.Start(root); entry=RegisteredUninstaller();
            CleanUninstall.RestoreInventory(root,job);
            var retained = RemoveOwned(root, true, true);
            var cleanup = FinishCleanup(root);
            Rules.Need(cleanup.Length==0,"卸载记录尚未清理完成。");
            CleanUninstall.Commit(job);
            return new SetupResult(0, "此安装的个人资料和配置已删除，可核实归属的程序及资源缓存已清理。\r\n关闭此窗口后完成开始菜单、安装器缓存及卸载器收尾；收尾失败会提示并保留可重试入口。\r\n无关文件、无法核实归属或被用户替换的文件保留。" +
                (retained > 0 ? "\r\n发现变化文件，未将其当作程序文件删除。" : ""));
        } catch (Exception error) {
            var recovery="关闭此窗口后可再次运行卸载入口。";
            if (root!=null && job!=null && entry!=null) try { UninstallEntry(root,entry,job); } catch (Exception) { recovery="卸载入口恢复失败，收尾计划保留在："+job; }
            return new SetupResult(1, "卸载未完成：" + error.Message + "\r\n归属记录和收尾计划保留；"+recovery+"不要手动删除未知文件。");
        }
        finally { if(mutex!=null) mutex.Dispose(); }
    }
    internal static List<Member> StaticFiles(string root, Paths guards) {
        var files=new List<Member>(); var folder=Path.Combine(root,"Cache","Static-v1"); if (!Directory.Exists(folder)) return files;
        guards.Ancestors(folder); int count=0;
        foreach (var path in Directory.EnumerateFileSystemEntries(folder)) {
            Rules.Need(++count<=240000,"缓存清理数量超限。"); var name=Path.GetFileName(path);
            if (!Rules.Match(name,"[0-9a-f]{64}\\.(?:sfc|tmp)")) continue;
            using (var read=new Paths()) {
                var bytes=read.Read(path,8*1024*1024+32768); var magic=Encoding.ASCII.GetBytes("STARRYFISH-CACHE-1\n");
                if (bytes.Length<=magic.Length+65 || !bytes.Take(magic.Length).SequenceEqual(magic) || bytes[magic.Length+64]!=10) continue;
                var digest=Encoding.ASCII.GetString(bytes,magic.Length,64); var payload=bytes.Skip(magic.Length+65).ToArray();
                if (!Rules.Match(digest,"[0-9a-f]{64}") || Rules.Hash(payload)!=digest) continue;
                var first=Array.IndexOf(payload,(byte)10); if (first<0 || first>=150) continue;
                var fields=Encoding.ASCII.GetString(payload,0,first).Split(' '); int urlLength=0,headerLength=0,bodyLength=0; ulong stored=0,expires=0;
                if(fields.Length!=5 || !ulong.TryParse(fields[0],out stored) || !ulong.TryParse(fields[1],out expires) || expires<=stored || expires-stored>604800 ||
                    !int.TryParse(fields[2],out urlLength) || urlLength<1 || urlLength>4096 || !int.TryParse(fields[3],out headerLength) || headerLength<1 || headerLength>16384 ||
                    !int.TryParse(fields[4],out bodyLength) || bodyLength<1 || bodyLength>8*1024*1024 || payload.Length!=first+1+urlLength+headerLength+bodyLength) continue;
                if(fields[0]!=stored.ToString(System.Globalization.CultureInfo.InvariantCulture) || fields[1]!=expires.ToString(System.Globalization.CultureInfo.InvariantCulture) ||
                    fields[2]!=urlLength.ToString() || fields[3]!=headerLength.ToString() || fields[4]!=bodyLength.ToString()) continue;
                var url=payload.Skip(first+1).Take(urlLength).ToArray(); if(Rules.Hash(url)!=name.Substring(0,64) || url.Any(x=>x<33 || x>126)) continue;
                var text=Encoding.ASCII.GetString(url);
                if(!Rules.Match(text,@"https://(?:game\.granbluefantasy\.jp|gbf\.game\.mbga\.jp|prd-game-a-granbluefantasy\.akamaized\.net)/(?:assets|assets_en)/[A-Za-z0-9_./-]+(?:\?(?:(?:v|ver|version|t)=)?[0-9A-Fa-f._-]{1,64})?") || text.Split('?')[0].Contains("..") || text.Substring(8).Contains("//")) continue;
                var headers=Encoding.ASCII.GetString(payload,first+1+urlLength,headerLength);
                var suffix=Path.GetExtension(text.Split('?')[0]).ToLowerInvariant(); string mime;
                switch(suffix) {
                    case ".png":mime="image/png";break; case ".jpg":case ".jpeg":mime="image/jpeg";break; case ".gif":mime="image/gif";break; case ".webp":mime="image/webp";break;
                    case ".js":mime="(?:text|application)/javascript";break; case ".css":mime="text/css";break; case ".woff":case ".woff2":mime="(?:font/woff2?|application/font-woff)";break;
                    case ".mp3":mime="audio/mpeg";break; case ".ogg":mime="audio/ogg";break; default:continue;
                }
                if(!System.Text.RegularExpressions.Regex.IsMatch(headers,"(?:^|\\r\\n)content-type: "+mime+"(?:;[^\\r\\n]*)?\\r\\n",System.Text.RegularExpressions.RegexOptions.IgnoreCase|System.Text.RegularExpressions.RegexOptions.CultureInvariant) || headers.IndexOf('\0')>=0) continue;
                files.Add(new Member("Cache/Static-v1/"+name,bytes.Length,Rules.Hash(bytes)));
            }
        }
        return files;
    }
    internal static List<Member> EvidenceFiles(string root, Paths guards) {
        var result=new List<Member>();
        Action<string,Member> add=(prefix,file)=> {
            var name=prefix+file.Name; var path=Path.Combine(root,name.Replace('/','\\'));
            if (!File.Exists(path)) return;
            using (var read=new Paths()) { var bytes=read.Read(path,64*1024*1024); if (bytes.Length==file.Bytes && Rules.Hash(bytes)==file.Hash) result.Add(new Member(name,file.Bytes,file.Hash)); }
        };
        Action<string,byte[]> metadata=(name,bytes)=>add("",new Member(name,bytes.Length,Rules.Hash(bytes)));
        foreach (var kind in new[]{"ApplicationUpdateEvidence","ManualReloadEvidence"}) {
            var parent=Path.Combine(root,kind); if (!Directory.Exists(parent)) continue; guards.Ancestors(parent); int count=0;
            foreach (var folder in Directory.EnumerateDirectories(parent)) {
                Rules.Need(++count<=1024,"维护历史数量超限。"); if (!Rules.Match(Path.GetFileName(folder),"[0-9a-f]{32}")) continue; guards.DirectoryPin(folder);
                var prefix=kind+"/"+Path.GetFileName(folder)+"/";
                if (kind=="ApplicationUpdateEvidence") {
                    foreach (var ownerName in new[]{"previous-owner.bin","next-owner.bin"}) {
                        var ownerPath=Path.Combine(folder,ownerName); if (!File.Exists(ownerPath)) continue;
                        using (var read=new Paths()) {
                            var bytes=read.Read(ownerPath,ApplicationOwner.Limit); var owner=ApplicationOwner.Decode(root,bytes); metadata(prefix+ownerName,bytes);
                            foreach (var file in owner.Files) foreach (var section in ownerName=="previous-owner.bin"?new[]{"previous/","retired/"}:new[]{"next/"}) add(prefix+section,file);
                        }
                    }
                    var catalogue=Path.Combine(folder,"component-catalogue.bin");
                    if (File.Exists(catalogue)) using (var read=new Paths()) { var bytes=read.Read(catalogue,16384); ManualReload.Decode(root,bytes); metadata(prefix+"component-catalogue.bin",bytes); }
                    var committed=Encoding.ASCII.GetBytes("APPLICATION_PAYLOAD_VERIFIED\nHELPERS_AND_PROFILE_UNTOUCHED\n");
                    add(prefix,new Member("committed.txt",committed.Length,Rules.Hash(committed)));
                } else {
                    bool admitted=false;
                    foreach (var name in new[]{"previous-catalogue.bin","accepted-catalogue.bin","pending-catalogue.bin"}) {
                        var path=Path.Combine(folder,name); if (!File.Exists(path)) continue;
                        using (var read=new Paths()) { var bytes=read.Read(path,16384); ManualReload.Decode(root,bytes); metadata(prefix+name,bytes); admitted=true; }
                    }
                    if (!admitted) continue;
                    var prepared=Path.Combine(folder,"prepared.json"); if (File.Exists(prepared)) using (var read=new Paths()) {
                        var bytes=read.Read(prepared,16384); var record=Rules.Object(Json.Read(bytes,16384));
                        if (Rules.Text(record,"kind")=="EXPLICIT_LOCAL_MANUAL_RELOAD" && Rules.Text(record,"source_identity")=="USER_OVERWRITE_NOT_AUTHOR_SIGNATURE") metadata(prefix+"prepared.json",bytes);
                    }
                    var committed=Encoding.ASCII.GetBytes("ACCEPTED_SOURCE_FOR_NEXT_NATIVE_RELOAD\nNO_DOWNLOAD_PROFILE_ACCESS_BROWSER_START\nDISABLED_EXTENSIONS_MUST_NOT_BE_REINSTALLED\n");
                    add(prefix,new Member("committed.txt",committed.Length,Rules.Hash(committed)));
                }
            }
        }
        var pending=Path.Combine(root,ApplicationOwner.Pending);
        if(File.Exists(pending)) using(var read=new Paths()) {
            var bytes=read.Read(pending,256); var text=Encoding.ASCII.GetString(bytes);
            if(Rules.Match(text,"EXPLICIT_APPLICATION_UPDATE\n[0-9a-f]{32}\n")) {
                var id=text.Split('\n')[1];
                if(result.Any(x=>x.Name=="ApplicationUpdateEvidence/"+id+"/previous-owner.bin" || x.Name=="ApplicationUpdateEvidence/"+id+"/next-owner.bin")) metadata(ApplicationOwner.Pending,bytes);
            }
        }
        return result;
    }
}
}
