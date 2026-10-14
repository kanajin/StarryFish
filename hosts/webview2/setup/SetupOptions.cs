using System;
using System.IO;
using System.Linq;

namespace Gbrowser.Setup {
internal sealed class SetupOptions {
    internal string Destination;
    internal string Proxy;
    internal bool Launch;
    internal bool LocalHelpers;
    internal string TarouZip, ArcarumZip;
    internal static string ValidateZip(string text) {
        Rules.Need(!string.IsNullOrWhiteSpace(text), "请选择太郎和肉鸽的 ZIP 文件。");
        var path = Paths.Local(Path.GetFullPath(text.Trim()));
        Rules.Need(Path.GetExtension(path).Equals(".zip", StringComparison.OrdinalIgnoreCase) && File.Exists(path), "请选择已有的本地 ZIP 文件。");
        return path;
    }
    internal void ValidateHelpers() {
        if (!LocalHelpers) return;
        TarouZip = ValidateZip(TarouZip); ArcarumZip = ValidateZip(ArcarumZip);
    }
    internal static string ValidateDestination(string text) {
        var path = Paths.Local(Path.GetFullPath(text.Trim()).TrimEnd('\\'));
        Rules.Need(path.Length <= 120 && Path.GetPathRoot(path).TrimEnd('\\') != path, "请选择专用的 StarryFish 安装文件夹，不要选择磁盘根目录；路径请短于120个字符。");
        Rules.Need(!File.Exists(path), "安装位置是文件，请选择文件夹。");
        if (Directory.Exists(path)) {
            // A new install may use an empty folder. Existing installations must
            // prove ownership, never treat an arbitrary folder as this product.
            if (Directory.EnumerateFileSystemEntries(path).Any()) {
                using (var guards = new Paths()) {
                    guards.Ancestors(path);
                    ApplicationOwner.Decode(path, guards.Read(Path.Combine(path, ApplicationOwner.Name), ApplicationOwner.Limit));
                }
            }
        }
        return path;
    }
    internal static void ValidateProxy(string value) {
        Rules.Need(value == "acgpower" || value == "island" || value == "direct", "请选择 acgpower、岛风go 或直连。");
    }
    internal static string ProxyHint(string value) {
        return value == "direct" ? "直连：游戏不使用代理，请确认当前网络可以访问 GBF。" :
            value == "island" ? "岛风go：请先启动并启用岛风go，保持本机 8099 端口服务运行。" :
            "acgpower：请先启动并启用 acgpower，保持本机 8123 端口服务运行。";
    }
}
}
