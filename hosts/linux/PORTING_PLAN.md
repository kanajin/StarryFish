# Linux 移植：实施与验收账本

目标：完整移植 Windows v0.1.12 浏览器功能，在 CachyOS x86_64 / KDE Plasma Wayland 上通过 XWayland 使用。保留 StarryFish 外观，以 CEF Views + Alloy style 和宿主适配层实施。旧 Ubuntu 24.04 计划只作为历史背景，不再是首要交付平台。

本次由 Windows 准备 linux 分支、构建入口及公开接续；后续在 CachyOS 本机继续。根基线为 3190a37（v0.1.12 加公开手册），现役行为依据 docs/UI_REQUIREMENTS.md、docs/INSTALLATION_REQUIREMENTS.md、docs/PROFILE_TRANSFER.md、docs/NETWORK_CAPTURE.md。平台差异记录在本文件，不复制一份 Windows 需求。

## 当前状态

| 交付项 | 代码 | 检查证据 | Linux / 用户实测 |
|---|---|---|---|
| doctor、显式 CEF lock/fetch、build/test/verify | 已实现，CEF 156 stable 已锁定 | 20 项 Python 检查、依赖与输出身份通过 | CachyOS 本机通过；受限执行器差异见下 |
| CEF Views Alloy / X11 启动骨架 | 修复 C/C++20、异常及 rpath；禁用自动扩展 | 首次 Linux Release 编译通过，保留 -Werror 和 sandbox | 隔离窗口、正常退出、GPU 和 namespace/seccomp 已观察；用户验收 pending |
| XDG 路径、私有目录、资料锁、原子写入 | 已接线／提供原语 | 原生 CTest 通过，含跨进程锁、硬链接和权限拒绝 | 第二实例拒绝、退出后进程清空已观察 |
| 固定本地诊断页、网页缩放、合成桌面检查器 | 已实现 | 构建身份绑定，保留合成证据 | 中文输入和页面 100%/125%/150% 用户确认；KDE 缩放切换未测 |
| 双布局、主题、基础标签及地址导航 | 部分实现；独立公共模型和 CEF Views 接线 | 20 项 Python、2 项 CTest；Linux Release 构建通过 | 合成页面导航、标签关闭确认、主题／布局及重启恢复已观察；用户外观验收 pending |
| 双窗与完整消息桥 | 未实现；仅已有文档代次门禁模型 | 跨资料／视图、旧文档、子 frame、伪造来源拒绝的模型检查 | pending |
| 原生代理与 ZeroOmega 配置兼容 | 未实现 | pending | pending |
| 太郎、肉鸽受限适配 | 未实现 | pending | pending |
| 书签、汉化、完整菜单、历史与登录适配 | 仅基础菜单和 Chromium 后退／前进；登录弹窗暂拒绝 | 合成网页后退／前进与卸载确认通过 | 真实登录／游戏 pending |
| 快捷键、CD 条、MCP、静态缓存 | 未实现 | pending | pending |
| 加密资料迁移、下载、启动器与卸载 | 未实现 | pending | pending |

## 2026-10-26 首次 CachyOS 接续记录

- Git：从无未提交修改的 linux checkout 接续至远端 `73e0036`；未回退、清理或读取私人交接。CEF 锁单独提交为 `e80698e`。
- 本机：CachyOS x86_64、KDE Wayland/XWayland，Python 3.14.8、GCC 16.2.1；GTK 3.24.52、X11 1.8.14、XTest 1.2.6、OpenSSL 3.6.5、libsecret 0.21.8.2。系统 CMake 缺失，使用已核验官方摘要的本地 CMake 4.4.4，未安装系统包。
- 依赖：官方索引 stable/linux64/standard 完整版本 `156.0.14+g4fa4ab5+chromium-156.0.8078.48`，695590857 字节，上游 SHA-1 与本地 SHA-256 均匹配锁文件。未修改 CEF 包内容。
- 执行环境：受限执行器看不到 `/dev/dri`，首次解包以信号 139 退出；残留目录保留。主机普通用户环境可见 DRM、`unshare -Ur true` 成功，fetch 完成全部身份核验。未把执行器问题当作 GPU/沙箱禁用理由。
- 编译修复：CEF CMake 的 C 编译器检测要求项目同时启用 C/C++；CEF 156 使用 C++20，宿主路径异常边界单独启用 `-fexceptions`；删除上游 sample 的 cwd rpath，成品仅有 `$ORIGIN`。helper 权限为 0755，无 setuid。
- 隔离测试：19 项 Python 检查及 LinuxPlatformChecks CTest 通过，build/test/verify 通过。补充跨进程资料锁、锁文件硬链接/宽松权限拒绝；失败夹具保留。
- 真实运行（均为新建合成 XDG）：about:blank 窗口已显示；第二实例退出 3 并报告资料占用；标准窗口关闭后退出 0，记录到的全部子进程已退出。没有真实登录或游戏操作。
- 沙箱：`chrome://sandbox` 显示 Layer 1 Namespace、PID/Network namespaces、Seccomp-BPF、TSYNC 为 Yes；renderer 的 `/proc` 状态也显示 NoNewPrivs=1、Seccomp=2 和独立 user namespace。没有关闭 sandbox 或修改系统策略。
- GPU：`chrome://gpu` 实际页面显示 Canvas、Compositing、Rasterization、WebGL、WebGPU 为 Hardware accelerated，GPU 进程持有 DRM render 节点。Mesa GBM/Vulkan 探测仍有警告，Vulkan 显示 Disabled、Video Encode 为 Software only；这些不等同于游戏性能或所有图形接口验收。
- 扩展边界：首轮发现 Chrome bootstrap 会加载系统 Plasma 扩展及 native host；增加 `disable-extensions` 后，新合成进程树未再出现它们。未来助手通过审查的宿主适配接入，不依赖自动扩展加载。
- 用户验收：候选可执行文件 SHA-256 `5b567d316ab9eb26232187de5b58b56c23d986796de9fd755b1303466a3c1bcb`，用户确认中文输入和页面 100%/125%/150% 缩放均正常，已正常关闭。该反馈不扩大为 KDE 系统缩放切换、多显示器或游戏输入验收。
- 下一步：进入外壳与消息边界；双窗、代理、助手及后续各里程碑仍未交付。诊断页不能代替完整浏览器验收。

