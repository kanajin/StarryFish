using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;

namespace Gbrowser.Setup {
internal static class EmbeddedPackage {
    internal static string OwnedRoot;
    internal static Member CopiedSetup;
    internal static string Open() {
        var assembly = Assembly.GetExecutingAssembly();
        using (var resource = assembly.GetManifestResourceStream("StarryFish.Payload.zip")) {
            // The check executable has no embedded payload. Ordinary production
            // setup always has one; old directory packaging remains inspectable.
            if (resource == null) return Paths.Local(AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\'));
            var parent = Paths.Local(Path.Combine(Path.GetTempPath().TrimEnd('\\'), "StarryFishSetup"));
            var root = Paths.Local(Path.Combine(parent, Guid.NewGuid().ToString("N")));
            OwnedRoot = root;
            using (var guards = new Paths()) {
                guards.CreateTree(root);
                using (var zip = new ZipArchive(resource, ZipArchiveMode.Read)) {
                    Rules.Need(zip.Entries.Count == SetupInputs.Files.Length, "内置安装文件数量异常。");
                    foreach (var item in SetupInputs.Files) {
                        var matches = zip.Entries.Where(entry => entry.FullName == item.Name).ToArray();
                        Rules.Need(matches.Length == 1 && matches[0].Length == item.Bytes, "内置安装文件大小异常。");
                        byte[] bytes;
                        using (var input = matches[0].Open()) using (var output = new MemoryStream()) {
                            var buffer = new byte[65536]; int count;
                            while ((count = input.Read(buffer, 0, buffer.Length)) != 0) {
                                Rules.Need(output.Length + count <= item.Bytes, "内置安装文件超限。"); output.Write(buffer, 0, count);
                            }
                            bytes = output.ToArray();
                        }
                        Rules.Need(bytes.Length == item.Bytes && Rules.Hash(bytes) == item.Hash, "内置安装文件校验失败。");
                        var target = Paths.Local(Path.Combine(root, item.Name.Replace('/', '\\')));
                        guards.CreateTree(Path.GetDirectoryName(target)); Paths.WriteNew(target, bytes);
                    }
                }
                var setupBytes=guards.Read(assembly.Location,64*1024*1024);
                CopiedSetup=new Member(Path.Combine(root,"StarryFishSetup.exe"),setupBytes.Length,Rules.Hash(setupBytes));
                Paths.WriteNew(CopiedSetup.Name,setupBytes);
                Paths.WriteNew(Path.Combine(root, "setup-record.json"), Encoding.ASCII.GetBytes("{\"kind\":\"embedded-verified-payload\"}\n"));
            }
            return root;
        }
    }
}
}
