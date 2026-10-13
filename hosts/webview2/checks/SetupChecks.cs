using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;

namespace Gbrowser.Setup {
internal static class SetupChecks {
    static int passed;
    static void Reject(Action action, string name) {
        bool failed = false; try { action(); } catch (InvalidDataException) { failed = true; } catch (IOException) { failed = true; } catch (EncoderFallbackException) { failed = true; } catch (System.Security.Cryptography.CryptographicException) { failed = true; }
        Rules.Need(failed, "Failed rejection: " + name); ++passed;
    }
    static byte[] Zip(string manifest, string extra = null) {
        using (var memory = new MemoryStream()) {
            using (var zip = new ZipArchive(memory, ZipArchiveMode.Create, true)) {
                foreach (var item in new Dictionary<string, string> {
                    {"fixture/manifest.json", manifest}, {"fixture/worker.js", "console.log('synthetic');"},
                    {"fixture/src/panel/index.html", "<!doctype html><title>synthetic</title>"}}) {
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
            File.WriteAllText(Path.Combine(root, "result.txt"), "PASS " + passed + " setup source/archive/filesystem boundary checks\nNO_NETWORK_INSTALL_BROWSER_PROFILE\n");
            Console.WriteLine("PASS " + passed + " setup source/archive/filesystem boundary checks; network/install/browser NOT_RUN"); return 0;
        } catch (Exception error) { Console.Error.WriteLine("FAIL setup source checks: " + error.Message); return 1; }
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
