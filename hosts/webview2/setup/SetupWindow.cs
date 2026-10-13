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
            ClientSize = new Size(640, 440), MinimumSize = new Size(640, 440), StartPosition = FormStartPosition.CenterScreen,
            AutoScaleMode = AutoScaleMode.Dpi, Font = font, MaximizeBox = false }) {
            var panel = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(22), ColumnCount = 1, RowCount = 8 };
            foreach (var height in new[] { 34, 24, 36, 36, 64, 32 }) panel.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
            panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
            var title = new Label { Dock = DockStyle.Fill, Text = uninstall ? "卸载程序，个人资料由你决定是否保留" : "欢迎安装 StarryFish" };
            var label = new Label { Dock = DockStyle.Fill, Text = "安装位置" };
            var row = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2 }; row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); row.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 86));
            var location = new TextBox { Dock = DockStyle.Fill, Text = Installer.Destination(), ReadOnly = uninstall };
            var browse = new Button { Dock = DockStyle.Fill, Text = "浏览…", Enabled = !uninstall && Integration.Registered() == null };
            if (Integration.Registered() != null) location.ReadOnly = true;
            row.Controls.Add(location, 0, 0); row.Controls.Add(browse, 1, 0);
            var proxy = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList, Enabled = !uninstall };
            proxy.Items.AddRange(new object[] { "游戏连接：acgpower", "游戏连接：岛风go", "游戏连接：直连" }); proxy.SelectedIndex = 0;
            var hint = new Label { Dock = DockStyle.Fill, Text = SetupOptions.ProxyHint("acgpower") + "\r\n安装下载遵循 Windows 系统代理；这里选择游戏使用的连接方式。" };
            var ready = new CheckBox { Dock = DockStyle.Fill, Text = uninstall ? "同时删除此安装的账号资料、登录态和配置（不可恢复）" : "安装完成后启动 StarryFish", Checked = !uninstall };
            var detail = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, BorderStyle = BorderStyle.None, ScrollBars = ScrollBars.Vertical,
                Text = uninstall ? "默认保留个人资料。浏览器必须先正常关闭。" : "安装前请确认所选代理已启动并启用。首次安装会联网获取太郎和肉鸽。\r\n安装后可以按 Win+S 搜索 StarryFish，也可以从开始菜单打开或卸载。已有安装更新时保留原游戏代理选择。" };
            var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
            var start = new Button { Text = uninstall ? "卸载" : "开始安装", Width = 120, Height = 32 };
            var cancel = new Button { Text = "取消", Width = 90, Height = 32 };
            buttons.Controls.Add(start); buttons.Controls.Add(cancel);
            panel.Controls.Add(title,0,0); panel.Controls.Add(label,0,1); panel.Controls.Add(row,0,2); panel.Controls.Add(proxy,0,3);
            panel.Controls.Add(hint,0,4); panel.Controls.Add(ready,0,5); panel.Controls.Add(detail,0,6); panel.Controls.Add(buttons,0,7); form.Controls.Add(panel);
            if (uninstall) { proxy.Visible = false; hint.Visible = false; }
            proxy.SelectedIndexChanged += (sender,args) => { var value = proxy.SelectedIndex == 2 ? "direct" : proxy.SelectedIndex == 1 ? "island" : "acgpower"; hint.Text = SetupOptions.ProxyHint(value) + "\r\n安装下载遵循 Windows 系统代理。"; };
            browse.Click += (sender,args) => { using (var picker = new FolderBrowserDialog { Description = "选择安装目录的父文件夹，将创建 StarryFish 子文件夹", ShowNewFolderButton = true }) {
                if (picker.ShowDialog(form) == DialogResult.OK) location.Text = Path.Combine(picker.SelectedPath, "StarryFish");
            }};
            cancel.Click += (sender,args) => form.Close();
            form.FormClosing += (sender,args) => { if (busy) { args.Cancel = true; title.Text = "正在处理文件，请等待完成"; } };
            start.Click += (sender,args) => {
                if (finished) { form.Close(); return; }
                var options = new SetupOptions { Proxy = proxy.SelectedIndex == 2 ? "direct" : proxy.SelectedIndex == 1 ? "island" : "acgpower", Launch = ready.Checked };
                try { options.Destination = uninstall ? Paths.Local(location.Text) : SetupOptions.ValidateDestination(location.Text); SetupOptions.ValidateProxy(options.Proxy); }
                catch (Exception error) { MessageBox.Show(form,error.Message,"请检查安装位置",MessageBoxButtons.OK,MessageBoxIcon.Information); return; }
                if (uninstall && ready.Checked && MessageBox.Show(form,"确定永久删除此安装的登录态、资料和配置？", "确认删除个人数据", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
                busy = true; start.Enabled = false; cancel.Enabled = false; browse.Enabled = false; location.ReadOnly = true; proxy.Enabled = false; ready.Enabled = false;
                title.Text = uninstall ? "正在卸载…" : "正在安装…";
                bool deleteData = ready.Checked;
                Action<string> report = message => form.BeginInvoke(new Action(() => title.Text = message));
                Task.Run(() => {
                    SetupResult outcome;
                    try { outcome = uninstall ? Integration.Uninstall(deleteData) : operation(options,report); }
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
