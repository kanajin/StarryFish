using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Gbrowser.Setup {
internal sealed class SetupResult {
    internal readonly int Code;
    internal readonly string Message;
    internal SetupResult(int code, string message) { Code = code; Message = message; }
}

// Only the no-argument install entry uses this window. The same backend owns
// CLI maintenance; the UI never cancels, retries or clears an unknown commit.
internal static class SetupWindow {
    internal static int Run(Func<Action<string>, SetupResult> operation) {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        int resultCode = 1; bool busy = true;
        using (var font = new Font("Microsoft YaHei UI", 9F))
        using (var form = new Form { Text = "StarryFish 安装", Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath), ClientSize = new Size(610, 300),
            MinimumSize = new Size(500, 290), StartPosition = FormStartPosition.CenterScreen,
            AutoScaleMode = AutoScaleMode.Dpi, Font = font, MaximizeBox = false }) {
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(20),
                ColumnCount = 1, RowCount = 4 };
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
            var title = new Label { Dock = DockStyle.Fill, Text = "正在准备 StarryFish…", AutoSize = false };
            var detail = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true,
                ScrollBars = ScrollBars.Vertical, BorderStyle = BorderStyle.None,
                Text = "首次安装会联网获取太郎和肉鸽。\r\n现有安装则只维护程序文件，保留助手和资料。" };
            var progress = new ProgressBar { Dock = DockStyle.Fill, Style = ProgressBarStyle.Marquee };
            var close = new Button { Text = "请等待完成", Enabled = false, Dock = DockStyle.Right, Width = 120 };
            layout.Controls.Add(title, 0, 0); layout.Controls.Add(detail, 0, 1);
            layout.Controls.Add(progress, 0, 2); layout.Controls.Add(close, 0, 3);
            form.Controls.Add(layout); form.AcceptButton = close;
            close.Click += (sender, args) => form.Close();
            form.FormClosing += (sender, args) => {
                if (!busy) return;
                // The worker must finish its real publication/readback before
                // exit. Closing a progress window is not transaction rollback.
                args.Cancel = true;
                title.Text = "安装正在处理文件，请等待结果后关闭";
            };
            Action<string> report = message => form.BeginInvoke(new Action(() => title.Text = message));
            form.Shown += (sender, args) => Task.Run(() => {
                SetupResult outcome;
                try { outcome = operation(report); }
                catch (Exception error) { outcome = new SetupResult(1, "安装过程发生异常：" + error.Message +
                    "\r\n现有安装与资料保留；请保留现场，不自动重试。"); }
                form.BeginInvoke(new Action(() => {
                    resultCode = outcome.Code; busy = false;
                    progress.Style = ProgressBarStyle.Continuous; progress.Value = 0;
                    title.Text = outcome.Code == 0 ? "程序文件已就绪" : "本次操作未完成";
                    detail.Text = outcome.Message; close.Text = "关闭"; close.Enabled = true;
                }));
            });
            Application.Run(form);
        }
        return resultCode;
    }
}
}
