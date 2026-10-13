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
    static List<string> TreeFiles(string root) {
        var result = new List<string>(); var queue = new Queue<string>(); queue.Enqueue(root); int entries = 0;
        while (queue.Count > 0) foreach (var path in Directory.EnumerateFileSystemEntries(queue.Dequeue())) {
            Rules.Need(++entries <= 100000 && (File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0, "目录包含链接或文件过多；保留现场。");
            Paths.Local(path);
            if (Directory.Exists(path)) queue.Enqueue(path); else result.Add(path);
        }
        return result;
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
    internal static void Register(string root) {
        var prior = Registered(); Rules.Need(prior == null || prior.Equals(root, StringComparison.OrdinalIgnoreCase), "已有其他位置的 StarryFish，请先卸载，避免改变原资料位置。");
        using (var guards = new Paths()) {
            guards.Ancestors(root); guards.ExclusiveApplication(root);
            Capture(root);
            // A stable, per-build maintenance cache permits removing installed
            // files without deleting a running executable. It is not user data.
            var bytes = guards.Read(Assembly.GetExecutingAssembly().Location, 64 * 1024 * 1024);
            var cache = Paths.Local(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "StarryFish", "SetupCache", Rules.Hash(bytes).Substring(0, 16)));
            guards.CreateTree(cache); var setup = Path.Combine(cache, "StarryFishSetup.exe");
            if (!File.Exists(setup)) Paths.WriteNew(setup, bytes);
            guards.Read(setup, bytes.Length, Rules.Hash(bytes));
            var menu = MenuFolder(); guards.CreateTree(menu);
            Shortcut(Path.Combine(menu, "StarryFish.lnk"), Path.Combine(root, "StarryFish.exe"), "");
            Shortcut(Path.Combine(menu, "卸载 StarryFish.lnk"), setup, "--uninstall");
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
        using (var guard = new Paths()) {
            guard.Ancestors(root); guard.ExclusiveApplication(root);
            List<Member> files;
            using (var read = new Paths()) files = Decode(root, read.Read(Path.Combine(root, Inventory), 2 * 1024 * 1024));
            // Preflight the complete list before deleting anything. Changed or
            // unknown files remain, so uninstall cannot consume user additions.
            var removable = new List<string>();
            using (var read = new Paths()) foreach (var file in files) {
                var path = Paths.Local(Path.Combine(root, file.Name.Replace('/', '\\')));
                if (!File.Exists(path)) { Rules.Need(!Directory.Exists(path), "文件已变为目录，卸载停止。"); continue; }
                var bytes = read.Read(path, 64 * 1024 * 1024);
                if (bytes.Length == file.Bytes && Rules.Hash(bytes) == file.Hash) removable.Add(path); else ++retained;
                for (var folder = Path.GetDirectoryName(path); folder != root; folder = Path.GetDirectoryName(folder))
                    if (directories.Add(folder)) guard.DirectoryPin(folder);
            }
            if (removeData) RemoveData(root);
            foreach (var path in removable) File.Delete(path);
            if (!keepInventory) File.Delete(Path.Combine(root, Inventory));
        }
        File.Delete(Paths.Local(Path.Combine(root, "process.lock")));
        foreach (var folder in directories.OrderByDescending(path => path.Length))
            if (Directory.Exists(folder) && !Directory.EnumerateFileSystemEntries(Paths.Local(folder)).Any()) Directory.Delete(folder);
        if (!Directory.EnumerateFileSystemEntries(Paths.Local(root)).Any()) Directory.Delete(root); else ++retained;
        return retained;
    }
    internal static void RemoveData(string root) {
        var state = StatePath(root); if (!Directory.Exists(state)) return;
        DeleteDataTree(state);
    }
    internal static void DeleteDataTree(string state) {
        state = Paths.Local(state);
        using (var parent = new Paths()) {
            parent.Ancestors(Path.GetDirectoryName(state));
            // Preflight all descendants before deletion; recursion below pins
            // each parent until its child has been removed.
            TreeFiles(state);
            DeletePinnedTree(state);
        }
    }
    static void DeletePinnedTree(string directory) {
        using (var pin = new Paths()) {
            pin.DirectoryPin(directory);
            foreach (var path in Directory.EnumerateFileSystemEntries(directory)) {
                Paths.Local(path);
                Rules.Need((File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0, "资料包含链接，删除停止。");
                if (Directory.Exists(path)) DeletePinnedTree(path); else File.Delete(path);
            }
        }
        Directory.Delete(Paths.Local(directory), false);
    }
    internal static string FinishCleanup(string root) {
        // Once program removal and OS unregistration have succeeded, a locked
        // metadata remnant does not make the application installed again.
        try {
            File.Delete(Paths.Local(Path.Combine(root, Inventory)));
            if (!Directory.EnumerateFileSystemEntries(Paths.Local(root)).Any()) Directory.Delete(root, false);
            return "";
        } catch (IOException) { return "\r\n程序已移除；少量卸载记录暂被占用，保留在原目录。"; }
          catch (UnauthorizedAccessException) { return "\r\n程序已移除；少量卸载记录因权限限制保留在原目录。"; }
    }
    internal static SetupResult Uninstall(bool removeData) {
        try {
            var root = Registered(); Rules.Need(root != null, "没有找到已登记的 StarryFish 安装。");
            var retained = RemoveOwned(root, removeData, true);
            var menu = MenuFolder();
            foreach (var name in new[] { "StarryFish.lnk", "卸载 StarryFish.lnk" }) { var path = Paths.Local(Path.Combine(menu, name)); if (File.Exists(path)) File.Delete(path); }
            if (Directory.Exists(menu) && !Directory.EnumerateFileSystemEntries(menu).Any()) Directory.Delete(menu);
            Registry.CurrentUser.DeleteSubKey(Key, false);
            var cleanup = FinishCleanup(root);
            return new SetupResult(0, "StarryFish 已卸载。\r\n" + (removeData ? "此安装的个人资料已删除。" : "账号资料和配置已保留。") +
                (retained > 0 ? "\r\n安装目录内有变化或额外文件，已保留这些文件。" : "") + cleanup + "\r\n安装器缓存保留，便于后续维护。");
        } catch (Exception error) { return new SetupResult(1, "卸载未完成：" + error.Message + "\r\n请关闭浏览器后重试；不要手动删除未知文件。"); }
    }
}
}
