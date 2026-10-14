// Small per-user installer and explicit maintenance. Its progress window may
// stay open after launch is requested; the game host stays native C++.
// No profile access/migration.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace Gbrowser.Setup {
internal static class Prerequisites {
    internal static void CheckValues(int? framework, string machine, string user) {
        // 528040 is the smallest .NET 4.8 Release value; later installed
        // framework versions remain valid. No registry/updater policy is changed.
        Rules.Need(framework.HasValue && framework.Value >= 528040,
            "Install Microsoft .NET Framework 4.8 or later before StarryFish setup");
        Version value;
        bool available = new[] { machine, user }.Any(text => Version.TryParse(text, out value) && value.Major >= 154);
        // 154 is this candidate's tested baseline, not certification of every
        // runtime build. The native host still feature-detects required APIs.
        Rules.Need(available, "Install/update Microsoft Evergreen WebView2 Runtime (candidate baseline 154+): https://developer.microsoft.com/microsoft-edge/webview2/");
    }
    static object Read(RegistryHive hive, RegistryView view, string path, string name) {
        using (var registry = RegistryKey.OpenBaseKey(hive, view)) using (var key = registry.OpenSubKey(path, false))
            return key == null ? null : key.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames);
    }
    internal static void Check() {
        const string runtime = @"Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
        CheckValues(Read(RegistryHive.LocalMachine, RegistryView.Registry64, @"SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full", "Release") as int?,
            Read(RegistryHive.LocalMachine, RegistryView.Registry32, runtime, "pv") as string,
            Read(RegistryHive.CurrentUser, RegistryView.Registry64, runtime, "pv") as string);
    }
}

