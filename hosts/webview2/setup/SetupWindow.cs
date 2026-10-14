using System;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Gbrowser.Setup {
internal sealed class SetupResult {
    internal readonly int Code;
    internal readonly string Message;
    internal SetupResult(int code, string message) { Code = code; Message = message; }
}
internal static class SetupWindow {
    internal static int Run(Func<SetupOptions, Action<string>, SetupResult> operation, bool uninstall = false) {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        int resultCode = 1; bool busy = false, finished = false;
        using (var font = new Font("Microsoft YaHei UI", 9F))
        using (var form = new Form { Text = uninstall ? "卸载 StarryFish" : "StarryFish 安装", Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath),
            ClientSize = new Size(640, uninstall ? 440 : 610), MinimumSize = new Size(640, uninstall ? 440 : 610), StartPosition = FormStartPosition.CenterScreen,
            AutoScaleMode = AutoScaleMode.Dpi, Font = font, MaximizeBox = false }) {
            var panel = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(22), ColumnCount = 1, RowCount = 12 };
            foreach (var height in new[] { 34, 24, 36, 36, 64, 32, 36, 36, 36, 32 }) panel.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
            panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
            var title = new Label { Dock = DockStyle.Fill, Text = uninstall ? "完整卸载 StarryFish" : "欢迎安装 StarryFish" };
            var label = new Label { Dock = DockStyle.Fill, Text = "安装位置" };
            var row = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2 }; row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); row.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
            var location = new TextBox { Dock = DockStyle.Fill, Text = Installer.Destination(), ReadOnly = uninstall };
            var browse = new Button { Dock = DockStyle.Fill, Text = "浏览…", Enabled = !uninstall && Integration.Registered() == null };
            if (Integration.Registered() != null) location.ReadOnly = true;
            row.Controls.Add(location, 0, 0); row.Controls.Add(browse, 1, 0);
            var proxy = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList, Enabled = !uninstall };
            proxy.Items.AddRange(new object[] { "游戏连接：acgpower", "游戏连接：岛风go", "游戏连接：直连" }); proxy.SelectedIndex = 0;
            var hint = new Label { Dock = DockStyle.Fill, Text = SetupOptions.ProxyHint("acgpower") + "\r\n安装下载遵循 Windows 系统代理；这里选择游戏使用的连接方式。" };
            bool existing = Integration.Registered() != null;
            var sourceLabel = new Label { Dock = DockStyle.Fill, Text = "首次安装的插件来源" };
            var sources = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList, Enabled = !uninstall && !existing };
            sources.Items.AddRange(new object[] { "联网获取太郎和肉鸽（遵循系统代理）", "选择本地太郎和肉鸽 ZIP（离线）" }); sources.SelectedIndex = 0;
            var tarou = new TextBox { Dock = DockStyle.Fill, Enabled = false };
            var arcarum = new TextBox { Dock = DockStyle.Fill, Enabled = false };
            Func<string, TextBox, TableLayoutPanel> zipRow = (name, input) => {
                var container = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3 };
                container.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 48)); container.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); container.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
                var pick = new Button { Dock = DockStyle.Fill, Text = "选择ZIP…", Enabled = false };
                container.Controls.Add(new Label { Dock = DockStyle.Fill, Text = name }, 0, 0); container.Controls.Add(input, 1, 0); container.Controls.Add(pick, 2, 0);
                sources.SelectedIndexChanged += (sender,args) => { input.Enabled = pick.Enabled = sources.Enabled && sources.SelectedIndex == 1; };
                pick.Click += (sender,args) => { using (var dialog = new OpenFileDialog { Filter = "插件 ZIP 文件 (*.zip)|*.zip", CheckFileExists = true, Multiselect = false }) {
                    if (dialog.ShowDialog(form) == DialogResult.OK) input.Text = dialog.FileName;
                }};
                return container;
            };
            var tarouRow = zipRow("太郎", tarou); var arcarumRow = zipRow("肉鸽", arcarum);
            var ready = new CheckBox { Dock = DockStyle.Fill, Text = "安装完成后启动 StarryFish", Checked = !uninstall };
            var detail = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, BorderStyle = BorderStyle.None, ScrollBars = ScrollBars.Vertical,
                Text = uninstall ? "卸载将永久删除此安装的账号资料、登录态、配置、缓存和程序文件。安装目录中的无关文件及其他安装的资料保留。\r\n请先正常关闭浏览器。关闭卸载窗口后自动清理卸载器。" : "选择本地 ZIP 时不下载插件，两个包均须通过格式、角色和权限检查。原 ZIP 文件保留。\r\n更新已有安装保留原插件和代理选择；安装后可按 Win+S 搜索 StarryFish。" };
            var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
            var start = new Button { Text = uninstall ? "卸载" : "开始安装", Width = 120, Height = 32 };
            var cancel = new Button { Text = "取消", Width = 90, Height = 32 };
            buttons.Controls.Add(start); buttons.Controls.Add(cancel);
            panel.Controls.Add(title,0,0); panel.Controls.Add(label,0,1); panel.Controls.Add(row,0,2); panel.Controls.Add(proxy,0,3);
            panel.Controls.Add(hint,0,4); panel.Controls.Add(sourceLabel,0,5); panel.Controls.Add(sources,0,6); panel.Controls.Add(tarouRow,0,7); panel.Controls.Add(arcarumRow,0,8);
            panel.Controls.Add(ready,0,9); panel.Controls.Add(detail,0,10); panel.Controls.Add(buttons,0,11); form.Controls.Add(panel);
            if (uninstall) { foreach (var control in new Control[] { proxy, hint, sourceLabel, sources, tarouRow, arcarumRow, ready }) control.Visible = false; for (int i = 3; i <= 9; ++i) panel.RowStyles[i].Height = 0; }
            proxy.SelectedIndexChanged += (sender,args) => { var value = proxy.SelectedIndex == 2 ? "direct" : proxy.SelectedIndex == 1 ? "island" : "acgpower"; hint.Text = SetupOptions.ProxyHint(value) + "\r\n安装下载遵循 Windows 系统代理。"; };
            browse.Click += (sender,args) => { using (var picker = new FolderBrowserDialog { Description = "选择安装目录的父文件夹，将创建 StarryFish 子文件夹", ShowNewFolderButton = true }) {
                if (picker.ShowDialog(form) == DialogResult.OK) location.Text = Path.Combine(picker.SelectedPath, "StarryFish");
            }};
            cancel.Click += (sender,args) => form.Close();
            form.FormClosing += (sender,args) => { if (busy) { args.Cancel = true; title.Text = "正在处理文件，请等待完成"; } };
            start.Click += (sender,args) => {
                if (finished) { form.Close(); return; }
                var options = new SetupOptions { Proxy = proxy.SelectedIndex == 2 ? "direct" : proxy.SelectedIndex == 1 ? "island" : "acgpower", Launch = ready.Checked,
                    LocalHelpers = sources.SelectedIndex == 1, TarouZip = tarou.Text, ArcarumZip = arcarum.Text };
                try { options.Destination = uninstall ? Paths.Local(location.Text) : SetupOptions.ValidateDestination(location.Text); SetupOptions.ValidateProxy(options.Proxy); if (!uninstall) options.ValidateHelpers(); }
                catch (Exception error) { MessageBox.Show(form,error.Message,"请检查安装位置",MessageBoxButtons.OK,MessageBoxIcon.Information); return; }
                if (uninstall && MessageBox.Show(form,"确定完整卸载并永久删除此安装的登录态、资料、配置和缓存？无关文件会保留。", "确认完整卸载", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
                busy = true; start.Enabled = false; cancel.Enabled = false; browse.Enabled = false; location.ReadOnly = true; proxy.Enabled = false; ready.Enabled = false;
                title.Text = uninstall ? "正在卸载…" : "正在安装…";
                sources.Enabled = false; tarouRow.Enabled = arcarumRow.Enabled = false;
                Action<string> report = message => form.BeginInvoke(new Action(() => title.Text = message));
                Task.Run(() => {
                    SetupResult outcome;
                    try { outcome = uninstall ? Integration.Uninstall() : operation(options,report); }
                    catch (Exception error) { outcome = new SetupResult(1,error.Message + "\r\n请保留现场，不自动重试。"); }
                    form.BeginInvoke(new Action(() => { resultCode = outcome.Code; busy = false; finished = true;
                        title.Text = outcome.Code == 0 ? (uninstall ? "卸载完成" : "安装完成") : "操作未完成";
                        detail.Text = outcome.Message; start.Text = "关闭"; start.Enabled = true; }));
                });
            };
            Application.Run(form);
        }
        return resultCode;
    }
}
}
