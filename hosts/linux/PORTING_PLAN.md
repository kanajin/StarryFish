# Linux 移植：实施与验收账本

目标：完整移植 Windows v0.1.12 浏览器功能，在 CachyOS x86_64 / KDE Plasma Wayland 上通过 XWayland 使用。保留 StarryFish 外观，以 CEF Views + Alloy style 和宿主适配层实施。旧 Ubuntu 24.04 计划只作为历史背景，不再是首要交付平台。

本次由 Windows 准备 linux 分支、构建入口及公开接续；后续在 CachyOS 本机继续。根基线为 3190a37（v0.1.12 加公开手册），现役行为依据 docs/UI_REQUIREMENTS.md、docs/INSTALLATION_REQUIREMENTS.md、docs/PROFILE_TRANSFER.md、docs/NETWORK_CAPTURE.md。平台差异记录在本文件，不复制一份 Windows 需求。

## 当前状态

| 交付项 | 代码 | 检查证据 | Linux / 用户实测 |
|---|---|---|---|
| doctor、显式 CEF lock/fetch、build/test/verify | 已实现 | Python 离线夹具通过 | 待 CachyOS 检查；CEF 尚未锁定 |
| CEF Views Alloy / X11 启动骨架 | 已接线 | 静态检查，非 Linux 编译 | pending |
| XDG 路径、私有目录、资料锁、原子写入 | 已接线／提供原语 | 已编写原生隔离测试，未执行 | pending |
| 双布局、主题、标签、双窗与消息桥 | 未实现 | pending | pending |
| 原生代理与 ZeroOmega 配置兼容 | 未实现 | pending | pending |
| 太郎、肉鸽受限适配 | 未实现 | pending | pending |
| 书签、汉化、菜单、历史与登录适配 | 未实现 | pending | pending |
| 快捷键、CD 条、MCP、静态缓存 | 未实现 | pending | pending |
| 加密资料迁移、下载、启动器与卸载 | 未实现 | pending | pending |

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
