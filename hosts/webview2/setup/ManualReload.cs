// Explicit acceptance of two locally overwritten helper trees. No downloader,
// automatic updater, repository execution, profile access or browser launch.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace Gbrowser.Setup {
internal sealed class ManualCommitOutcome : IOException {
    internal readonly string Outcome;
    internal ManualCommitOutcome(string outcome, Exception error) : base(
        outcome + ": catalogue publication was attempted; current files and evidence retained. No browser started, rollback or automatic retry performed.", error) { Outcome = outcome; }
}
internal static class ManualReload {
    internal static void CommitGate(string pending, string catalogue, string evidence, byte[] next) {
        bool verified = false;
        try {
            // Even a ReplaceFile error can leave a publication outcome needing
            // inspection. Never report it as definitely unsubmitted or replay it.
            File.Replace(pending, catalogue, null);
            using (var committed = new Paths()) committed.Read(catalogue, 16384, Rules.Hash(next));
            verified = true;
            Paths.WriteNew(Path.Combine(evidence, "committed.txt"), Encoding.ASCII.GetBytes(
                "ACCEPTED_SOURCE_FOR_NEXT_NATIVE_RELOAD\nNO_DOWNLOAD_PROFILE_ACCESS_BROWSER_START\nDISABLED_EXTENSIONS_MUST_NOT_BE_REINSTALLED\n"));
        } catch (Exception error) {
            throw new ManualCommitOutcome(verified ? "COMMITTED_EVIDENCE_INCOMPLETE" : "PUBLICATION_OUTCOME_UNKNOWN", error);
        }
    }
    internal static CheckedPackage ReadTree(string root, Paths guards) {
        root = Paths.Local(root); guards.Ancestors(root);
        var result = new CheckedPackage(); var queue = new Queue<string>(); queue.Enqueue(root);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase); int visited = 0;
        while (queue.Count > 0) {
            var directory = queue.Dequeue(); guards.DirectoryPin(directory);
            foreach (var path in Directory.EnumerateFileSystemEntries(directory)) {
                var name = path.Substring(root.Length + 1).Replace('\\', '/'); Rules.Member(name);
                Rules.Need(++visited <= 4096 && names.Add(name) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0,
                    "Manual helper traversal/link/collision budget");
                if (Directory.Exists(path)) queue.Enqueue(path);
                else {
                    var bytes = guards.Read(path, Rules.TotalLimit - result.Bytes);
                    Rules.Need(result.Files.Count < 1024, "Manual helper file budget");
                    result.Files.Add(name, bytes); result.Bytes += bytes.Length;
                }
            }
        }
        result.Tree = Archive.Tree(result.Files.ToDictionary(item => item.Key, item => Rules.Hash(item.Value), StringComparer.Ordinal));
        return result;
    }
    internal static CheckedPackage[] Decode(string application, byte[] cipher) {
        Rules.Need(cipher.Length > 0 && cipher.Length <= 16384, "Prior catalogue size");
        var entropy = Encoding.UTF8.GetBytes("GbrowserWebView2.SourceCatalogue.v1\n" + Paths.Local(application).ToLowerInvariant());
        var plain = ProtectedData.Unprotect(cipher, entropy, DataProtectionScope.CurrentUser);
        Rules.Need(plain.Length == 223 && Encoding.ASCII.GetString(plain, 0, 7) == "GBWV01\n", "Prior catalogue format");
        var entries = new CheckedPackage[3];
        for (int i = 0; i < entries.Length; ++i) {
            int offset = 7 + 72 * i; var tree = Encoding.ASCII.GetString(plain, offset, 64);
            uint count = BitConverter.ToUInt32(plain, offset + 64), bytes = BitConverter.ToUInt32(plain, offset + 68);
            Rules.Need(Rules.Match(tree, "[0-9a-f]{64}") && count > 0 && count <= 1024 && bytes <= Rules.TotalLimit, "Prior catalogue role budget");
            entries[i] = new CheckedPackage { Tree = tree, Bytes = (int)bytes };
            for (uint n = 0; n < count; ++n) entries[i].Files.Add(n.ToString(), new byte[0]);
        }
        Rules.Need(entries[0].Tree == SetupInputs.ZeroTree && entries[0].Files.Count == SetupInputs.Files.Count(item => item.Name.StartsWith("Components/zeroomega/", StringComparison.Ordinal)) &&
            entries[0].Bytes == SetupInputs.Files.Where(item => item.Name.StartsWith("Components/zeroomega/", StringComparison.Ordinal)).Sum(item => item.Bytes), "Fixed ZeroOmega catalogue changed");
        return entries;
    }
    internal static void Accept(string application) {
        application = Paths.Local(application);
        Rules.Need(Directory.Exists(application), "Candidate installation required; no directory created");
        using (var guards = new Paths()) {
            guards.Ancestors(application); guards.ExclusiveApplication(application);
            ApplicationOwner.NoPending(application);
            // A maintenance binary can only accept trees for its exact compiled
            // native payload. An older installation requires an explicit product
            // upgrade first; JSON install evidence cannot bypass this comparison.
            foreach (var item in SetupInputs.Files.Where(item => !item.Name.StartsWith("Components/", StringComparison.Ordinal)))
                guards.Read(Path.Combine(application, item.Name.Replace('/', '\\')), item.Bytes, item.Hash);
            var cataloguePath = Path.Combine(application, "component-catalogue.bin"); byte[] previous;
            using (var prior = new Paths()) { previous = prior.Read(cataloguePath, 16384); }
            var old = Decode(application, previous);
            var zero = ReadTree(Path.Combine(application, "Components", "zeroomega"), guards);
            Rules.Need(zero.Tree == old[0].Tree && zero.Files.Count == old[0].Files.Count && zero.Bytes == old[0].Bytes, "Fixed ZeroOmega source changed");
            var components = new List<CheckedPackage> { zero };
            foreach (var role in new[] { "tarou", "arcarum-dungeon" }) {
                var value = ReadTree(Path.Combine(application, "Components", role), guards);
                Archive.CheckRuntime(value, role); components.Add(value);
            }
            bool changed = Enumerable.Range(1, 2).Any(i => components[i].Tree != old[i].Tree || components[i].Files.Count != old[i].Files.Count || components[i].Bytes != old[i].Bytes);
            if (!changed) { Console.WriteLine("Helper source trees already accepted; no files/profile/browser changed."); return; }
            // The user has already overwritten source files. We retain the prior
            // acceptance envelope, not a claim that removed source bytes can be
            // restored. Only this one gate file is committed atomically.
            var evidence = Paths.Local(Path.Combine(application, "ManualReloadEvidence", Guid.NewGuid().ToString("N")));
            // Match first install/application update: pin existing ancestors
            // before creating descendants, then retain every created directory.
            guards.CreateTree(evidence);
            var next = Catalogue.Encode(application, components);
            Paths.WriteNew(Path.Combine(evidence, "previous-catalogue.bin"), previous);
            Paths.WriteNew(Path.Combine(evidence, "accepted-catalogue.bin"), next);
            var roles = Enumerable.Range(1, 2).Select(i => "{\"role\":" + Json.Quote(i == 1 ? "tarou" : "arcarum-dungeon") +
                ",\"version\":" + Json.Quote(components[i].Version) + ",\"previous_tree\":" + Json.Quote(old[i].Tree) +
                ",\"accepted_tree\":" + Json.Quote(components[i].Tree) + ",\"files\":" + components[i].Files.Count + ",\"bytes\":" + components[i].Bytes + "}");
            Paths.WriteNew(Path.Combine(evidence, "prepared.json"), Encoding.ASCII.GetBytes(
                "{\"kind\":\"EXPLICIT_LOCAL_MANUAL_RELOAD\",\"source_identity\":\"USER_OVERWRITE_NOT_AUTHOR_SIGNATURE\",\"host_fingerprint\":" + Json.Quote(SetupInputs.HostFingerprint) +
                ",\"browser\":\"NOT_STARTED\",\"loading\":\"PENDING\",\"roles\":[" + string.Join(",", roles) + "]}\n"));
            var pending = Path.Combine(evidence, "pending-catalogue.bin"); Paths.WriteNew(pending, next);
            // The same install-root process.lock is held by every current host
            // mode through runtime exit. All accepted source files stay pinned.
            // Interrupted/failed acceptance never starts a browser or retries.
            CommitGate(pending, cataloguePath, evidence, next);
            Console.WriteLine("Manually replaced helper files accepted. Loading pending at next native startup; disabled extensions remain disabled. No browser/profile opened.");
        }
    }
}
}
