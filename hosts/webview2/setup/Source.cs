// First-install transport and archive checks. No browser, profiles, updates,
// repository scripts or author-signature claim. Uses only Windows .NET APIs.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

// Console compilation must opt into current framework compatibility defaults;
// otherwise it can inherit legacy TLS behaviour despite a modern installed CLR.
[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8", FrameworkDisplayName = ".NET Framework 4.8")]

namespace Gbrowser.Setup {
internal static class Rules {
    internal const int ArchiveLimit = 64 * 1024 * 1024, TotalLimit = 32 * 1024 * 1024;
    internal static void Need(bool value, string message) { if (!value) throw new InvalidDataException(message); }
    internal static string Hash(byte[] value) {
        using (var hash = SHA256.Create()) return Hex(hash.ComputeHash(value));
    }
    internal static string Hex(byte[] value) { return BitConverter.ToString(value).Replace("-", "").ToLowerInvariant(); }
    internal static bool Match(string text, string pattern) { return Regex.IsMatch(text, "\\A(?:" + pattern + ")\\z", RegexOptions.CultureInvariant); }
    internal static void Part(string part) {
        Need(part.Length > 0 && part.Length <= 255 && part != "." && part != ".." &&
            part.TrimEnd('.', ' ') == part && !part.Any(c => c < 32 || "<>:\"|?*\\/".Contains(c)), "Unsafe Windows member name");
        Need(!Match(part.Split('.')[0].TrimEnd(' ').ToUpperInvariant(), "CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|(?:COM|LPT)[1-9¹²³]"), "Device name rejected");
    }
    internal static void Member(string name) {
        Need(name.Length > 0 && name.Length <= 240 && name.All(c => c >= 32 && c < 127) && !name.Contains('\\'), "Unsupported ZIP name");
        var parts = name.Split('/'); Need(parts.Length <= 16, "ZIP path depth exceeded"); foreach (var part in parts) Part(part);
    }
    internal static Dictionary<string, object> Object(object value) {
        var result = value as Dictionary<string, object>; Need(result != null, "JSON object required"); return result;
    }
    internal static object Get(Dictionary<string, object> value, string key) { object result; return value.TryGetValue(key, out result) ? result : null; }
    internal static string Text(Dictionary<string, object> value, string key) {
        var result = Get(value, key) as string; Need(result != null, "JSON text required: " + key); return result;
    }
    internal static long Number(Dictionary<string, object> value, string key) {
        var result = Get(value, key); Need(result is long, "JSON integer required: " + key); return (long)result;
    }
}

// Duplicate keys, excessive depth/nodes and invalid Unicode must fail instead
// of a serializer silently choosing a different source/permission field.
internal sealed class Json {
    readonly string text; int position, nodes;
    Json(byte[] raw) { text = new UTF8Encoding(false, true).GetString(raw); }
    internal static object Read(byte[] raw, int limit) {
        Rules.Need(raw.Length > 0 && raw.Length <= limit, "JSON byte budget exceeded");
        var parser = new Json(raw); var value = parser.Value(0); parser.Space();
        Rules.Need(parser.position == parser.text.Length, "Trailing JSON content"); return value;
    }
    void Space() { while (position < text.Length && " \r\n\t".Contains(text[position])) ++position; }
    char Take() { Rules.Need(position < text.Length, "Truncated JSON"); return text[position++]; }
    bool Eat(char value) { Space(); if (position < text.Length && text[position] == value) { ++position; return true; } return false; }
    string String() {
        Rules.Need(Take() == '"', "JSON string required"); var output = new StringBuilder();
        for (;;) {
            var c = Take(); if (c == '"') break; Rules.Need(c >= 32, "JSON control character");
            if (c == '\\') {
                c = Take(); const string from = "\"\\/bfnrt", to = "\"\\/\b\f\n\r\t";
                var index = from.IndexOf(c); if (index >= 0) c = to[index];
                else {
                    Rules.Need(c == 'u' && position + 4 <= text.Length, "JSON escape");
                    var hex = text.Substring(position, 4); Rules.Need(Rules.Match(hex, "[0-9a-fA-F]{4}"), "JSON Unicode escape");
                    c = (char)Convert.ToUInt16(hex, 16); position += 4;
                }
            }
            output.Append(c);
        }
        var result = output.ToString(); new UTF8Encoding(false, true).GetBytes(result); return result;
    }
    object Value(int depth) {
        Space(); Rules.Need(depth <= 32 && ++nodes <= 65536, "JSON depth/node budget exceeded");
        if (Eat('{')) {
            var result = new Dictionary<string, object>(StringComparer.Ordinal);
            if (Eat('}')) return result;
            do { Space(); var key = String(); Rules.Need(Eat(':') && !result.ContainsKey(key), "JSON duplicate key/colon"); result.Add(key, Value(depth + 1)); } while (Eat(','));
            Rules.Need(Eat('}'), "JSON object end"); return result;
        }
        if (Eat('[')) {
            var result = new List<object>(); if (Eat(']')) return result;
            do { result.Add(Value(depth + 1)); } while (Eat(',')); Rules.Need(Eat(']'), "JSON array end"); return result;
        }
        if (position < text.Length && text[position] == '"') return String();
        foreach (var word in new[] { "true", "false", "null" }) {
            if (position + word.Length <= text.Length && text.Substring(position, word.Length) == word) {
                position += word.Length; return word == "null" ? null : (object)(word == "true");
            }
        }
        var start = position;
        while (position < text.Length && "-+0123456789.eE".Contains(text[position])) ++position;
        var number = text.Substring(start, position - start);
        Rules.Need(Rules.Match(number, "-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?"), "Invalid JSON number");
        long integer; if (long.TryParse(number, System.Globalization.NumberStyles.AllowLeadingSign,
            System.Globalization.CultureInfo.InvariantCulture, out integer)) return integer;
        double floating; Rules.Need(double.TryParse(number, System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out floating) && !double.IsInfinity(floating), "JSON number overflow"); return floating;
    }
    internal static string Quote(string text) {
        var result = new StringBuilder("\"");
        foreach (var c in text) {
            if (c == '"' || c == '\\') result.Append('\\').Append(c);
            else if (c < 32 || c >= 127) result.Append("\\u").Append(((int)c).ToString("x4")); else result.Append(c);
        }
        return result.Append('"').ToString();
    }
}

internal sealed class HttpStatus : IOException { internal readonly int Status; internal HttpStatus(int status) : base("Source HTTP " + status) { Status = status; } }
internal static class PublicSource {
    internal static string Repository(string role) {
        Rules.Need(role == "tarou" || role == "arcarum-dungeon", "Unknown helper role");
        return role == "tarou" ? "Waaatanuki/Chrome-Extension-Tarou" : "Aureth/arcarumDungeon";
    }
    static Uri Url(string text, string[] hosts) {
        Uri value; Rules.Need(text.Length <= 8192 && text.All(c => c >= 33 && c < 127) &&
            Uri.TryCreate(text, UriKind.Absolute, out value), "Invalid source URL");
        value = new Uri(text);
        Rules.Need(value.Scheme == "https" && hosts.Contains(value.Host, StringComparer.Ordinal) &&
            value.Port == 443 && value.UserInfo == "" && value.Fragment == "", "Source HTTPS host/redirect rejected"); return value;
    }
    internal static byte[] Fetch(string text, int limit) {
        // Follow Windows TLS policy; no old framework default protocol pin,
        // certificate callback, browser credentials or trust-store changes.
        Rules.Need(ServicePointManager.SecurityProtocol == SecurityProtocolType.SystemDefault,
            "System-default TLS policy required; no legacy fallback");
        var address = Url(text, new[] { "api.github.com", "github.com", "codeload.github.com" });
        var started = System.Diagnostics.Stopwatch.StartNew();
        for (int redirect = 0; redirect <= 4; ++redirect) {
            var query = (HttpWebRequest)WebRequest.Create(address); query.Method = "GET"; query.Proxy = null;
            query.AllowAutoRedirect = false; query.Timeout = 15000; query.ReadWriteTimeout = 15000;
            query.UserAgent = "Gbrowser-FirstInstall"; query.Headers[HttpRequestHeader.AcceptEncoding] = "identity";
            query.Accept = address.Host == "api.github.com" ? "application/vnd.github+json" : "application/octet-stream";
            HttpWebResponse response;
            try { response = (HttpWebResponse)query.GetResponse(); }
            catch (WebException error) {
                response = error.Response as HttpWebResponse;
                // Only the bounded status enum is public; exception messages
                // can contain redirect queries and must not enter install logs.
                if (response == null) throw new IOException("Source network/TLS failure (" + error.Status + "); verification retained", error);
            }
            using (response) {
                var code = (int)response.StatusCode;
                Rules.Need(started.Elapsed.TotalSeconds <= 90, "Source time budget exceeded");
                if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
                    Rules.Need(redirect < 4, "Source redirect budget exceeded");
                    var location = response.Headers[HttpResponseHeader.Location];
                    Rules.Need(location != null, "Missing source redirect");
                    var target = new Uri(address, location);
                    address = Url(target.AbsoluteUri, address.Host == "api.github.com" ? new[] { "api.github.com" } :
                        new[] { "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com", "codeload.github.com" }); continue;
                }
                if (code != 200) throw new HttpStatus(code);
                Rules.Need((response.ContentEncoding == "" || response.ContentEncoding == "identity") && response.ContentLength <= limit,
                    "Encoded/oversized response rejected");
                using (var input = response.GetResponseStream()) using (var output = new MemoryStream()) {
                    var chunk = new byte[65536]; int count;
                    while ((count = input.Read(chunk, 0, Math.Min(chunk.Length, limit - (int)output.Length + 1))) != 0) {
                        Rules.Need(started.Elapsed.TotalSeconds <= 90 && output.Length + count <= limit, "Actual download budget exceeded"); output.Write(chunk, 0, count);
                    }
                    Rules.Need(response.ContentLength < 0 || output.Length == response.ContentLength, "Truncated source response"); return output.ToArray();
                }
            }
        }
        throw new IOException("Source redirect failed");
    }
    static Dictionary<string, object> Metadata(string url, Func<string, int, byte[]> fetch) {
        return Rules.Object(Json.Read(fetch(url, 1024 * 1024), 1024 * 1024));
    }
    static Source Snapshot(string repository, Func<string, int, byte[]> fetch) {
        var api = "https://api.github.com/repos/" + repository;
        var meta = Metadata(api, fetch); var branch = Rules.Text(meta, "default_branch");
        Rules.Need(Rules.Text(meta, "full_name") == repository && branch.Length > 0 && branch.Length <= 256 && branch.All(c => c >= 33 && c < 127), "Development repository mismatch");
        meta = Metadata(api + "/commits/" + Uri.EscapeDataString(branch), fetch); var commit = Rules.Text(meta, "sha");
        Rules.Need(Rules.Match(commit, "[0-9a-f]{40}") && Rules.Text(meta, "url") == api + "/commits/" + commit, "Development commit mismatch");
        return new Source { Repository = repository, Tag = commit, Commit = commit, Kind = "DEVELOPMENT_CODE_SNAPSHOT",
            Url = "https://codeload.github.com/" + repository + "/zip/" + commit };
    }
    internal static Source Resolve(string role, Func<string, int, byte[]> fetch) {
        var repository = Repository(role); Dictionary<string, object> meta;
        try { meta = Metadata("https://api.github.com/repos/" + repository + "/releases/latest", fetch); }
        catch (HttpStatus error) { if (role == "arcarum-dungeon" && error.Status == 404) return Snapshot(repository, fetch); throw; }
        Rules.Need(Equals(Rules.Get(meta, "draft"), false) && Equals(Rules.Get(meta, "prerelease"), false) && Rules.Number(meta, "id") > 0, "Published Release required");
        var tag = Rules.Text(meta, "tag_name"); Rules.Need(Rules.Match(tag, "v?[0-9]+(?:\\.[0-9]+){0,3}") &&
            Rules.Text(meta, "html_url") == "https://github.com/" + repository + "/releases/tag/" + tag, "Release identity mismatch");
        var assets = Rules.Get(meta, "assets") as List<object>; Rules.Need(assets != null && assets.Count <= 64, "Release assets budget");
        var version = tag.TrimStart('v'); var pattern = role == "tarou" ? "Chrome-Extension-Tarou[._-]v?" + Regex.Escape(version) + "\\.zip" :
            "(?:arcarum-dungeon|arcarumDungeon)(?:[._-]v?" + Regex.Escape(version) + ")?\\.zip";
        var chosen = assets.Select(Rules.Object).Where(asset => Rules.Get(asset, "name") is string &&
            Regex.IsMatch(Rules.Text(asset, "name"), "\\A(?:" + pattern + ")\\z", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)).ToArray();
        if (chosen.Length == 0 && role == "arcarum-dungeon") { Rules.Need(assets.Count == 0, "Unknown assets; no snapshot fallback"); return Snapshot(repository, fetch); }
        Rules.Need(chosen.Length == 1, "One unambiguous extension ZIP required"); var item = chosen[0];
        var name = Rules.Text(item, "name"); Rules.Part(name); var size = Rules.Number(item, "size");
        var url = "https://github.com/" + repository + "/releases/download/" + tag + "/" + Uri.EscapeDataString(name);
        Rules.Need(Rules.Text(item, "browser_download_url") == url && Rules.Text(item, "state") == "uploaded" &&
            Rules.Number(item, "id") > 0 && size >= 22 && size <= Rules.ArchiveLimit, "Asset source/size mismatch");
        var digest = Rules.Get(item, "digest") as string;
        Rules.Need(Rules.Get(item, "digest") == null || (digest != null && Rules.Match(digest, "sha256:[0-9a-f]{64}")), "Asset digest unsupported");
        return new Source { Repository = repository, Tag = tag, Kind = "RELEASE_ASSET", Url = url, Size = (int)size,
            Hash = digest == null ? null : digest.Substring(7) };
    }
}
internal sealed class Source { internal string Repository, Tag, Commit, Kind, Url, Hash; internal int Size; }

internal sealed class CheckedPackage {
    internal readonly SortedDictionary<string, byte[]> Files = new SortedDictionary<string, byte[]>(StringComparer.Ordinal);
    internal string Version, Tree;
    internal int Bytes;
}
internal static class Archive {
    sealed class Entry { internal string Name; internal bool Directory; internal uint Crc; internal int Size, Packed, Method, Flags, Start, End; }
    static uint U32(byte[] data, int offset) { Rules.Need(offset >= 0 && offset <= data.Length - 4, "Truncated ZIP integer"); return BitConverter.ToUInt32(data, offset); }
    static int U16(byte[] data, int offset) { Rules.Need(offset >= 0 && offset <= data.Length - 2, "Truncated ZIP integer"); return BitConverter.ToUInt16(data, offset); }
    static int Bound(uint value, int maximum) { Rules.Need(value <= maximum, "ZIP integer budget"); return (int)value; }
    static void Extra(byte[] data, int offset, int length) {
        var end = offset + length; Rules.Need(end <= data.Length, "ZIP extra range"); var seen = new HashSet<int>();
        while (offset < end) {
            Rules.Need(offset + 4 <= end, "Truncated ZIP extra"); var kind = U16(data, offset); var size = U16(data, offset + 2); offset += 4;
            Rules.Need(seen.Add(kind) && offset + size <= end, "ZIP extra duplicate/range");
            if (kind == 0x5455) Rules.Need(size >= 1 && size <= 13 && (data[offset] & ~7) == 0 && (size - 1) % 4 == 0, "ZIP timestamp metadata");
            else if (kind == 0x7875) {
                Rules.Need(size >= 3 && data[offset] == 1 && data[offset + 1] >= 1 && data[offset + 1] <= 8 && 2 + data[offset + 1] < size, "ZIP UID metadata");
                var gid = data[offset + 2 + data[offset + 1]]; Rules.Need(gid >= 1 && gid <= 8 && size == 3 + data[offset + 1] + gid, "ZIP GID metadata");
            } else throw new InvalidDataException("Unsupported ZIP path/link/ZIP64 metadata");
            offset += size;
        }
    }
    static List<Entry> Directory(byte[] data) {
        Rules.Need(data.Length >= 22 && data.Length <= Rules.ArchiveLimit && U32(data, 0) == 0x04034b50, "Non-executable ZIP required");
        var end = -1;
        for (int i = data.Length - 22; i >= Math.Max(0, data.Length - 65557); --i) if (U32(data, i) == 0x06054b50) { end = i; break; }
        Rules.Need(end >= 0 && U16(data, end + 4) == 0 && U16(data, end + 6) == 0, "Split/missing ZIP rejected");
        var count = U16(data, end + 10); var offset = Bound(U32(data, end + 16), end); var size = Bound(U32(data, end + 12), end);
        Rules.Need(count > 0 && count <= 4096 && count == U16(data, end + 8) && offset + size == end && end + 22 + U16(data, end + 20) == data.Length, "ZIP directory/end budget");
        var result = new List<Entry>(); var paths = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var explicitNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase); var position = offset; long total = 0, compressed = 0;
        while (position < end) {
            Rules.Need(result.Count < count && position + 46 <= end && U32(data, position) == 0x02014b50, "ZIP central count/signature");
            var length = U16(data, position + 28); var extra = U16(data, position + 30); var note = U16(data, position + 32);
            Rules.Need(length > 0 && length <= 241 && position + 46 + length + extra + note <= end && U16(data, position + 34) == 0, "ZIP central name/disk budget");
            var rawName = data.Skip(position + 46).Take(length).ToArray(); Rules.Need(rawName.All(c => c >= 32 && c < 127), "ASCII ZIP paths required");
            var name = Encoding.ASCII.GetString(rawName); var directory = name.EndsWith("/", StringComparison.Ordinal); var canonical = directory ? name.Substring(0, name.Length - 1) : name;
            Rules.Member(canonical); Rules.Need(explicitNames.Add(canonical), "Duplicate ZIP member");
            var parts = canonical.Split('/'); for (int i = 1; i <= parts.Length; ++i) {
                var path = string.Join("/", parts.Take(i)); var identity = path + (i < parts.Length || directory ? "/" : ""); string previous;
                Rules.Need(!paths.TryGetValue(path, out previous) || previous == identity, "ZIP case/type collision"); paths[path] = identity;
            }
            var attr = U32(data, position + 38); var kind = (attr >> 16) & 0xf000;
            Rules.Need((kind == 0 || kind == (directory ? 0x4000U : 0x8000U)) && (attr & 0x400) == 0 && (directory || (attr & 0x10) == 0), "ZIP link/special/type rejected");
            var entry = new Entry { Name = name, Directory = directory, Flags = U16(data, position + 8), Method = U16(data, position + 10),
                Crc = U32(data, position + 16), Packed = Bound(U32(data, position + 20), Rules.ArchiveLimit),
                Size = Bound(U32(data, position + 24), Rules.TotalLimit), Start = Bound(U32(data, position + 42), offset) };
            // Only descriptor/UTF8 and Deflate's two compression-option bits.
            // Do not let a different parser reinterpret reserved ZIP flags.
            Rules.Need((entry.Flags & ~(8 | 0x800 | (entry.Method == 8 ? 6 : 0))) == 0 && (entry.Method == 0 || entry.Method == 8) &&
                entry.Size <= (long)Math.Max(1, entry.Packed) * 200 && (!directory || entry.Size == 0) &&
                (entry.Size != 0 || entry.Packed <= 64) && (entry.Method != 0 || entry.Size == entry.Packed), "ZIP compression/member budget");
            total += entry.Size; compressed += entry.Packed;
            Rules.Need(total <= Rules.TotalLimit && compressed <= data.Length, "ZIP total budget");
            Extra(data, position + 46 + length, extra);
            var start = entry.Start; Rules.Need(start + 30 <= offset && U32(data, start) == 0x04034b50 &&
                U16(data, start + 6) == entry.Flags && U16(data, start + 8) == entry.Method, "ZIP local header mismatch");
            var localName = U16(data, start + 26); var localExtra = U16(data, start + 28); var body = start + 30 + localName + localExtra;
            Rules.Need(localName == length && body <= offset && rawName.SequenceEqual(data.Skip(start + 30).Take(localName)) && entry.Packed <= offset - body, "ZIP local name/range mismatch");
            Extra(data, start + 30 + localName, localExtra); var dataEnd = body + entry.Packed;
            var crc = U32(data, start + 14); var packed = U32(data, start + 18); var unpacked = U32(data, start + 22);
            if ((entry.Flags & 8) != 0) {
                Rules.Need((crc == 0 || crc == entry.Crc) && (packed == 0 || packed == entry.Packed) && (unpacked == 0 || unpacked == entry.Size), "ZIP descriptor local mismatch");
                var descriptor = dataEnd + (dataEnd + 4 <= offset && U32(data, dataEnd) == 0x08074b50 ? 4 : 0);
                Rules.Need(descriptor + 12 <= offset && U32(data, descriptor) == entry.Crc && U32(data, descriptor + 4) == entry.Packed && U32(data, descriptor + 8) == entry.Size, "ZIP descriptor mismatch"); dataEnd = descriptor + 12;
            } else Rules.Need(crc == entry.Crc && packed == entry.Packed && unpacked == entry.Size, "ZIP local size/CRC mismatch");
            entry.End = dataEnd; result.Add(entry); position += 46 + length + extra + note;
        }
        Rules.Need(position == end && result.Count == count, "ZIP central count mismatch");
        var previousEnd = 0; foreach (var entry in result.OrderBy(value => value.Start)) {
            Rules.Need(entry.Start >= previousEnd, "Overlapping ZIP content"); if (previousEnd == 0) Rules.Need(entry.Start == 0, "Prepended ZIP data"); previousEnd = entry.End;
        }
        return result;
    }
    static readonly uint[] CrcTable = MakeCrcTable();
    static uint[] MakeCrcTable() {
        var table = new uint[256];
        for (uint index = 0; index < table.Length; ++index) { var value = index; for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) == 0 ? 0U : 0xedb88320U); table[index] = value; }
        return table;
    }
    static uint Crc(byte[] data) {
        uint value = 0xffffffff; foreach (var item in data) value = (value >> 8) ^ CrcTable[(value ^ item) & 0xff]; return value ^ 0xffffffff;
    }
    internal static CheckedPackage Inspect(byte[] raw, string role, Source source) {
        var entries = Directory(raw); var manifestEntries = entries.Where(entry => !entry.Directory && entry.Name.Split('/').Last().Equals("manifest.json", StringComparison.OrdinalIgnoreCase)).ToArray();
        Rules.Need(manifestEntries.Length == 1 && manifestEntries[0].Name.EndsWith("manifest.json", StringComparison.Ordinal) &&
            manifestEntries[0].Size > 0 && manifestEntries[0].Size <= 256 * 1024, "One bounded lowercase manifest required");
        var prefix = manifestEntries[0].Name.Substring(0, manifestEntries[0].Name.Length - 13);
        Rules.Need(entries.All(entry => entry.Directory ? prefix.StartsWith(entry.Name, StringComparison.Ordinal) || entry.Name.StartsWith(prefix, StringComparison.Ordinal) : entry.Name.StartsWith(prefix, StringComparison.Ordinal)), "Files outside extension root");
        Rules.Need(role != "tarou" || prefix == "Chrome-Extension-Tarou/", "Tarou extension layout changed");
        if (source != null && source.Kind == "DEVELOPMENT_CODE_SNAPSHOT") Rules.Need(prefix == "arcarumDungeon-" + source.Commit + "/", "Snapshot root/commit mismatch");
        var result = new CheckedPackage();
        using (var memory = new MemoryStream(raw, false)) using (var zip = new ZipArchive(memory, ZipArchiveMode.Read)) {
            Rules.Need(zip.Entries.Count == entries.Count, "ZIP extractor count mismatch");
            for (int i = 0; i < entries.Count; ++i) {
                var entry = entries[i]; var parsed = zip.Entries[i]; Rules.Need(parsed.FullName == entry.Name && parsed.Length == entry.Size && parsed.CompressedLength == entry.Packed, "ZIP extractor identity mismatch");
                using (var input = parsed.Open()) using (var output = new MemoryStream()) {
                    var buffer = new byte[65536]; int count;
                    while ((count = input.Read(buffer, 0, buffer.Length)) > 0) { Rules.Need(output.Length + count <= entry.Size, "Actual ZIP expansion exceeded"); output.Write(buffer, 0, count); }
                    var bytes = output.ToArray(); Rules.Need(bytes.Length == entry.Size && Crc(bytes) == entry.Crc, "ZIP content length/CRC mismatch");
                    if (!entry.Directory) { result.Files.Add(entry.Name.Substring(prefix.Length), bytes); result.Bytes += bytes.Length; }
                }
            }
        }
        CheckRuntime(result, role);
        if (source != null && source.Kind == "RELEASE_ASSET") Rules.Need(result.Version == source.Tag.TrimStart('v'), "Release manifest version mismatch");
        return result;
    }
    // First install and explicit local reload share the same manifest/capability
    // policy. Reload reads only the two fixed application component directories.
    internal static void CheckRuntime(CheckedPackage result, string role) {
        Rules.Need(role == "tarou" || role == "arcarum-dungeon", "Unsupported helper role");
        Rules.Need(result.Files.Count > 0 && result.Files.Count <= 1024 && result.Bytes <= Rules.TotalLimit && result.Files.ContainsKey("manifest.json"), "Runtime component budget/manifest");
        var manifest = Rules.Object(Json.Read(result.Files["manifest.json"], 256 * 1024));
        result.Version = ValidateManifest(manifest, result.Files, role);
        result.Tree = Tree(result.Files.ToDictionary(item => item.Key, item => Rules.Hash(item.Value), StringComparer.Ordinal));
    }
    internal static string Tree(IDictionary<string, string> hashes) {
        var list = hashes.OrderBy(item => item.Key, StringComparer.Ordinal).Select(item => "[" + Json.Quote(item.Key) + "," + Json.Quote(item.Value) + "]");
        return Rules.Hash(Encoding.ASCII.GetBytes("[" + string.Join(",", list) + "]"));
    }
    static string ValidateManifest(Dictionary<string, object> manifest, IDictionary<string, byte[]> files, string role) {
        var known = new[] { "manifest_version", "name", "version", "description", "icons", "permissions", "optional_permissions", "host_permissions", "optional_host_permissions",
            "background", "content_scripts", "web_accessible_resources", "action", "minimum_chrome_version", "author", "short_name", "version_name", "homepage_url", "default_locale" };
        Rules.Need(manifest.Keys.All(key => known.Contains(key, StringComparer.Ordinal)), "New manifest capability requires scope review");
        Rules.Need(Rules.Number(manifest, "manifest_version") == 3 && Rules.Text(manifest, "name") == (role == "tarou" ? "Tarou" : "arcarum-dungeon"), "Extension role/MV3 mismatch");
        var version = Rules.Text(manifest, "version"); Rules.Need(Rules.Match(version, "(?:0|[1-9][0-9]{0,4})(?:\\.(?:0|[1-9][0-9]{0,4})){0,3}") &&
            version.Split('.').All(part => int.Parse(part) <= 65535) && version.Split('.').Any(part => int.Parse(part) > 0), "Extension version invalid");
        var permissions = role == "tarou" ? new[] { "storage", "unlimitedStorage", "tabs", "notifications", "debugger", "webRequest", "contextMenus", "scripting", "sidePanel" } : new[] { "storage", "tabs", "windows", "scripting" };
        var hosts = role == "tarou" ? new[] { "*://*.granbluefantasy.jp/*", "*://gbf.game.mbga.jp/*", "*://*.akamaized.net/*", "*://localhost/*" } : new[] { "*://*.granbluefantasy.jp/*", "*://gbf.game.mbga.jp/*", "*://*.akamaized.net/*" };
        foreach (var key in new[] { "permissions", "optional_permissions", "host_permissions", "optional_host_permissions" }) {
            var value = Rules.Get(manifest, key); var values = value == null ? new List<object>() : value as List<object>;
            Rules.Need(values != null && values.Count <= 256 && values.All(item => item is string) && values.Distinct().Count() == values.Count, "Manifest permissions invalid");
            var allowed = key.Contains("host_") ? hosts : permissions;
            Rules.Need(values.Cast<string>().All(item => allowed.Contains(item, StringComparer.Ordinal)), "New permission requires explicit scope review");
        }
        Action<object> asset = value => { var path = value as string; Rules.Need(path != null, "Local asset text required"); if (path.StartsWith("./", StringComparison.Ordinal)) path = path.Substring(2); Rules.Member(path); Rules.Need(!path.Contains('%') && !path.Contains('#') && files.ContainsKey(path), "Manifest local asset missing/encoded"); };
        Action<object> icons = value => { if (value is string) asset(value); else { var table = Rules.Object(value); Rules.Need(table.Count <= 128, "Icon budget"); foreach (var item in table.Values) asset(item); } };
        Action<object> assets = value => { var list = value as List<object>; Rules.Need(list != null && list.Count <= 4096, "Asset list budget"); foreach (var item in list) asset(item); };
        if (manifest.ContainsKey("icons")) icons(manifest["icons"]);
        if (manifest.ContainsKey("background")) { var background = Rules.Object(manifest["background"]); foreach (var key in new[] { "service_worker", "page" }) if (background.ContainsKey(key)) asset(background[key]); if (background.ContainsKey("scripts")) assets(background["scripts"]); }
        if (manifest.ContainsKey("content_scripts")) {
            var scripts = manifest["content_scripts"] as List<object>; Rules.Need(scripts != null && scripts.Count <= 256, "Content-script budget");
            var matches = role == "tarou" ? new[] { "*://*.granbluefantasy.jp/*", "*://gbf.game.mbga.jp/*", "*://localhost/*" } : new[] { "*://*.granbluefantasy.jp/*", "*://gbf.game.mbga.jp/*" };
            foreach (var item in scripts) {
                var script = Rules.Object(item);
                Rules.Need(script.Keys.All(key => new[] { "matches", "js", "css", "run_at", "world" }.Contains(key, StringComparer.Ordinal)), "New content-script capability requires review");
                var scope = Rules.Get(script, "matches") as List<object>;
                Rules.Need(scope != null && scope.Count > 0 && scope.Count <= matches.Length && scope.All(value => value is string && matches.Contains((string)value, StringComparer.Ordinal)), "Content-script scope expanded");
                Rules.Need(!script.ContainsKey("world") || Equals(script["world"], "ISOLATED"), "Content-script execution world changed");
                Rules.Need(!script.ContainsKey("run_at") || Equals(script["run_at"], "document_end"), "Content-script injection timing changed");
                foreach (var key in new[] { "js", "css" }) if (script.ContainsKey(key)) assets(script[key]);
            }
        }
        if (manifest.ContainsKey("web_accessible_resources")) {
            var groups = manifest["web_accessible_resources"] as List<object>; Rules.Need(groups != null && groups.Count <= 64, "Public resource budget");
            var scope = role == "tarou" ? new[] { "*://*.granbluefantasy.jp/*", "*://gbf.game.mbga.jp/*", "*://localhost/*" } : new[] { "*://*.granbluefantasy.jp/*", "*://gbf.game.mbga.jp/*" };
            var resource = role == "tarou" ? "inject.js" : "src/content/inject.js";
            foreach (var item in groups) {
                var group = Rules.Object(item);
                Rules.Need(group.Keys.All(key => key == "matches" || key == "resources") && group.Count == 2, "New externally accessible resource capability");
                var matches = Rules.Get(group, "matches") as List<object>; var resources = Rules.Get(group, "resources") as List<object>;
                Rules.Need(matches != null && matches.Count > 0 && matches.Count <= scope.Length && matches.All(value => value is string && scope.Contains((string)value, StringComparer.Ordinal)) &&
                    resources != null && resources.Count == 1 && Equals(resources[0], resource), "Public resource scope expanded"); assets(resources);
            }
        }
        // Only action is admitted by the role manifest contract above. The old
        // MV2/options/side_panel branches could never be reached and were removed.
        if (manifest.ContainsKey("action")) {
            var value = Rules.Object(manifest["action"]);
            if (value.ContainsKey("default_popup") && !Equals(value["default_popup"], "")) asset(value["default_popup"]);
            if (value.ContainsKey("default_icon")) icons(value["default_icon"]);
        }
        Rules.Need(files.ContainsKey(role == "tarou" ? "src/views/sidePanel/main.html" : "src/panel/index.html"), "Supported helper entry missing");
        return version;
    }
}
}