原始日志、窗口截图、合成资料和构建产物只保留于忽略的 `.build/` 与本轮私有 runtime 目录，不进入公开提交。后续桌面检查用 `checks/run_desktop.py` 将证据与构建收据一同保存。

## 外壳第一批实现与验收

- 代码：从 Windows 配色、图标标签及紧凑布局规则提取无 Win32 依赖的 hosts/common 模型；Linux 单独编译接入，构建收据纳入公共目录。Windows 源码和构建接线未修改，未声称 Windows 已验证或已迁入新模型。
- 代码：CEF Views 无边框外壳、两布局、天蓝／群青、固定首标签和最多 7 个普通标签、地址栏、基础菜单、导航／标签快捷键；主题和布局严格解析后私有原子保存。安全读取拒绝链接、非私有文件、越界路径及过大输入；坏配置保留且不覆盖。
- 生命周期：窗口关闭等待浏览器卸载；标签卸载仅移除自己的 BrowserView。导航使旧文档票据失效，favicon 下载完成后再次核对视图与文档；模型不授予网页权限，完整消息桥尚未接入。
- 构建与隔离测试：GCC / CEF 156 Release 保留 -Werror、sandbox 和 $ORIGIN；20 项 Python 构建工具测试、LinuxPlatformChecks 与 ShellModelChecks 通过。新增共享源码指纹失效、安全配置读取、标签重叠／布局及文档身份检查。
- 真实运行（合成 XDG / 回环网页）：候选 SHA-256 `164e75289858436f309679ab5ed4afa0dce65fb146e296a3972ed24b830dec9a` 已观察新建／切换／关闭标签、普通网页标题和 favicon、地址导航、后退／前进；beforeunload 取消保留页面，确认只关闭当前普通标签，固定首标签保留。正常窗口关闭退出 0，检查器观察到的子进程全部退出。
- 持久化：菜单切换布局 2 与群青，shell-settings.json 权限 0600；复用这份已知合成资料重启，截图确认双排群青外壳恢复，进程退出 0。没有读取任何现有真实 profile。
- 证据边界：上述外壳桌面候选之后增加了零身份文档票据拒绝、相同导航代次下跨资料／视图测试及标签 ID 溢出保护；这些由最终构建和隔离检查验证，不重复宣称其拥有单独用户验收。
- 待修与待实现：历史导航后的 favicon 有时回到中性图标；中键关闭、完整菜单与下载、双窗、页面适配、完整消息桥、登录弹窗及其 opener 关系尚未完成。当前弹窗全部拒绝，不能验收登录。外壳外观和游戏行为仍待用户分项反馈；此前中文输入／页面缩放反馈只属于已记录基线候选。
- 后续顺序：补齐标签细节及消息边界，再实施双窗与页面适配，继续代理、助手及后续里程碑。此提交只代表外壳第一批源码，不代表里程碑 2 或完整 Linux 版完成。

## 里程碑 1：宿主基线

- 按 README 在 CachyOS 运行 doctor，锁定官方 stable linux64 standard 的完整版本和 SHA-256，提交锁文件；不能隐式拉取 latest。
- 使用宿主 build.py，修复实际 CEF 版本引出的编译或 API 差异；保留 -Werror、来源校验和 sandbox。普通构建不提升权限，不配置 setuid helper。
- 通过独立临时目录的原生路径、所有者、权限、链接、锁竞争、原子替换测试；所有子进程先执行 CEF 子进程入口，不争抢主资料锁。
- 真机验证空白页、进程退出、namespace sandbox、XWayland/GPU/中文输入/缩放；阻断时记录实际原因，不通过禁用沙箱或放宽系统策略绕过。

