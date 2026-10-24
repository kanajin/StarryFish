# WebView2 接入指南 · ABI 1

交付文件为 `gbf_hotkeys.dll`、`gbf_hotkeys.lib`、`include/gbf_hotkeys.h`、`vendor/character_shortcuts.h` 及示例。C# 不需要 `.lib`。库不依赖 StarryFish 的账号目录、配置目录或界面框架；不会注册全局键盘钩子、接管 Raw Input 或建立本地端口。

## 宿主提供的边界

1. 使用绝对路径加载可信 DLL 并验证 ABI 为 1。C++ 可随程序链接 import library；部署 DLL 到自身应用目录并遵守自身的 DLL 加载策略。
2. 在 WebView UI 线程创建实例，传入前台顶层 HWND 及**完全覆盖游戏视口的无边框子 HWND**。可使用宿主专门的 WebView 容器；不得把包含地址栏的整个主窗口当视口。v1 需要可取得该 HWND，纯 composition host 未适配，不能承诺直接支持所有 WinUI 3 布局。
3. 宿主负责焦点判断、真实按键边沿、改键 UI 和持久化。仅前台游戏视图按键进入 SDK；不转发组合键、长按重复和输入框操作。WebView2 加速键仅用于阻止已处理键的默认动作，避免与 Raw Input 重复分发。
4. 获得动作 mask 后调用 `start`。数字键可同时包含角色和药水两个互斥作用域的位；页面探测决定当前对应功能。规则、默认键、冲突检查、SFKEYS3 编解码见 `vendor/character_shortcuts.h`，配置文件位置由宿主决定。
5. 每个实例全部调用串行化，所有 ExecuteScript 在 WebView UI 线程调度。失焦、导航（含 hash）、关闭视图、修改配置、缩放或尺寸变化时调用 `cancel`。双窗为每个视图建立实例，但只允许当前焦点实例运行。

## 请求协议

`gbf_command` 接收 UTF-8 JSON，返回内部 UTF-8 字符串；必须在下一次调用／销毁前复制。输入上限 64 KiB。不得跨 ABI 传递 STL 对象。异常转换为 `error` 响应。

| 请求 | 响应与处理 |
|---|---|
| `{"op":"start","mask":1}` | `started` 开始新动作；`queued` 加入 FIFO；`queue-full` 表示拒绝新项。最多 32 项预输入。 |
| `{"op":"poll"}` | 新动作开始后调用一次；返回 `probe`，含 `ticket/mask/phase/locked`。 |
| `{"op":"step","ticket":N,"reply":{...}}` | 发送对应页面探测结果；返回下一次 `probe` 或终态。 |
| `{"op":"cancel"}` | 清空动作与队列，返回 `idle`。 |

收到 `probe` 后，间隔约 30ms，在对应 WebView 执行 `gbf_probe_script()` 返回的函数表达式，参数为 `(本动作开始时的URL,mask,phase,locked)`。使用 JSON 序列化传入字符串，禁止直接拼接未经转义的 URL。将 ExecuteScript 返回的对象 JSON 放入 `reply`，不是再次编码成 JSON 字符串。

每个 probe 只能回复一次。收到 `idle` 停止轮询；`reload` 停止并调用当前 WebView 的 Reload；`cancelled/error` 停止并显示原因，不自动重试。SDK 校验两次一致几何、焦点、鼠标干预、遮挡、视口尺寸和 1.5 秒／FC 3 秒截止时间；它不负责验证宿主伪造的页面回复，宿主必须仅传对应脚本结果。

每次异步脚本请求捕获宿主自己的生命周期代号。导航／取消／销毁递增代号，丢弃旧结果；**禁止在 destroy 后调用旧句柄**。没有在途回调会使用句柄后才能卸载 DLL。

## 示例

- `examples/dotnet/HotkeysSession.cs`：可编译的 P/Invoke 适配器，适用于有 UI SynchronizationContext 的 .NET 宿主。将 `CoreWebView2.ExecuteScriptAsync`、读取 `Source`、`Reload` 作为委托传入；按键转换成 mask 后 `await EnqueueAsync(mask)`。控件关闭前 Dispose；先接好所有导航／失焦取消事件。
- `examples/cpp/session.h`：C++ ABI 和探测脚本封装，已在 SDK 检查程序中编译。宿主按照上面的请求循环接入 `ExecuteScript` 回调，并使用弱引用或生命周期代号丢弃旧回调。

示例是适配组件，不是完整浏览器。工具栏入口、配置 UI、主题和账号隔离仍由宿主决定。这允许闭源作者接入，同时保留自己的界面和输入分发机制。

## 动作索引

0–3 角色；4 刷新；5 Back；6 Quick 召唤；7 召唤面板；8–11 技能；12–14 普通药水；15 回复道具；16 攻击／取消；17 FC；18 奥义；19 Menu；20–22 活动药水。mask 为各索引的 `1u << index` 按位或。

首次使用总开关关闭；配置 UI 必须执行重叠作用域冲突校验。StarryFish 已有 SFKEYS3 配置无需迁移；第三方宿主应存储在自己的配置目录，不读 StarryFish 用户资料。
