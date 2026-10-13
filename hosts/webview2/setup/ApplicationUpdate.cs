// Explicit installation-package update. Keeps the installation path, helper
// bytes and external WebView profile stable. No automatic downloader/rollback.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace Gbrowser.Setup {
internal sealed class OwnedApplication {
    internal string Fingerprint;
    internal readonly List<Member> Files = new List<Member>();
}
internal static class ApplicationOwner {
    internal const string Name = "application-owner.bin", Pending = "application-update.pending";
    internal const int Limit = 512 * 1024;
    static byte[] Entropy(string root) { return Encoding.UTF8.GetBytes("GbrowserWebView2.ApplicationOwner.v1\n" + Paths.Local(root).ToLowerInvariant()); }
    internal static void CheckMembers(IList<Member> files) {
        Rules.Need(files.Count > 0 && files.Count <= 1024, "Application member budget");
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase); long total = 0;
        foreach (var item in files) {
            Rules.Member(item.Name);
            bool product = item.Name == "StarryFish.exe" || item.Name == "WebView2-SDK-LICENSE.txt" ||
                item.Name == "LICENSE.txt" || item.Name == "THIRD_PARTY_NOTICES.md" ||
                item.Name.StartsWith("Licenses/", StringComparison.Ordinal) ||
                item.Name.StartsWith("Assets/", StringComparison.Ordinal) || item.Name.StartsWith("Scripts/", StringComparison.Ordinal);
            // An old ownership envelope cannot retire locks, catalogues, helper
            // source/evidence or user data merely by listing them as app files.
            Rules.Need(product && seen.Add(item.Name) &&
                item.Bytes >= 0 && item.Bytes <= Rules.TotalLimit && Rules.Match(item.Hash, "[0-9a-f]{64}"), "Application ownership member rejected");
            total += item.Bytes; Rules.Need(total <= 256L * 1024 * 1024, "Application payload budget");
        }
        Rules.Need(seen.Contains("StarryFish.exe"), "Owned executable missing");
    }
    internal static byte[] Encode(string root, string fingerprint, IList<Member> files) {
        CheckMembers(files); Rules.Need(Rules.Match(fingerprint, "[0-9a-f]{64}"), "Application fingerprint rejected");
        using (var memory = new MemoryStream()) using (var writer = new BinaryWriter(memory, Encoding.UTF8, true)) {
            writer.Write(Encoding.ASCII.GetBytes("GBWA01\n")); writer.Write(Encoding.ASCII.GetBytes(fingerprint)); writer.Write(files.Count);
            foreach (var item in files.OrderBy(item => item.Name, StringComparer.Ordinal)) {
                writer.Write(item.Name); writer.Write(item.Bytes); writer.Write(Encoding.ASCII.GetBytes(item.Hash));
            }
            writer.Flush(); Rules.Need(memory.Length <= Limit - 1024, "Application owner size");
            return ProtectedData.Protect(memory.ToArray(), Entropy(root), DataProtectionScope.CurrentUser);
        }
    }
    internal static OwnedApplication Decode(string root, byte[] cipher) {
        Rules.Need(cipher.Length > 0 && cipher.Length <= Limit, "Application owner size");
        var plain = ProtectedData.Unprotect(cipher, Entropy(root), DataProtectionScope.CurrentUser);
        Rules.Need(plain.Length <= Limit, "Application owner plaintext budget");
        using (var memory = new MemoryStream(plain, false)) using (var reader = new BinaryReader(memory, Encoding.UTF8, true)) {
            Rules.Need(Encoding.ASCII.GetString(reader.ReadBytes(7)) == "GBWA01\n", "Application owner format");
            var result = new OwnedApplication { Fingerprint = Encoding.ASCII.GetString(reader.ReadBytes(64)) };
            Rules.Need(Rules.Match(result.Fingerprint, "[0-9a-f]{64}"), "Application owner fingerprint");
            int count = reader.ReadInt32(); Rules.Need(count > 0 && count <= 1024, "Application owner count");
            for (int i = 0; i < count; ++i) {
                var name = reader.ReadString(); int size = reader.ReadInt32(); var hash = Encoding.ASCII.GetString(reader.ReadBytes(64));
                result.Files.Add(new Member(name, size, hash));
            }
            Rules.Need(memory.Position == memory.Length, "Application owner trailing data");
            CheckMembers(result.Files); return result;
        }
    }
    internal static void NoPending(string root) {
        Paths.RequireMissing(Path.Combine(root, Pending));
    }
}
internal sealed class ApplicationUpdateOutcome : IOException {
    internal readonly string Outcome;
    internal ApplicationUpdateOutcome(bool verified, Exception error) : base(
        (verified ? "PAYLOAD_VERIFIED_GATE_CLEARANCE_UNKNOWN" : "APPLICATION_PUBLICATION_OUTCOME_UNKNOWN") +
        ": inspect the pending gate and retained previous payload. No browser launch, rollback or automatic retry.", error) {
        Outcome = verified ? "PAYLOAD_VERIFIED_GATE_CLEARANCE_UNKNOWN" : "APPLICATION_PUBLICATION_OUTCOME_UNKNOWN";
    }
}
internal static class ApplicationUpdate {
    internal static Member[] CurrentFiles() { return SetupInputs.Files.Where(item => !item.Name.StartsWith("Components/", StringComparison.Ordinal)).ToArray(); }
    static string MemberPath(string root, string name) { return Paths.Local(Path.Combine(root, name.Replace('/', '\\'))); }
    internal static void Apply(string package, string application) {
        package = Paths.Local(package); application = Paths.Local(application);
        Installer.Inspect(package); Prerequisites.Check();
        ApplyChecked(package, application);
    }
    // Shared by production and isolated fixtures after package/prerequisite
    // validation. Never invokes a network, browser or user-profile API.
    internal static void ApplyChecked(string package, string application) {
        using (var root = new Paths()) using (var source = new Paths()) using (var helpers = new Paths()) {
            root.Ancestors(application); root.ExclusiveApplication(application); source.Ancestors(package);
            ApplicationOwner.NoPending(application);
            OwnedApplication previous; byte[] priorOwner, catalogue;
            var snapshots = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            using (var prior = new Paths()) {
                priorOwner = prior.Read(MemberPath(application, ApplicationOwner.Name), ApplicationOwner.Limit);
                previous = ApplicationOwner.Decode(application, priorOwner);
                foreach (var item in previous.Files) snapshots.Add(item.Name, prior.Read(MemberPath(application, item.Name), item.Bytes, item.Hash));
                // These handles remain pinned through publication: unlike old
                // executable files, helper/catalogue bytes are never replaced.
                catalogue = helpers.Read(MemberPath(application, "component-catalogue.bin"), 16384);
                var accepted = ManualReload.Decode(application, catalogue);
                string[] roles = { "zeroomega", "tarou", "arcarum-dungeon" };
                for (int i = 0; i < roles.Length; ++i) {
                    var helper = ManualReload.ReadTree(MemberPath(application, "Components/" + roles[i]), helpers);
                    Rules.Need(helper.Tree == accepted[i].Tree && helper.Files.Count == accepted[i].Files.Count && helper.Bytes == accepted[i].Bytes,
                        "Helper source differs from accepted tree; accept explicit manual helper reload before app update");
                    if (i != 0) Archive.CheckRuntime(helper, roles[i]);
                }
                // Release old immutable file handles before ReplaceFile. The
                // installation lock/ancestor pins still exclude a running host.
            }
            CheckManagedNames(application, previous.Files, root);
            var next = CurrentFiles(); ApplicationOwner.CheckMembers(next);
            var payload = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            foreach (var item in next) {
                var target = MemberPath(application, item.Name);
                if (!snapshots.ContainsKey(item.Name)) Rules.Need(!File.Exists(target) && !Directory.Exists(target), "New application member collides with an unowned file");
                payload.Add(item.Name, source.Read(MemberPath(package, item.Name), item.Bytes, item.Hash));
            }
            if (previous.Fingerprint == SetupInputs.HostFingerprint) {
                Rules.Need(previous.Files.Count == next.Length && next.All(item => snapshots.ContainsKey(item.Name) &&
                    Rules.Hash(snapshots[item.Name]) == item.Hash && snapshots[item.Name].Length == item.Bytes), "Same fingerprint changed payload");
                Console.WriteLine("Application already matches this package; no update, helpers/profile untouched."); return;
            }
            long budget = snapshots.Values.Sum(bytes => (long)bytes.Length) + payload.Values.Sum(bytes => (long)bytes.Length) + 32L * 1024 * 1024;
            Rules.Need(new DriveInfo(Path.GetPathRoot(application)).AvailableFreeSpace >= budget, "Insufficient space for retained old/new application payload");
            var evidence = MemberPath(application, "ApplicationUpdateEvidence/" + Guid.NewGuid().ToString("N"));
            root.CreateTree(evidence);
            foreach (var item in snapshots) WritePinned(root, MemberPath(evidence, "previous/" + item.Key), item.Value);
            WritePinned(root, MemberPath(evidence, "previous-owner.bin"), priorOwner);
            WritePinned(root, MemberPath(evidence, "component-catalogue.bin"), catalogue);
            foreach (var item in payload) WritePinned(root, MemberPath(evidence, "next/" + item.Key), item.Value);
            var nextOwner = ApplicationOwner.Encode(application, SetupInputs.HostFingerprint, next);
            WritePinned(root, MemberPath(evidence, "next-owner.bin"), nextOwner);
            // Pin destination parents before mutations, but do not hold read
            // handles for destination/staged files across atomic replacements.
            foreach (var item in next) root.CreateTree(Path.GetDirectoryName(MemberPath(application, item.Name)));
            foreach (var old in previous.Files.Where(item => !payload.ContainsKey(item.Name)))
                root.CreateTree(Path.GetDirectoryName(MemberPath(evidence, "retired/" + old.Name)));
            var marker = MemberPath(application, ApplicationOwner.Pending);
            bool verified = false;
            try {
                Paths.WriteNew(marker, Encoding.ASCII.GetBytes("EXPLICIT_APPLICATION_UPDATE\n" + Path.GetFileName(evidence) + "\n"));
                foreach (var item in next) {
                    var staged = MemberPath(evidence, "next/" + item.Name); var target = MemberPath(application, item.Name);
                    if (snapshots.ContainsKey(item.Name)) File.Replace(staged, target, null);
                    else File.Move(staged, target);
                }
                foreach (var old in previous.Files.Where(item => !payload.ContainsKey(item.Name))) {
                    var retired = MemberPath(evidence, "retired/" + old.Name);
                    File.Move(MemberPath(application, old.Name), retired);
                }
                File.Replace(MemberPath(evidence, "next-owner.bin"), MemberPath(application, ApplicationOwner.Name), null);
                using (var readback = new Paths()) {
                    foreach (var item in next) readback.Read(MemberPath(application, item.Name), item.Bytes, item.Hash);
                    readback.Read(MemberPath(application, ApplicationOwner.Name), ApplicationOwner.Limit, Rules.Hash(nextOwner));
                    readback.Read(marker, 1024, Rules.Hash(Encoding.ASCII.GetBytes("EXPLICIT_APPLICATION_UPDATE\n" + Path.GetFileName(evidence) + "\n")));
                }
                WritePinned(root, MemberPath(evidence, "committed.txt"), Encoding.ASCII.GetBytes("APPLICATION_PAYLOAD_VERIFIED\nHELPERS_AND_PROFILE_UNTOUCHED\n"));
                verified = true;
                File.Delete(marker); // Only the exact gate just read back under the installation lock.
                ApplicationOwner.NoPending(application);
            } catch (Exception error) { throw new ApplicationUpdateOutcome(verified, error); }
            Console.WriteLine("Application updated. Previous payload retained at " + evidence + "; helpers and profile untouched.");
        }
    }
    static void WritePinned(Paths guards, string path, byte[] bytes) {
        guards.CreateTree(Path.GetDirectoryName(path)); Paths.WriteNew(path, bytes);
        using (var check = new Paths()) check.Read(path, bytes.Length, Rules.Hash(bytes));
    }
    static void CheckManagedNames(string application, IList<Member> files, Paths guards) {
        var known = new HashSet<string>(files.Select(item => item.Name), StringComparer.Ordinal);
        foreach (var path in Directory.EnumerateFiles(application)) {
            var extension = Path.GetExtension(path);
            if (extension.Equals(".exe", StringComparison.OrdinalIgnoreCase) || extension.Equals(".dll", StringComparison.OrdinalIgnoreCase))
                Rules.Need(known.Contains(Path.GetFileName(path)), "Unowned executable in application directory; retained for inspection");
        }
        foreach (var name in new[] { "Assets", "Scripts" }) {
            var folder = MemberPath(application, name); if (!Directory.Exists(folder)) continue;
            var queue = new Queue<string>(); queue.Enqueue(folder); int visited = 0;
            while (queue.Count > 0) {
                var directory = queue.Dequeue(); guards.DirectoryPin(directory);
                foreach (var path in Directory.EnumerateFileSystemEntries(directory)) {
                    Rules.Need(++visited <= 4096 && (File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0, "Managed application directory/link budget");
                    var member = path.Substring(application.Length + 1).Replace('\\', '/');
                    if (Directory.Exists(path)) {
                        Rules.Need(known.Any(file => file.StartsWith(member + "/", StringComparison.Ordinal)), "Unowned managed application directory"); queue.Enqueue(path);
                    } else Rules.Need(known.Contains(member), "Unowned managed application member; retained for inspection");
                }
            }
        }
    }
}
}