## 里程碑 2：外壳与页面边界

- 实现两种布局、天蓝/群青主题、图标标签、固定 GBF 标签、地址导航、菜单、下载和助手停靠。使用 CEF Views 管理控件；平台几何接口只使用 X11 后端。
- 从现有代码抽离不依赖 Win32 的业务模型，建立独立 CMake 目标；不直接 add_subdirectory 当前 app/，不把 Win32 类型带入共享接口。
- 双窗沿用一次拓宽/收窄、主视口锚定、空间不足保留单窗、布局完成后创建副窗，以及取消/失败/迟到回调门禁；单次同步只改变副窗页面。
- 消息桥绑定资料、视图、主 frame 来源、文档导航代次；只向可信游戏文档提供所需能力，旧回调不得作用于新页面。复用页面适配脚本时替换 WebView2 通道。
- 接入书签、汉化、加载画面开关、Moba 菜单、会话历史与登录弹窗；保留原来源及 opener 关系，不读凭据、不重放认证地址。

## 里程碑 3：代理与助手

- 原生代理控制器提供直连、系统、固定代理、PAC 和情景规则；独立实现 ZeroOmega 配置数据导入，不复制 GPL 实现到 MIT 核心。
- 每份资料独立的 CEF request context 与代理状态；串行提交、核对实际配置、成功后保存并刷新可见游戏一次。未知提交不重放、失败不显示已匹配；观察与登录页面不触发重复刷新。
- 锁定太郎/肉鸽原包来源和摘要，本地保持原包不变，适配器放在宿主源码；第三方包与游戏素材不镜像进本仓库。扩展 ZIP 不是已兼容的 Linux 插件。
- 已观察到太郎依赖 runtime/messages、storage、tabs、notifications、contextMenus、sidePanel、scripting、webRequest、debugger；肉鸽另使用 windows。对锁定版本建立调用清单，以宿主后台服务和面板协议接入所需子集。
- debugger 只接入审查过的只读网络观察命令；脚本只允许已审查资产及绑定的游戏 frame。组件只能看到授权视图和自己的存储；不支持的接口明确失败，不返回假成功。
- 验证双窗目标绑定、面板停靠/拆出、Buff 标记、配置、存储变化通知、后台重启及跨资料隔离。

## 里程碑 4：输入、观察与数据

- 快捷键抽离动作状态、队列、DOM 只读探测；键码采用跨平台表示并显式转换 Windows 绑定。X11/XTest 承接原生输入，保留焦点/遮挡/目标校验、手动鼠标移动取消和超时；不以 JS click 或游戏接口代替系统输入。
- CEF 内部网络观察供 CD 条和 MCP 独立订阅。保留 MCP 工具参数、分页、容量、脱敏、600 秒 rolling、stop 冻结语义；stdio 入口不变，IPC 改为 XDG_RUNTIME_DIR 下同用户 Unix socket，核对对端 UID 和目标进程身份，不开放远程调试端口。
- 静态缓存保留认证、代理、Vary、Range、有效期及响应头语义；无法正确处理的响应交给 Chromium，不因缓存功能影响普通请求。
- Linux 加密实现兼容 SFPACK01、PBKDF2-HMAC-SHA256、AES-256-GCM 和显式 UTF-16LE 密码编码。导入只新建资料、跳过 Cookie、回读核对；保留原归档和其他资料，失败不得激活半成品。
- 合成归档验证中文/非 BMP 密码、损坏/错误密码、重名、角色配置映射及重启。不得借真实用户备份补测试；不能把原生 wchar_t 内存直接当成 Windows 密码字节。
- 实现用户级启动器、桌面入口和卸载；正式卸载经用户明确确认，按安装归属及文件身份处理，不递归清理未知内容。本次源码工作不执行实际卸载。

## 交付门槛

- 共享代码修改须通过 Windows 宿主官方 build/test/verify；Linux 通过 CachyOS 原生构建、隔离检查、文件身份核验和 KDE 实机桌面检查。
- 用户正常使用时分项反馈登录、代理、两助手、快捷键、CD 条及资料持久化。系统输入返回成功、组件连接成功或空白页启动不能替代游戏结果。
- GPU/中文输入/常用缩放、双窗贴边/切换、焦点转移和正常退出都须记录具体构建身份；每个里程碑分别记录代码、构建、隔离测试、运行和用户反馈。
- 原生 Wayland、其他发行版安装包、Linux WebView SDK、登录态迁移和云同步不在首版范围。
- 经审查只推送 linux 源码分支；不强推、不推 main、不创建版本标签、二进制安装包或 Release。所有功能及对应验收完成前，不称为日用 Linux 版本。
