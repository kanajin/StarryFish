using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Text;

namespace Gbrowser.Setup {
internal static class SetupChecks {
    static int passed;
    static void Reject(Action action, string name) {
        bool failed = false; try { action(); } catch (InvalidDataException) { failed = true; } catch (IOException) { failed = true; } catch (EncoderFallbackException) { failed = true; } catch (System.Security.Cryptography.CryptographicException) { failed = true; }
        Rules.Need(failed, "Failed rejection: " + name); ++passed;
    }
    static byte[] Zip(string manifest, string extra = null, bool tarou = false) {
        using (var memory = new MemoryStream()) {
            using (var zip = new ZipArchive(memory, ZipArchiveMode.Create, true)) {
                foreach (var item in new Dictionary<string, string> {
                    {"fixture/manifest.json", manifest}, {"fixture/worker.js", "console.log('synthetic');"},
                    {tarou ? "fixture/src/views/sidePanel/main.html" : "fixture/src/panel/index.html", "<!doctype html><title>synthetic</title>"}}) {
                    using (var stream = zip.CreateEntry(item.Key).Open()) { var bytes = Encoding.UTF8.GetBytes(item.Value); stream.Write(bytes, 0, bytes.Length); }
                }
                if (extra != null) using (var stream = zip.CreateEntry(extra).Open()) stream.WriteByte(1);
            }
            return memory.ToArray();
        }
    }
    static readonly string Manifest = "{\"manifest_version\":3,\"name\":\"arcarum-dungeon\",\"version\":\"0.1.0\",\"background\":{\"service_worker\":\"worker.js\"},\"permissions\":[\"storage\"],\"host_permissions\":[\"*://*.granbluefantasy.jp/*\"]}";
    static int Main(string[] args) {
        try {
            if (args.Length == 4 && args[0] == "--archives") {
                using (var guards = new Paths()) {
                    var tarou = Archive.Inspect(guards.Read(Paths.Local(args[2]), Rules.ArchiveLimit,
                        "87cb6de12fac3cd18d75567198e8404f3d2b84aff724eea4968aefef20ba2b18"), "tarou", new Source { Kind = "RELEASE_ASSET", Tag = "3.7.1" });
                    var arcarum = Archive.Inspect(guards.Read(Paths.Local(args[3]), Rules.ArchiveLimit,
                        "fbb8298434b76ca30edb10bc58c57bfbf2c0760d89d89e837ac0deb4a9f44c27"), "arcarum-dungeon",
                        new Source { Kind = "DEVELOPMENT_CODE_SNAPSHOT", Commit = "c857641f7e77a730d4fda9cfcce947a701eae0d0" });
                    Rules.Need(tarou.Tree == "6e524626b55985d934f4f06510a488ca675dd1e0bec76d20674739d73957d248" && tarou.Files.Count == 331 &&
                        arcarum.Tree == "d1f848a1a6ef0fecd675cd808de6436e4ef4813cdaf2f9ad2afbb3d263689495" && arcarum.Files.Count == 181,
                        "Saved package/runtime tree parity changed");
                    Paths.WriteNew(Path.Combine(Paths.Local(args[1]), "saved-archives.txt"), Encoding.ASCII.GetBytes(
                        "TAROU_3.7.1_331_FILES_PARITY_PASS\nARCARUM_c857641_181_FILES_PARITY_PASS\nNO_DOWNLOAD_INSTALL_BROWSER\n"));
                    Console.WriteLine("PASS retained Tarou/Arcarum ZIP and runtime-tree parity; no download/install/browser"); return 0;
                }
            }
            Rules.Need(args.Length == 2 && Directory.Exists(args[0]) && !Directory.EnumerateFileSystemEntries(args[0]).Any(), "New empty setup-check directory and exact compiled fixture inventory required");
            var root = Paths.Local(args[0]); var raw = Zip(Manifest); var valid = Archive.Inspect(raw, "arcarum-dungeon", null);
            Prerequisites.CheckValues(528040, "154.0.4258.62", null); ++passed;
            Prerequisites.CheckValues(533510, "0.0.0.0", "154.0.4258.62"); ++passed;
            Reject(() => Prerequisites.CheckValues(461808, "154.0.4258.62", null), "legacy framework");
            Reject(() => Prerequisites.CheckValues(528040, "0.0.0.0", null), "runtime absent");
            Reject(() => Prerequisites.CheckValues(528040, "153.0.1.2", "preview"), "old/invalid runtime");
            Rules.Need(valid.Files.Count == 3 && valid.Version == "0.1.0" && valid.Files.ContainsKey("worker.js"), "Valid fixture identity"); ++passed;
            Reject(() => Json.Read(Encoding.UTF8.GetBytes("{\"source\":1,\"source\":2}"), 4096), "duplicate JSON");
            Reject(() => Json.Read(Encoding.UTF8.GetBytes("{\"x\":\"\\ud800\"}"), 4096), "unpaired surrogate");
            Reject(() => Archive.Inspect(Zip(Manifest, "../outside.txt"), "arcarum-dungeon", null), "path traversal");
            Reject(() => Archive.Inspect(Zip(Manifest, "fixture/WORKER.js"), "arcarum-dungeon", null), "case collision");
            Reject(() => Archive.Inspect(Zip(Manifest, "fixture/link:stream"), "arcarum-dungeon", null), "alternate stream");
            Reject(() => Archive.Inspect(Zip(Manifest, "fixture/CON.txt"), "arcarum-dungeon", null), "device path");
            Reject(() => Archive.Inspect(Zip(Manifest.Replace("\"storage\"", "\"management\"")), "arcarum-dungeon", null), "permission expansion");
            Reject(() => Archive.Inspect(Zip(Manifest.Replace("\"manifest_version\"", "\"externally_connectable\":{\"matches\":[\"<all_urls>\"]},\"manifest_version\"")), "arcarum-dungeon", null), "external connection expansion");
            Reject(() => Archive.Inspect(Zip(Manifest.Replace("\"manifest_version\"", "\"web_accessible_resources\":[{\"resources\":[\"worker.js\"],\"matches\":[\"<all_urls>\"]}],\"manifest_version\"")), "arcarum-dungeon", null), "public-resource expansion");
            Reject(() => Archive.Inspect(Zip(Manifest.Replace("worker.js", "missing.js")), "arcarum-dungeon", null), "missing manifest entry");
            Reject(() => Archive.Inspect(raw, "arcarum-dungeon", new Source { Kind = "RELEASE_ASSET", Tag = "v0.2.0" }), "release version mismatch");
            Reject(() => Archive.Inspect(raw, "arcarum-dungeon", new Source { Kind = "DEVELOPMENT_CODE_SNAPSHOT", Commit = new string('a', 40) }), "snapshot wrapper mismatch");
            var central = -1;
            for (int i = 0; i + 46 < raw.Length; ++i) if (BitConverter.ToUInt32(raw, i) == 0x02014b50) { central = i; break; }
            Rules.Need(central >= 0, "Fixture central header");
            var linked = (byte[])raw.Clone(); Array.Copy(BitConverter.GetBytes(0xa000U << 16), 0, linked, central + 38, 4);
            Reject(() => Archive.Inspect(linked, "arcarum-dungeon", null), "ZIP symlink");
            var crc = (byte[])raw.Clone(); crc[central + 16] ^= 1;
            Reject(() => Archive.Inspect(crc, "arcarum-dungeon", null), "ZIP CRC/local mismatch");
            var reserved = (byte[])raw.Clone(); reserved[central + 9] |= 0x40;
            Reject(() => Archive.Inspect(reserved, "arcarum-dungeon", null), "reserved ZIP flags");
            var size = (byte[])raw.Clone(); Array.Copy(BitConverter.GetBytes(0xffffffffU), 0, size, central + 24, 4);
            Reject(() => Archive.Inspect(size, "arcarum-dungeon", null), "ZIP64/size budget");
            Reject(() => PublicSource.Resolve("tarou", (url, limit) => Encoding.UTF8.GetBytes("{\"draft\":false,\"prerelease\":false,\"id\":1,\"tag_name\":\"v1.0\",\"html_url\":\"https://github.com/other/project/releases/tag/v1.0\",\"assets\":[]}")), "repository mismatch");
            int requests = 0; var commit = new string('a', 40);
            var snapshot = PublicSource.Resolve("arcarum-dungeon", (url, limit) => {
                ++requests;
                if (url.EndsWith("/releases/latest", StringComparison.Ordinal)) throw new HttpStatus(404);
                if (url.EndsWith("/arcarumDungeon", StringComparison.Ordinal)) return Encoding.UTF8.GetBytes("{\"full_name\":\"Aureth/arcarumDungeon\",\"default_branch\":\"dev\"}");
                Rules.Need(url.EndsWith("/commits/dev", StringComparison.Ordinal), "Expected commit resolution");
                return Encoding.UTF8.GetBytes("{\"sha\":\"" + commit + "\",\"url\":\"https://api.github.com/repos/Aureth/arcarumDungeon/commits/" + commit + "\"}");
            });
            Rules.Need(requests == 3 && snapshot.Commit == commit && snapshot.Kind == "DEVELOPMENT_CODE_SNAPSHOT" && snapshot.Url.EndsWith("/zip/" + commit, StringComparison.Ordinal), "Immutable commit resolution"); ++passed;
            string source = Path.Combine(root, "Held"), moved = Path.Combine(root, "Moved"); Directory.CreateDirectory(source);
            using (var guards = new Paths()) {
                guards.Ancestors(source); bool stopped = false;
                try { Directory.Move(source, moved); } catch (IOException) { stopped = true; }
                Rules.Need(stopped && Directory.Exists(source), "Installer directory rename guard"); ++passed;
                var file = Path.Combine(source, "bytes.bin"); Paths.WriteNew(file, raw); guards.Read(file, raw.Length, Rules.Hash(raw));
                Reject(() => { var linkedPath = Path.Combine(source, "bytes.bin"); Paths.WriteNew(linkedPath, raw); }, "no overwrite");
            }
            Directory.Move(source, moved); Directory.Move(moved, source); ++passed;
            // The managed installer has its own creation/pin implementation.
            // Both newly created ancestors and the leaf must stay protected.
            string created = Path.Combine(root, "Created"), leaf = Path.Combine(created, "Parent", "Leaf");
            using (var guards = new Paths()) {
                guards.CreateTree(leaf);
                Reject(() => Directory.Move(created, Path.Combine(root, "CreatedMoved")), "created ancestor stays pinned");
                Reject(() => Directory.Move(leaf, Path.Combine(root, "LeafMoved")), "created leaf stays pinned");
            }
            Directory.Move(created, Path.Combine(root, "CreatedReleased")); ++passed;
            var blockedParent = Path.Combine(root, "CreationBlockedByFile"); Paths.WriteNew(blockedParent, new byte[] { 1 });
            using (var guards = new Paths())
                Reject(() => guards.CreateTree(Path.Combine(blockedParent, "Child")), "file ancestor rejects directory creation");
            // Cross-language catalogue fixtures: no real component/profile is
            // touched; native checks subsequently decode these same envelopes.
            var zero = new CheckedPackage { Tree = SetupInputs.ZeroTree, Bytes = SetupInputs.Files.Where(item => item.Name.StartsWith("Components/zeroomega/", StringComparison.Ordinal)).Sum(item => item.Bytes) };
            foreach (var item in SetupInputs.Files.Where(item => item.Name.StartsWith("Components/zeroomega/", StringComparison.Ordinal))) zero.Files.Add(item.Name, new byte[0]);
            var roles = new List<CheckedPackage> {zero, valid, valid};
            foreach (var name in new[] { "valid", "corrupt", "wrong-root" }) Directory.CreateDirectory(Path.Combine(root, name));
            var envelope = Catalogue.Encode(Path.Combine(root, "valid"), roles);
            Paths.WriteNew(Path.Combine(root, "valid", "component-catalogue.bin"), envelope);
            var damaged = (byte[])envelope.Clone(); damaged[damaged.Length / 2] ^= 1;
            Paths.WriteNew(Path.Combine(root, "corrupt", "component-catalogue.bin"), damaged);
            Paths.WriteNew(Path.Combine(root, "wrong-root", "component-catalogue.bin"), envelope);
            ManualChecks(root, Paths.Local(args[1]), valid);
            UpdateChecks(root, Paths.Local(args[1]), valid);
            FirstPublicationChecks(root);
            SystemProxyChecks();
            Console.WriteLine("Checking owned/unrelated-file uninstall boundaries..."); IntegrationChecks(root, Paths.Local(args[1]), valid);
            Console.WriteLine("Checking offline acquisition and cleanup recovery..."); OfflineAndCleanupChecks(root,Paths.Local(args[1]),valid);
            File.WriteAllText(Path.Combine(root, "result.txt"), "PASS " + passed + " setup source/archive/filesystem boundary checks\nNO_NETWORK_INSTALL_BROWSER_PROFILE\n");
            Console.WriteLine("PASS " + passed + " setup source/archive/filesystem boundary checks; network/install/browser NOT_RUN"); return 0;
        } catch (Exception error) { Console.Error.WriteLine("FAIL setup source checks: " + error.Message); return 1; }
    }
    static void IntegrationChecks(string root, string payload, CheckedPackage component) {
        var application = Path.Combine(root, "UninstallFixture"); Fixture(application, payload, component);
        Paths.WriteNew(Path.Combine(application, ApplicationOwner.Name), ApplicationOwner.Encode(application, SetupInputs.HostFingerprint, ApplicationUpdate.CurrentFiles()));
        Rules.Need(SetupOptions.ValidateDestination(application) == application, "Existing owned destination"); ++passed;
        var empty = Path.Combine(root, "ChosenEmpty"); Directory.CreateDirectory(empty);
        Rules.Need(SetupOptions.ValidateDestination(empty) == empty, "Chosen empty destination"); ++passed;
        Installer.StageAndPublish(empty, staging => Paths.WriteNew(Path.Combine(staging, "fixture.txt"), new byte[] { 1 })); ++passed;
        Reject(() => SetupOptions.ValidateDestination(empty), "Do not overwrite unrelated directory");
        Reject(() => SetupOptions.ValidateDestination(Path.GetPathRoot(root)), "No drive root install");
        foreach (var choice in new[] { "acgpower", "island", "direct" }) { SetupOptions.ValidateProxy(choice); ++passed; }
        Reject(() => SetupOptions.ValidateProxy("unknown"), "Unknown install proxy");
        // Even unknown files inside component/evidence folders are not ours.
        var extra = Path.Combine(application, "Components", "tarou", "my-note.txt"); Paths.WriteNew(extra, new byte[] { 9 });
        var evidence = Path.Combine(application, "InstallEvidence", "my-note.txt"); Paths.WriteNew(evidence, new byte[] { 9 });
        Integration.Capture(application);
        var inventory = File.ReadAllBytes(Path.Combine(application, Integration.Inventory));
        var members = Integration.Decode(application, inventory);
        Rules.Need(!members.Any(file => file.Name.EndsWith("my-note.txt", StringComparison.Ordinal)), "Unknown additions claimed by uninstall"); ++passed;
        Reject(() => Integration.Decode(Path.Combine(root, "WrongInstall"), inventory), "Inventory bound to root");
        Reject(() => Integration.Encode(application, Enumerable.Repeat(members[0], 8193).ToArray()), "Inventory write count bound");
        var changed = Path.Combine(application, "StarryFish.exe"); File.WriteAllBytes(changed, new byte[] { 8 });
        using (var running = new Paths()) {
            running.ExclusiveApplication(application); Reject(() => Integration.RemoveOwned(application), "Uninstall excludes running browser");
        }
        var shortcut = Path.Combine(root, "SyntheticShortcut.lnk");
        System.Threading.Tasks.Task.Run(() => Integration.Shortcut(shortcut, changed, "--uninstall")).GetAwaiter().GetResult();
        Rules.Need(File.Exists(shortcut) && new FileInfo(shortcut).Length > 0, "Worker thread shortcut creation"); ++passed;
        using (var lockedInventory = new FileStream(Path.Combine(application, Integration.Inventory), FileMode.Open, FileAccess.Read, FileShare.Read)) {
            Rules.Need(Integration.FinishCleanup(application).Length > 0, "Final cleanup lock incorrectly treated as failure"); ++passed;
        }
        Integration.RemoveOwned(application);
        Rules.Need(File.Exists(extra) && File.Exists(evidence) && File.ReadAllBytes(changed).SequenceEqual(new byte[] { 8 }), "Uninstall removed unknown/modified file"); ++passed;
        Rules.Need(!File.Exists(Path.Combine(application, "WebView2-SDK-LICENSE.txt")), "Uninstall left unmodified owned file"); ++passed;
        var syntheticData = Path.Combine(root, "DeleteDataFixture"); Directory.CreateDirectory(Path.Combine(syntheticData, "Nested"));
        File.WriteAllText(Path.Combine(syntheticData, "Nested", "synthetic.txt"), "synthetic only");
        Integration.DeleteDataTree(syntheticData); Rules.Need(!Directory.Exists(syntheticData), "Explicit data deletion"); ++passed;
    }
    [System.Runtime.InteropServices.DllImport("kernel32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode,SetLastError=true)]
    static extern bool CreateHardLinkW(string link, string existing, IntPtr security);
    static void OfflineAndCleanupChecks(string root, string payload, CheckedPackage arcarum) {
        var local=Path.Combine(root,"OfflineZip"); Directory.CreateDirectory(local);
        var tarouPath=Path.Combine(local,"tarou.zip"); var arcarumPath=Path.Combine(local,"arcarum.zip");
        Paths.WriteNew(tarouPath,Zip(Manifest.Replace("arcarum-dungeon","Tarou"),null,true)); Paths.WriteNew(arcarumPath,Zip(Manifest));
        var options=new SetupOptions { LocalHelpers=true,TarouZip=tarouPath,ArcarumZip=arcarumPath }; options.ValidateHelpers(); ++passed;
        foreach (var role in new[]{"tarou","arcarum-dungeon"}) {
            Source source; var bytes=Installer.AcquireHelper(options,role,(url,limit)=>{throw new InvalidDataException("Offline mode used the network");},out source);
            var component=Archive.Inspect(bytes,role,source); Rules.Need(source.Kind=="LOCAL_USER_ARCHIVE" && source.Url=="" && component.Files.ContainsKey("manifest.json"),"Offline acquisition identity"); ++passed;
            Reject(()=>Archive.Inspect(bytes,role=="tarou"?"arcarum-dungeon":"tarou",source),"Offline ZIP role confusion");
        }
        Reject(()=>SetupOptions.ValidateZip(""),"Missing offline package");
        Reject(()=>SetupOptions.ValidateZip(Path.Combine(local,"missing.zip")),"Missing ZIP");
        Reject(()=>Archive.Inspect(Zip(Manifest.Replace("\"storage\"","\"management\"")),"arcarum-dungeon",new Source {Kind="LOCAL_USER_ARCHIVE"}),"Offline permissions unchanged");
        var application=Fixture(Path.Combine(root,"c"),payload,arcarum);
        Paths.WriteNew(Path.Combine(application,ApplicationOwner.Name),ApplicationOwner.Encode(application,new string('b',64),ApplicationUpdate.CurrentFiles()));
        Integration.Capture(application);
        ApplicationUpdate.ApplyChecked(payload,application);
        Integration.Capture(application);
        var historyRoot=Path.Combine(application,"ApplicationUpdateEvidence");
        var historyFolder=Directory.EnumerateDirectories(historyRoot).Single();
        var historyNote=Path.Combine(historyFolder,"my-own-note.txt"); Paths.WriteNew(historyNote,new byte[]{9});
        var unknown=Path.Combine(application,"my-own-document.txt"); Paths.WriteNew(unknown,new byte[]{9});
        var cache=Path.Combine(application,"Cache","Static-v1"); Directory.CreateDirectory(cache);
        var url=Encoding.ASCII.GetBytes("https://game.granbluefantasy.jp/assets/fixture.png");
        var headers="Content-Type: image/png\r\n";
        var body=Encoding.ASCII.GetBytes("0 100 "+url.Length+" "+headers.Length+" 4\n"+Encoding.ASCII.GetString(url)+headers+"body");
        var entry=Encoding.ASCII.GetBytes("STARRYFISH-CACHE-1\n"+Rules.Hash(body)+"\n").Concat(body).ToArray();
        var cachePath=Path.Combine(cache,Rules.Hash(url)+".sfc"); Paths.WriteNew(cachePath,entry);
        var unknownCache=Path.Combine(cache,new string('a',64)+".sfc"); Paths.WriteNew(unknownCache,new byte[]{9});
        using (var guard=new Paths()) Rules.Need(Integration.StaticFiles(application,guard).Count==1,"Cache ownership consumed an unrelated file"); ++passed;
        Reject(()=>Paths.DeleteRegular(unknown,new Member(unknown,1,Rules.Hash(new byte[]{8}))),"Deletion rechecks bytes");
        var linked=Path.Combine(cache,Rules.Hash(url)+".tmp");
        Rules.Need(CreateHardLinkW(linked,cachePath,IntPtr.Zero),"Create synthetic cache hardlink");
        Reject(()=>Integration.RemoveOwned(application),"Cache hardlinks stop before deletion");
        Rules.Need(File.Exists(Path.Combine(application,"StarryFish.exe")) && File.Exists(cachePath),"Preflight mutated before rejecting hardlink"); ++passed;
        File.Delete(linked);
        var originalInventory=File.ReadAllBytes(Path.Combine(application,Integration.Inventory));
        Integration.RemoveOwned(application);
        Rules.Need(!File.Exists(cachePath) && File.Exists(unknownCache) && File.Exists(unknown),"Owned/unrelated cache cleanup"); ++passed;
        var history=Path.Combine(application,"ApplicationUpdateEvidence");
        Rules.Need(File.Exists(historyNote) && Directory.EnumerateFiles(history,"*",SearchOption.AllDirectories).Count()==1,"History cleanup removed unrelated file or kept owned evidence"); ++passed;
        var hash=new string('a',64); var cacheMember=new Member(Path.Combine(CleanUninstall.CacheRoot(application),hash.Substring(0,16),"StarryFishSetup.exe"),1,hash);
        var envelope=CleanUninstall.Encode(application,new[]{cacheMember}); Rules.Need(CleanUninstall.Decode(application,envelope).Count==1,"Maintenance ownership parity"); ++passed;
        CleanUninstall.RecoverOwnership(application,originalInventory,envelope);
        Rules.Need(File.ReadAllBytes(Path.Combine(application,Integration.Inventory)).SequenceEqual(originalInventory) && File.ReadAllBytes(Path.Combine(application,CleanUninstall.Ledger)).SequenceEqual(envelope),"Partial uninstall lost authenticated retry ownership"); ++passed;
        Reject(()=>CleanUninstall.RecoverOwnership(Path.Combine(root,"NotThisInstall"),originalInventory,envelope),"Recovery cannot bind another root");
        Reject(()=>CleanUninstall.Decode(Path.Combine(root,"OtherInstallation"),envelope),"Maintenance belongs to exact install root");
        Reject(()=>CleanUninstall.Encode(application,new[]{new Member(unknown,1,hash)}),"No arbitrary external cleanup target");
        Reject(()=>CleanUninstall.EmptyParents(unknown,Path.Combine(root,"AnotherRoot")),"Empty-folder cleanup cannot escape boundary");
        var data=Path.Combine(root,"PrivateData"); Directory.CreateDirectory(data); Paths.WriteNew(Path.Combine(data,"synthetic-cookie"),new byte[]{1});
        var outsider=Path.Combine(root,"not-our-data"); Paths.WriteNew(outsider,new byte[]{2});
        var dataLink=Path.Combine(data,"linked"); Rules.Need(CreateHardLinkW(dataLink,outsider,IntPtr.Zero),"Create data hardlink");
        Reject(()=>Integration.DeleteDataTree(data),"Data preflight rejects hardlink");
        Rules.Need(File.Exists(Path.Combine(data,"synthetic-cookie")) && File.Exists(outsider),"Data preflight deleted partial files"); ++passed;
        File.Delete(dataLink); Integration.DeleteDataTree(data); Rules.Need(!Directory.Exists(data) && File.Exists(outsider),"Clean data scoped to private tree"); ++passed;
        var assembly=System.Reflection.Assembly.Load(File.ReadAllBytes(System.Reflection.Assembly.GetExecutingAssembly().Location));
        Rules.Need(assembly.GetType("Gbrowser.Setup.CleanUninstall",true).GetMethod("Finish",System.Reflection.BindingFlags.Static|System.Reflection.BindingFlags.NonPublic)!=null,"In-memory uninstall handoff method absent"); ++passed;
    }
    static string Fixture(string application, string payload, CheckedPackage arcarum) {
        application = Paths.Local(application); Directory.CreateDirectory(application);
        using (var guards = new Paths()) {
            guards.Ancestors(payload);
            foreach (var item in SetupInputs.Files) {
                var source = Path.Combine(payload, item.Name.Replace('/', '\\'));
                Paths.WriteNew(Path.Combine(application, item.Name.Replace('/', '\\')), guards.Read(source, item.Bytes, item.Hash));
            }
        }
        var tarou = new CheckedPackage();
        tarou.Files.Add("manifest.json", Encoding.UTF8.GetBytes("{\"manifest_version\":3,\"name\":\"Tarou\",\"version\":\"1.0.0\",\"permissions\":[\"storage\"]}"));
        tarou.Files.Add("src/views/sidePanel/main.html", Encoding.ASCII.GetBytes("<!doctype html><title>synthetic</title>"));
        tarou.Bytes = tarou.Files.Values.Sum(bytes => bytes.Length);
        Archive.CheckRuntime(tarou, "tarou");
        foreach (var role in new[] { "tarou", "arcarum-dungeon" }) {
            var component = role == "tarou" ? tarou : arcarum;
            foreach (var item in component.Files) Paths.WriteNew(Path.Combine(application, "Components", role, item.Key.Replace('/', '\\')), item.Value);
        }
        CheckedPackage zero;
        using (var guards = new Paths()) zero = ManualReload.ReadTree(Path.Combine(application, "Components", "zeroomega"), guards);
        var catalogue = Path.Combine(application, "component-catalogue.bin");
        var initial = Catalogue.Encode(application, new List<CheckedPackage> { zero, tarou, arcarum }); Paths.WriteNew(catalogue, initial);
        return application;
    }
    static void ManualChecks(string root, string payload, CheckedPackage arcarum) {
        var application = Fixture(Path.Combine(root, "m"), payload, arcarum);
        var catalogue = Path.Combine(application, "component-catalogue.bin"); var initial = File.ReadAllBytes(catalogue);
        CheckedPackage tarou;
        using (var guards = new Paths()) tarou = ManualReload.ReadTree(Path.Combine(application, "Components", "tarou"), guards);
        using (var running = new Paths()) {
            running.ExclusiveApplication(application);
            Reject(() => ManualReload.Accept(application), "maintenance excludes all host modes");
        }
        Rules.Need(File.ReadAllBytes(catalogue).SequenceEqual(initial), "Busy maintenance modified catalogue"); ++passed;
        // A helper acquires a new capability after manual overwrite: acceptance
        // must fail without changing the previously accepted gate.
        var manifest = Path.Combine(application, "Components", "arcarum-dungeon", "manifest.json");
        File.WriteAllBytes(manifest, Encoding.UTF8.GetBytes(Manifest.Replace("\"storage\"", "\"management\"")));
        Reject(() => ManualReload.Accept(application), "manual permission expansion");
        Rules.Need(File.ReadAllBytes(catalogue).SequenceEqual(initial), "Rejected helper changed catalogue"); ++passed;
        File.WriteAllBytes(manifest, arcarum.Files["manifest.json"]);
        var worker = Path.Combine(application, "Components", "arcarum-dungeon", "worker.js");
        File.WriteAllText(worker, "console.log('synthetic new bytes');", new UTF8Encoding(false));
        ManualReload.Accept(application);
        var accepted = ManualReload.Decode(application, File.ReadAllBytes(catalogue));
        CheckedPackage current;
        using (var guards = new Paths()) current = ManualReload.ReadTree(Path.Combine(application, "Components", "arcarum-dungeon"), guards);
        Rules.Need(accepted[2].Tree == current.Tree && accepted[1].Tree == tarou.Tree && Directory.GetDirectories(Path.Combine(application, "ManualReloadEvidence")).Length == 1,
            "Manual acceptance/tree/evidence mismatch"); ++passed;
        var committed = File.ReadAllBytes(catalogue); ManualReload.Accept(application);
        Rules.Need(committed.SequenceEqual(File.ReadAllBytes(catalogue)) && Directory.GetDirectories(Path.Combine(application, "ManualReloadEvidence")).Length == 1, "Unchanged manual reload rewrote gate"); ++passed;
        var executable = Path.Combine(application, "StarryFish.exe");
        File.WriteAllBytes(executable, new byte[] { 0 });
        Reject(() => ManualReload.Accept(application), "maintenance requires exact native build");
        Rules.Need(File.ReadAllBytes(catalogue).SequenceEqual(committed), "Wrong native build changed catalogue"); ++passed;
        var evidence = Path.Combine(root, "commit-evidence"); Directory.CreateDirectory(evidence);
        var gate = Path.Combine(evidence, "gate.bin"); var pending = Path.Combine(evidence, "pending.bin");
        Paths.WriteNew(gate, initial); Paths.WriteNew(pending, committed);
        Directory.CreateDirectory(Path.Combine(evidence, "committed.txt"));
        bool known = false;
        try { ManualReload.CommitGate(pending, gate, evidence, committed); }
        catch (ManualCommitOutcome error) { known = error.Outcome == "COMMITTED_EVIDENCE_INCOMPLETE"; }
        Rules.Need(known && File.ReadAllBytes(gate).SequenceEqual(committed), "Post-commit evidence failure obscured actual commit"); ++passed;
        Paths.WriteNew(pending, initial); bool unknown = false;
        using (var held = new FileStream(gate, FileMode.Open, FileAccess.Read, FileShare.None)) {
            try { ManualReload.CommitGate(pending, gate, evidence, initial); }
            catch (ManualCommitOutcome error) { unknown = error.Outcome == "PUBLICATION_OUTCOME_UNKNOWN"; }
        }
        Rules.Need(unknown && File.Exists(pending) && File.ReadAllBytes(gate).SequenceEqual(committed), "Failed publication was replayed or misreported"); ++passed;
    }
    static void SystemProxyChecks() {
        // Replace only this isolated check process's default proxy. No Windows
        // settings, credentials, PAC downloads or network requests are involved.
        var previous = WebRequest.DefaultWebProxy;
        try {
            var configured = new WebProxy("http://127.0.0.1:18473", false,
                new[] { @"^https://codeload\.github\.com(?::443)?(?:/|$)" });
            WebRequest.DefaultWebProxy = configured;
            foreach (var host in new[] { "api.github.com", "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com", "codeload.github.com" }) {
                var address = new Uri("https://" + host + "/fixture");
                var query = PublicSource.DownloadRequest(address);
                Rules.Need(query.Proxy != null && query.Proxy.GetProxy(address) == configured.GetProxy(address) &&
                    query.Proxy.IsBypassed(address) == (host == "codeload.github.com"), "Download ignored default proxy/bypass rule: " + host);
                Rules.Need(!query.AllowAutoRedirect && query.Credentials == null && query.Proxy.Credentials == null, "Proxy change enabled redirects or credentials");
                Rules.Need(query.Timeout==60000 && query.ReadWriteTimeout==60000,"Download timeout must be sixty seconds");
                query.Abort(); ++passed;
            }
            WebRequest.DefaultWebProxy = null;
            var direct = PublicSource.DownloadRequest(new Uri("https://github.com/fixture"));
            Rules.Need(direct.Proxy == null, "No-proxy environment no longer uses direct request");
            direct.Abort(); ++passed;
        } finally { WebRequest.DefaultWebProxy = previous; }
    }
    static void FirstPublicationChecks(string root) {
        var parent = Path.Combine(root, "first-publish");
        var destination = Path.Combine(parent, "Product");
        string staged = null;
        Installer.StageAndPublish(destination, staging => {
            staged = staging;
            Paths.WriteNew(Path.Combine(staging, "Components", "fixture", "worker.js"), new byte[] { 1, 2, 3 });
            // Reproduce the old lifetime bug: a pinned staging directory cannot
            // be renamed even when there is no other process using the product.
            Reject(() => Directory.Move(staging, destination), "staging pin blocks premature publication");
            Reject(() => Directory.Move(parent, parent + "-moved"), "parent stays pinned during preparation");
        });
        Rules.Need(!Directory.Exists(staged) && File.ReadAllBytes(Path.Combine(destination, "Components", "fixture", "worker.js")).SequenceEqual(new byte[] { 1, 2, 3 }), "First publication did not preserve prepared bytes"); ++passed;
        bool prepared = false;
        Reject(() => Installer.StageAndPublish(destination, staging => { prepared = true; }), "first install cannot replace existing product");
        Rules.Need(!prepared, "Existing product ran preparation"); ++passed;
        var failed = Path.Combine(parent, "Failed");
        Reject(() => Installer.StageAndPublish(failed, staging => {
            staged = staging; Paths.WriteNew(Path.Combine(staging, "evidence.txt"), new byte[] { 7 });
            throw new IOException("Synthetic preparation failure");
        }), "failed preparation must not publish");
        Rules.Need(!Directory.Exists(failed) && File.Exists(Path.Combine(staged, "evidence.txt")), "Failed staging was lost or published"); ++passed;
        var collision = Path.Combine(parent, "Collision");
        Reject(() => Installer.StageAndPublish(collision, staging => {
            staged = staging; Paths.WriteNew(Path.Combine(staging, "prepared.txt"), new byte[] { 8 });
            Paths.WriteNew(collision, new byte[] { 9 });
        }), "destination appearing during preparation is preserved");
        Rules.Need(File.ReadAllBytes(collision).SequenceEqual(new byte[] { 9 }) && File.Exists(Path.Combine(staged, "prepared.txt")), "Collision replaced destination or removed staging"); ++passed;
    }
    static void UpdateChecks(string root, string payload, CheckedPackage arcarum) {
        var application = Fixture(Path.Combine(root, "u"), payload, arcarum);
        var members = ApplicationUpdate.CurrentFiles(); var ownerPath = Path.Combine(application, ApplicationOwner.Name);
        var owner = ApplicationOwner.Encode(application, SetupInputs.HostFingerprint, members); Paths.WriteNew(ownerPath, owner);
        Rules.Need(ApplicationOwner.Decode(application, owner).Files.Count == members.Length, "Application ownership round trip"); ++passed;
        Reject(() => ApplicationOwner.Decode(Path.Combine(root, "wrong-owner"), owner), "ownership bound to installation root");
        var tampered = (byte[])owner.Clone(); tampered[tampered.Length / 2] ^= 1;
        Reject(() => ApplicationOwner.Decode(application, tampered), "ownership tamper");
        var forbidden = new[] { new Member("StarryFish.exe", 1, new string('a', 64)), new Member("component-catalogue.bin", 1, new string('a', 64)) };
        Reject(() => ApplicationOwner.Encode(application, SetupInputs.HostFingerprint, forbidden), "ownership cannot retire catalogue");
        forbidden[1] = new Member("Components/tarou/worker.js", 1, new string('a', 64));
        Reject(() => ApplicationOwner.Encode(application, SetupInputs.HostFingerprint, forbidden), "ownership cannot retire helper source");
        Reject(() => ApplicationOwner.Encode(application, SetupInputs.HostFingerprint, new[] {
            members.First(item => item.Name == "StarryFish.exe"), new Member("Scripts/a.js", 1, new string('a', 64)), new Member("Scripts/A.js", 1, new string('a', 64)) }), "ownership case collision");
        var catalogue = File.ReadAllBytes(Path.Combine(application, "component-catalogue.bin"));
        ApplicationUpdate.ApplyChecked(payload, application);
        Rules.Need(File.ReadAllBytes(ownerPath).SequenceEqual(owner) && !Directory.Exists(Path.Combine(application, "ApplicationUpdateEvidence")), "Matching application rewrote ownership"); ++passed;
        var gate = Path.Combine(application, ApplicationOwner.Pending); Paths.WriteNew(gate, new byte[] { 1 });
        Reject(() => ApplicationUpdate.ApplyChecked(payload, application), "pending app update refuses replay");
        Reject(() => ManualReload.Accept(application), "helper maintenance refuses pending app update");
        Rules.Need(File.ReadAllBytes(gate).SequenceEqual(new byte[] { 1 }) && File.ReadAllBytes(ownerPath).SequenceEqual(owner), "Pending update was cleared or rewritten"); ++passed;
        File.Delete(gate); // This check's synthetic gate only, never a product gate.
        Directory.CreateDirectory(gate); Reject(() => ApplicationOwner.NoPending(application), "directory update gate"); Directory.Delete(gate);
        using (var running = new Paths()) {
            running.ExclusiveApplication(application);
            Reject(() => ApplicationUpdate.ApplyChecked(payload, application), "app update excludes running host");
        }
        var oldFingerprint = SetupInputs.HostFingerprint == new string('a', 64) ? new string('b', 64) : new string('a', 64);
        var orphan = Path.Combine(application, "Scripts", "orphan.js"); Paths.WriteNew(orphan, new byte[] { 1 });
        Reject(() => ApplicationUpdate.ApplyChecked(payload, application), "unowned managed script rejected");
        Rules.Need(File.Exists(orphan), "Unowned script was removed"); ++passed; File.Delete(orphan);
        var orphanDll = Path.Combine(application, "orphan.dll"); Paths.WriteNew(orphanDll, new byte[] { 1 });
        Reject(() => ApplicationUpdate.ApplyChecked(payload, application), "unowned root DLL rejected"); File.Delete(orphanDll);
        // An otherwise valid old envelope omits a current, unowned destination:
        // publication must stop before replacing any application file.
        File.WriteAllBytes(ownerPath, ApplicationOwner.Encode(application, oldFingerprint, members.Where(item => item.Name != "WebView2-SDK-LICENSE.txt").ToArray()));
        Reject(() => ApplicationUpdate.ApplyChecked(payload, application), "unowned new-file collision");
        Rules.Need(!Directory.Exists(Path.Combine(application, "ApplicationUpdateEvidence")), "Collision created publication evidence"); ++passed;
        var executable = Path.Combine(application, "StarryFish.exe"); var oldExecutable = new byte[] { 1, 2, 3 };
        File.WriteAllBytes(executable, oldExecutable);
        var oldMembers = members.Select(item => item.Name == "StarryFish.exe" ? new Member(item.Name, oldExecutable.Length, Rules.Hash(oldExecutable)) : item).ToList();
        var retired = Encoding.ASCII.GetBytes("synthetic retired app code"); Paths.WriteNew(Path.Combine(application, "Scripts", "retired.js"), retired);
        oldMembers.Add(new Member("Scripts/retired.js", retired.Length, Rules.Hash(retired)));
        var oldOwner = ApplicationOwner.Encode(application, oldFingerprint, oldMembers); File.WriteAllBytes(ownerPath, oldOwner);
        var worker = Path.Combine(application, "Components", "arcarum-dungeon", "worker.js"); File.WriteAllBytes(worker, new byte[] { 7 });
        Reject(() => ApplicationUpdate.ApplyChecked(payload, application), "unaccepted helper blocks application update");
        File.WriteAllBytes(worker, arcarum.Files["worker.js"]);
        Paths.WriteNew(Path.Combine(application, "user-note.txt"), Encoding.ASCII.GetBytes("unowned file stays"));
        using (var held = new FileStream(executable, FileMode.Open, FileAccess.Read, FileShare.Read)) {
            bool unknown = false;
            try { ApplicationUpdate.ApplyChecked(payload, application); }
            catch (ApplicationUpdateOutcome error) { unknown = error.Outcome == "APPLICATION_PUBLICATION_OUTCOME_UNKNOWN"; }
            Rules.Need(unknown && File.Exists(gate) && File.ReadAllBytes(ownerPath).SequenceEqual(oldOwner), "Locked-file publication outcome/marker incorrect"); ++passed;
        }
        Reject(() => ApplicationOwner.NoPending(application), "failed publication gates native start");
        var failedEvidence = Directory.GetDirectories(Path.Combine(application, "ApplicationUpdateEvidence")).Single();
        Rules.Need(File.ReadAllBytes(Path.Combine(failedEvidence, "previous", "StarryFish.exe")).SequenceEqual(oldExecutable) &&
            File.ReadAllBytes(Path.Combine(failedEvidence, "previous-owner.bin")).SequenceEqual(oldOwner), "Failed publication lost previous payload"); ++passed;
        foreach (var item in oldMembers) Rules.Need(Rules.Hash(File.ReadAllBytes(Path.Combine(application, item.Name.Replace('/', '\\')))) == item.Hash, "Blocked fixture unexpectedly replaced app bytes"); ++passed;
        // Only our deliberately blocked fixture is resumed after proving no file
        // was replaced. Production never clears an unknown-outcome marker.
        File.Delete(gate);
        ApplicationUpdate.ApplyChecked(payload, application);
        var committed = ApplicationOwner.Decode(application, File.ReadAllBytes(ownerPath));
        Rules.Need(committed.Fingerprint == SetupInputs.HostFingerprint && !File.Exists(gate), "Updated payload owner/gate mismatch"); ++passed;
        foreach (var item in members) Rules.Need(Rules.Hash(File.ReadAllBytes(Path.Combine(application, item.Name.Replace('/', '\\')))) == item.Hash, "Updated application byte mismatch"); ++passed;
        Rules.Need(File.ReadAllBytes(Path.Combine(application, "component-catalogue.bin")).SequenceEqual(catalogue) &&
            File.ReadAllBytes(worker).SequenceEqual(arcarum.Files["worker.js"]) && File.Exists(Path.Combine(application, "user-note.txt")), "Update changed helpers/catalogue/unowned file"); ++passed;
        var completedEvidence = Directory.GetDirectories(Path.Combine(application, "ApplicationUpdateEvidence")).Single(path => File.Exists(Path.Combine(path, "committed.txt")));
        Rules.Need(!File.Exists(Path.Combine(application, "Scripts", "retired.js")) && File.ReadAllBytes(Path.Combine(completedEvidence, "retired", "Scripts", "retired.js")).SequenceEqual(retired), "Retired owned application code not preserved"); ++passed;
    }
}
}