internal sealed class Paths : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct Information {
        internal uint Attributes; internal System.Runtime.InteropServices.ComTypes.FILETIME Created, Accessed, Written;
        internal uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetFileInformationByHandle(SafeFileHandle handle, out Information info);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle, StringBuilder result, uint capacity, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool SetFileInformationByHandle(SafeFileHandle handle, int kind, ref int value, uint size);
    readonly List<IDisposable> held = new List<IDisposable>();
    internal static string Local(string path) {
        Rules.Need(Rules.Match(path, "[A-Za-z]:\\\\.*") && path.Length < 240 && Path.GetFullPath(path) == path, "Canonical local path required");
        foreach (var part in path.Substring(3).Split('\\')) if (part.Length != 0) Rules.Part(part);
        var drive = new DriveInfo(Path.GetPathRoot(path)); Rules.Need(drive.DriveType == DriveType.Fixed || drive.DriveType == DriveType.Removable, "Local drive required");
        var current = path;
        while (current != null) {
            if (File.Exists(current) || Directory.Exists(current)) Rules.Need((File.GetAttributes(current) & FileAttributes.ReparsePoint) == 0, "Linked installation path rejected");
            current = Path.GetDirectoryName(current);
        }
        return path;
    }
    internal void DirectoryPin(string path) {
        Local(path);
        var handle = CreateFileW(path, 0x80000000, 3, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
        try {
            Information info = new Information(); var actual = new StringBuilder(32768);
            Rules.Need(!handle.IsInvalid && GetFileInformationByHandle(handle, out info), "Cannot pin installation directory");
            Rules.Need((info.Attributes & 0x10) != 0 && (info.Attributes & 0x400) == 0, "Invalid installation directory");
            var length = GetFinalPathNameByHandleW(handle, actual, (uint)actual.Capacity, 0);
            Rules.Need(length > 0 && length < actual.Capacity && actual.ToString().StartsWith(@"\\?\", StringComparison.Ordinal) &&
                actual.ToString().Substring(4).TrimEnd('\\').Equals(path.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase), "Installation directory binding changed");
            held.Add(handle);
        } catch { handle.Dispose(); throw; }
    }
    internal void Ancestors(string path) {
        var stack = new Stack<string>(); for (var current = Local(path); current != null; current = Path.GetDirectoryName(current)) stack.Push(current);
        foreach (var current in stack) DirectoryPin(current);
    }
    internal void CreateTree(string path) {
        var missing = new Stack<string>(); var current = Local(path);
        while (!Directory.Exists(current)) {
            Rules.Need(!File.Exists(current) && missing.Count < 32, "Installation directory creation rejected");
            missing.Push(current); current = Path.GetDirectoryName(current);
            Rules.Need(current != null, "Existing local ancestor required");
        }
        Ancestors(current); // Pin before creating descendants, never after an unguarded recursive create.
        foreach (var directory in missing) { Directory.CreateDirectory(directory); DirectoryPin(directory); }
    }
    internal void ExclusiveApplication(string root) {
        var path = Local(Path.Combine(root, "process.lock"));
        var handle = CreateFileW(path, 0xc0000000, 0, IntPtr.Zero, 4, 0x00200000, IntPtr.Zero);
        try {
            Information info = new Information();
            Rules.Need(!handle.IsInvalid && GetFileInformationByHandle(handle, out info) &&
                (info.Attributes & (0x10 | 0x400)) == 0 && info.Links == 1, "Close StarryFish before installation maintenance");
            held.Add(handle);
        } catch { handle.Dispose(); throw; }
    }
    internal static void RequireMissing(string path) {
        Local(path);
        using (var handle = CreateFileW(path, 0x80, 7, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero)) {
            // Open the marker itself, including a dangling reparse point. Never
            // treat access/parent/type errors as proof that an update completed.
            Rules.Need(handle.IsInvalid && Marshal.GetLastWin32Error() == 2, "Prior application update outcome needs inspection; files/data retained, no automatic retry");
        }
    }
    internal byte[] Read(string path, int limit, string expected = null) {
        Local(path); var handle = CreateFileW(path, 0x80000000, 1, IntPtr.Zero, 3, 0x00200000, IntPtr.Zero);
        try {
            Information info = new Information(); Rules.Need(!handle.IsInvalid && GetFileInformationByHandle(handle, out info), "Cannot read regular installation file");
            Rules.Need((info.Attributes & (0x10 | 0x400)) == 0 && info.Links == 1 && info.SizeHigh == 0 && info.SizeLow <= limit, "File type/link/size rejected");
            var stream = new FileStream(handle, FileAccess.Read); held.Add(stream);
            var bytes = new byte[(int)info.SizeLow]; int offset = 0;
            while (offset < bytes.Length) { var count = stream.Read(bytes, offset, bytes.Length - offset); Rules.Need(count > 0, "Truncated file"); offset += count; }
            Rules.Need(stream.ReadByte() == -1 && (expected == null || Rules.Hash(bytes) == expected), "File content identity mismatch"); return bytes;
        } catch { handle.Dispose(); throw; }
    }
    public void Dispose() { for (int i = held.Count - 1; i >= 0; --i) held[i].Dispose(); held.Clear(); }
    internal static void RequireRegular(string path) {
        Local(path);
        using (var handle=CreateFileW(path,0x80,1,IntPtr.Zero,3,0x00200000,IntPtr.Zero)) {
            Information info; Rules.Need(!handle.IsInvalid && GetFileInformationByHandle(handle,out info) && (info.Attributes & (0x10|0x400))==0 && info.Links==1,"文件包含链接或不是普通文件。");
        }
    }
    internal static bool MissingFile(string path) {
        Local(path);
        using(var handle=CreateFileW(path,0x80,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero)) {
            if(handle.IsInvalid) { var error=Marshal.GetLastWin32Error(); Rules.Need(error==2 || error==3,"无法核实文件是否缺失，保留现场。"); return true; }
            Information info; Rules.Need(GetFileInformationByHandle(handle,out info) && (info.Attributes & (0x10|0x400))==0 && info.Links==1,"文件类型或链接状态不符合清理要求。"); return false;
        }
    }
    internal static void DeleteRegular(string path, Member expected = null) {
        Local(path);
        using (var handle = CreateFileW(path, 0x80010000, 1, IntPtr.Zero, 3, 0x00200000, IntPtr.Zero)) {
            if (handle.IsInvalid) {
                var error = Marshal.GetLastWin32Error(); if (error == 2) return;
                throw new IOException("无法安全删除 "+Path.GetFileName(path)+"（系统错误 "+error+"），文件保留。");
            }
            Information info = new Information();
            if (!GetFileInformationByHandle(handle, out info)) throw new IOException("无法核实待清理文件，文件保留。");
            Rules.Need((info.Attributes & (0x10 | 0x400)) == 0 && info.Links == 1, "待清理文件包含链接或不是普通文件，文件保留。");
            if (expected != null) {
                Rules.Need(info.SizeHigh == 0 && info.SizeLow == expected.Bytes, "待清理文件已变化。");
                using (var stream = new FileStream(handle, FileAccess.Read)) {
                    using (var sha = SHA256.Create()) Rules.Need(Rules.Hex(sha.ComputeHash(stream)) == expected.Hash, "待清理文件身份已变化。");
                    int deleted = 1; if (!SetFileInformationByHandle(handle, 4, ref deleted, 4)) throw new IOException("文件被占用或无法删除。");
                }
            } else { int deleted = 1; if (!SetFileInformationByHandle(handle, 4, ref deleted, 4)) throw new IOException("文件被占用或无法删除。"); }
        }
    }
    internal static void WriteNew(string path, byte[] bytes) {
        Local(path); Directory.CreateDirectory(Path.GetDirectoryName(path)); Local(path);
        using (var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { stream.Write(bytes, 0, bytes.Length); stream.Flush(true); }
    }
}

internal static class Catalogue {
    // A current-user integrity envelope for this accepted installation's bytes,
    // not an author signature, Chrome permission grant or trust in a JSON receipt.
    // A same-user attacker can also replace executable code; no stronger claim.
    internal static byte[] Encode(string root, IList<CheckedPackage> components) {
        Rules.Need(components.Count == 3, "Exactly three initial roles required");
        using (var memory = new MemoryStream()) using (var writer = new BinaryWriter(memory, Encoding.UTF8, true)) {
            writer.Write(Encoding.ASCII.GetBytes("GBWV01\n"));
            foreach (var component in components) {
                Rules.Need(Rules.Match(component.Tree, "[0-9a-f]{64}") && component.Files.Count > 0 && component.Files.Count <= 1024 && component.Bytes <= Rules.TotalLimit, "Catalogue role budget");
                writer.Write(Encoding.ASCII.GetBytes(component.Tree)); writer.Write((uint)component.Files.Count); writer.Write((uint)component.Bytes);
            }
            writer.Flush();
            var entropy = Encoding.UTF8.GetBytes("GbrowserWebView2.SourceCatalogue.v1\n" + Paths.Local(root).ToLowerInvariant());
            return ProtectedData.Protect(memory.ToArray(), entropy, DataProtectionScope.CurrentUser);
        }
    }
}

internal static class Installer {
    internal static string Destination() {
        return Integration.Registered() ?? DefaultDestination();
    }
    internal static string DefaultDestination() {
        // Product is distinct from the historical Candidate snapshot, which is
        // intentionally retained and never silently upgraded or migrated.
        return Paths.Local(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Programs", "StarryFish", "Product"));
    }
    static CheckedPackage FixedZero(string package, Paths guards) {
        var result = new CheckedPackage(); var hashes = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var item in SetupInputs.Files.Where(item => item.Name.StartsWith("Components/zeroomega/", StringComparison.Ordinal))) {
            var bytes = guards.Read(Paths.Local(Path.Combine(package, item.Name.Replace('/', '\\'))), item.Bytes, item.Hash);
            var name = item.Name.Substring("Components/zeroomega/".Length); result.Files.Add(name, bytes); result.Bytes += bytes.Length; hashes.Add(name, item.Hash);
        }
        result.Tree = Archive.Tree(hashes); result.Version = "3.5.2";
        Rules.Need(result.Tree == SetupInputs.ZeroTree && result.Files.ContainsKey("manifest.json") && result.Files.ContainsKey("options.html"), "Fixed ZeroOmega identity changed"); return result;
    }
    internal static void Inspect(string package) {
        using (var guards = new Paths()) {
            guards.Ancestors(package);
            var expected = new HashSet<string>(SetupInputs.Files.Select(item => item.Name), StringComparer.Ordinal);
            // Setup's own compiled identity is the package trust boundary. The
            // descriptive package-record is not used to select executable code.
            expected.Add("StarryFishSetup.exe"); expected.Add("setup-record.json");
            var queue = new Queue<string>(); queue.Enqueue(package); int visited = 0;
            while (queue.Count > 0) {
                var current = queue.Dequeue(); guards.DirectoryPin(current);
                foreach (var path in Directory.EnumerateFileSystemEntries(current)) {
                    Rules.Need(++visited <= 4096 && (File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0, "Package traversal/link budget");
                    var name = path.Substring(package.Length + 1).Replace('\\', '/');
                    if (Directory.Exists(path)) { Rules.Need(expected.Any(file => file.StartsWith(name + "/", StringComparison.Ordinal)), "Unknown package directory"); queue.Enqueue(path); }
                    else Rules.Need(expected.Remove(name), "Unknown package member");
                }
            }
            Rules.Need(expected.Count == 0, "Package member missing");
            foreach (var item in SetupInputs.Files) guards.Read(Path.Combine(package, item.Name.Replace('/', '\\')), item.Bytes, item.Hash);
            FixedZero(package, guards);
        }
    }
    static void Record(string root, string role, Source source, byte[] archive, CheckedPackage component) {
        var licenses = component.Files.Where(item => Rules.Match(item.Key.Split('/').Last().ToLowerInvariant(), "(?:license|licence|copying)(?:\\.(?:txt|md))?"))
            .Select(item => "{\"path\":" + Json.Quote(item.Key) + ",\"sha256\":" + Json.Quote(Rules.Hash(item.Value)) + "}");
        var record = "{\"schema\":1,\"component\":" + Json.Quote(role) + ",\"status\":\"SOURCE_READY_NATIVE_LOAD_PENDING\",\"repository\":" + Json.Quote(source.Repository) +
            ",\"source_kind\":" + Json.Quote(source.Kind) + ",\"release_tag\":" + Json.Quote(source.Tag) + ",\"source_commit\":" + (source.Commit == null ? "null" : Json.Quote(source.Commit)) +
            ",\"source_url\":" + Json.Quote(source.Url) + ",\"version\":" + Json.Quote(component.Version) + ",\"acquired_at_utc\":" + Json.Quote(DateTime.UtcNow.ToString("o")) +
            ",\"package_sha256\":" + Json.Quote(Rules.Hash(archive)) + ",\"runtime_tree_sha256\":" + Json.Quote(component.Tree) + ",\"files\":" + component.Files.Count +
            ",\"bytes\":" + component.Bytes + ",\"license_evidence\":[" + string.Join(",", licenses) +
            "],\"source_identity\":" + Json.Quote(source.Kind == "LOCAL_USER_ARCHIVE" ? "USER_SELECTED_LOCAL_ZIP_NOT_AUTHOR_SIGNATURE" : "PUBLIC_HTTPS_NOT_AUTHOR_SIGNATURE") + ",\"loading\":\"NOT_PERFORMED\",\"manual_update\":\"MANUAL_OVERWRITE_EXPLICIT_RELOAD\"}\n";
        Paths.WriteNew(Path.Combine(root, "InstallEvidence", role + "-record.json"), Encoding.UTF8.GetBytes(record));
        Paths.WriteNew(Path.Combine(root, "InstallEvidence", role + ".zip"), archive);
    }
    static void PublishComponent(string staging, string role, CheckedPackage component) {
        var root = Path.Combine(staging, "Components", role); Directory.CreateDirectory(root);
        using (var guards = new Paths()) {
            guards.Ancestors(root);
            foreach (var file in component.Files) {
                var target = Paths.Local(Path.Combine(root, file.Key.Replace('/', '\\'))); Paths.WriteNew(target, file.Value);
                guards.Read(target, file.Value.Length, Rules.Hash(file.Value));
            }
        }
    }
    internal static void StageAndPublish(string destination, Action<string> prepare) {
        Paths.Local(destination);
        var parent = Paths.Local(Path.GetDirectoryName(destination));
        var staging = Paths.Local(Path.Combine(parent, ".install-" + Guid.NewGuid().ToString("N")));
        using (var parentGuards = new Paths()) {
            parentGuards.CreateTree(parent);
            if (Directory.Exists(destination) && !Directory.EnumerateFileSystemEntries(destination).Any()) Directory.Delete(destination, false);
            Paths.RequireMissing(destination);
            // Keep ancestors pinned throughout publication, but release the
            // staging directory's no-delete handle before renaming that directory.
            using (var stagingGuards = new Paths()) {
                stagingGuards.CreateTree(staging);
                prepare(staging);
            }
            Paths.Local(staging); Paths.RequireMissing(destination);
            Directory.Move(staging, destination); // Same volume, no replacement; failures retain staging.
        }
    }
    internal static string Install(string package, SetupOptions options, Action<string> progress = null) {
        Inspect(package);
        Prerequisites.Check(); // Stop before downloading or creating staging.
        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData); Paths.Local(local);
        // Keep path-derived extension IDs stable for the later explicit manual
        // overwrite/reload workflow. A second first install still cannot reset it.
        var destination = SetupOptions.ValidateDestination(options.Destination); SetupOptions.ValidateProxy(options.Proxy);
        options.ValidateHelpers();
        Rules.Need((!Directory.Exists(destination) || !Directory.EnumerateFileSystemEntries(destination).Any()) && !File.Exists(destination), "Existing installation retained; no overwrite or reset");
        using (var packageGuards = new Paths()) {
            packageGuards.Ancestors(package);
            StageAndPublish(destination, staging => {
            Console.WriteLine("Installing to a new current-user directory; existing CEF/profiles retained.");
            var components = new List<CheckedPackage> { FixedZero(package, packageGuards) };
            foreach (var role in new[] { "tarou", "arcarum-dungeon" }) {
                if (progress != null) progress(role == "tarou" ? "正在获取太郎安装包…" : "正在获取肉鸽安装包…");
                Source source; byte[] bytes = AcquireHelper(options, role, PublicSource.Fetch, out source);
                Console.WriteLine(role + ": checking ZIP/manifest/content..."); var checkedPackage = Archive.Inspect(bytes, role, source);
                if (progress != null) progress(role == "tarou" ? "正在核对并解压太郎…" : "正在核对并解压肉鸽…");
                PublishComponent(staging, role, checkedPackage); Record(staging, role, source, bytes, checkedPackage); components.Add(checkedPackage);
            }
            PublishComponent(staging, "zeroomega", components[0]);
            Paths.WriteNew(Path.Combine(staging, "install-choice.state"), Encoding.ASCII.GetBytes("GBWV-INSTALL-1\n" + options.Proxy + "\n"));
            if (progress != null) progress("正在核对程序文件并完成安装…");
            using (var stageGuards = new Paths()) {
                stageGuards.Ancestors(staging);
                foreach (var item in SetupInputs.Files.Where(item => !item.Name.StartsWith("Components/", StringComparison.Ordinal))) {
                    var bytes = packageGuards.Read(Path.Combine(package, item.Name.Replace('/', '\\')), item.Bytes, item.Hash);
                    var target = Paths.Local(Path.Combine(staging, item.Name.Replace('/', '\\'))); Paths.WriteNew(target, bytes); stageGuards.Read(target, item.Bytes, item.Hash);
                }
                Paths.WriteNew(Path.Combine(staging, "component-catalogue.bin"), Catalogue.Encode(destination, components));
                var owner = ApplicationOwner.Encode(destination, SetupInputs.HostFingerprint, ApplicationUpdate.CurrentFiles());
                var ownerPath = Path.Combine(staging, ApplicationOwner.Name); Paths.WriteNew(ownerPath, owner);
                stageGuards.Read(ownerPath, ApplicationOwner.Limit, Rules.Hash(owner));
                Paths.WriteNew(Path.Combine(staging, "InstallEvidence", "application.json"), Encoding.ASCII.GetBytes(
                    "{\"input_fingerprint\":" + Json.Quote(SetupInputs.HostFingerprint) + ",\"loading\":\"NOT_PERFORMED\"}\n"));
            }
            });
        }
        Integration.Capture(destination, true);
        Console.WriteLine("Source files ready. Native loading will be performed by the new candidate: " + destination);
        return destination;
    }
    internal static byte[] AcquireHelper(SetupOptions options, string role, Func<string, int, byte[]> fetch, out Source source) {
        if (options.LocalHelpers) {
            var path = SetupOptions.ValidateZip(role == "tarou" ? options.TarouZip : options.ArcarumZip);
            using (var read = new Paths()) {
                read.Ancestors(Path.GetDirectoryName(path));
                var bytes = read.Read(path, Rules.ArchiveLimit);
                Rules.Need(bytes.Length >= 22, "本地 ZIP 文件不完整。");
                source = new Source { Repository = PublicSource.Repository(role), Kind = "LOCAL_USER_ARCHIVE", Tag = "", Url = "", Size = bytes.Length, Hash = Rules.Hash(bytes) };
                return bytes;
            }
        }
        source = PublicSource.Resolve(role, fetch);
        var downloaded = fetch(source.Url, source.Size == 0 ? Rules.ArchiveLimit : source.Size);
        Rules.Need(downloaded.Length >= 22 && (source.Size == 0 || source.Size == downloaded.Length) && (source.Hash == null || Rules.Hash(downloaded) == source.Hash), "Downloaded asset identity mismatch");
        return downloaded;
    }
}
internal static class Program {
    [STAThread]
    static int Main(string[] args) {
        // Keep exact CLI commands headless. Double-clicked installs retain a
        // readable result window; exact maintenance commands remain available.
        if (args.Length == 1 && args[0] == "--uninstall") return SetupWindow.Run((options, progress) => Integration.Uninstall(), true);
        return args.Length == 0 ? SetupWindow.Run((options, progress) => Execute(args, progress, options)) : Execute(args).Code;
    }
    internal static SetupResult Execute(string[] args, Action<string> progress = null, SetupOptions options = null) {
        string phase = "CHECK_PACKAGE";
        string destination = null;
        try {
            Rules.Need(args.Length == 0 || (args.Length == 1 && (args[0] == "--inspect" || args[0] == "--no-launch" || args[0] == "--reload-helpers" || args[0] == "--upgrade")), "Use setup directly, exact --inspect, --no-launch, --reload-helpers or --upgrade");
            var package = EmbeddedPackage.Open();
            if (args.Length == 1 && args[0] == "--reload-helpers") {
                phase = "ACCEPT_MANUALLY_REPLACED_HELPERS"; Installer.Inspect(package);
                ManualReload.Accept(Installer.Destination()); Integration.Capture(Installer.Destination()); return new SetupResult(0, "助手源树已核对，下一次启动使用正式接口加载。");
            }
            if (args.Length == 1 && args[0] == "--inspect") { Installer.Inspect(package); Console.WriteLine("PASS fixed setup inventory; no network/install/browser/profile performed"); return new SetupResult(0, "安装包文件核对完成；未安装或启动。"); }
            destination = options == null ? Installer.Destination() : SetupOptions.ValidateDestination(options.Destination);
            var registered = Integration.Registered();
            Rules.Need(registered == null || registered.Equals(destination, StringComparison.OrdinalIgnoreCase), "已有其他位置的安装，请先卸载后再改变安装路径。");
            bool existing = File.Exists(Path.Combine(destination, ApplicationOwner.Name));
            if (args.Length == 1 && args[0] == "--upgrade") Rules.Need(existing, "Product installation required for explicit upgrade");
            phase = existing ? "EXPLICIT_APPLICATION_UPDATE" : "FIRST_INSTALL";
            if (progress != null) progress(existing ? "正在维护程序文件，保留助手和资料…" : "正在核对首次安装条件…");
            if (!existing) Rules.Need(options != null, "首次安装请直接打开安装器，先选择位置和游戏连接方式。");
            if (existing) ApplicationUpdate.Apply(package, destination); else destination = Installer.Install(package, options, progress);
            phase = "REGISTER_APPLICATION"; Integration.Register(destination);
            if (args.Length == 1) { Console.WriteLine("Installation maintenance complete; browser NOT_STARTED."); return new SetupResult(0, "程序文件已就绪；浏览器未启动。"); }
            if (options != null && !options.Launch) return new SetupResult(0, "安装完成，可按 Win+S 搜索 StarryFish。\r\n安装位置：" + destination + "\r\n在 Windows 已安装的应用中可卸载。");
            phase = "START_NATIVE_CANDIDATE";
            if (progress != null) progress("程序文件已就绪，正在启动 StarryFish…");
            // No credentials, profile migration, forced permissions or browser
            // flags. Normal launch prepares the independent game profile;
            // game requests still require this session's explicit proxy choice.
            var process = Process.Start(new ProcessStartInfo { FileName = Path.Combine(destination, "StarryFish.exe"), WorkingDirectory = destination, UseShellExecute = false });
            Rules.Need(process != null, "Native candidate did not start"); process.Dispose();
            return new SetupResult(0, "程序文件已安装在：\r\n" + destination +
                "\r\n\r\n已请求启动 StarryFish，也可按 Win+S 搜索打开。\r\n在 Windows 已安装的应用中可卸载。\r\n首次游戏连接按安装时的选择核对，登录由本人处理。");
        } catch (ManualCommitOutcome error) {
            Console.Error.WriteLine("MANUAL_RELOAD " + error.Message);
            return new SetupResult(3, "助手维护结果需要核对：" + error.Message + "\r\n保留现场，不自动重试或回滚。");
        } catch (ApplicationUpdateOutcome error) {
            Console.Error.WriteLine("APPLICATION_UPDATE " + error.Message);
            return new SetupResult(3, "程序维护结果需要核对：" + error.Message + "\r\n保留现场，不自动重试或回滚。");
        } catch (Exception error) {
            Console.Error.WriteLine("STOP " + phase + ": " + error.Message);
            var message = phase == "START_NATIVE_CANDIDATE" ?
                "程序文件已安装在：\r\n" + destination + "\r\n浏览器未能启动：" + error.Message + "\r\n请保留安装目录，之后直接启动 StarryFish.exe；不必重新下载助手。" :
                "本次操作停止（" + phase + "）：" + error.Message + "\r\n现有安装和资料保留；若有失败暂存也保留。请保留现场，不自动重试或回滚。";
            Console.Error.WriteLine(message); return new SetupResult(1, message);
        } finally {
            // Only the exact extraction created by this invocation, never the
            // user's downloaded installer/ZIP or a failed install staging tree.
            if (EmbeddedPackage.OwnedRoot!=null) try { CleanUninstall.RemovePackage(EmbeddedPackage.OwnedRoot); }
                catch (IOException) { Console.Error.WriteLine("Temporary verified installer payload could not be fully cleared; unknown files retained."); }
        }
    }
}
}
