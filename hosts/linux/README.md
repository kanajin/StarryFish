# StarryFish Linux：CachyOS 接续入口

目标为 **CachyOS x86_64、KDE Plasma Wayland 桌面、XWayland**。CEF Views + Alloy style 保留 StarryFish 自定义外观；现代 CEF 使用 Chrome bootstrap。Linux 与 Windows WebView2 为独立宿主，不直接链接带 Win32 依赖的 app/ 目标。

**当前是移植基线，不是日用版本。** 已在 CachyOS 完成首次 Linux 编译、原生隔离检查和文件身份核验，并运行了全新 XDG 目录中的空白页、GPU 与沙箱诊断。资料锁和正常退出已观察，Chromium 报告 namespace/seccomp 沙箱及硬件加速；中文输入和页面缩放已有用户确认，KDE 系统缩放切换仍待验证。已继续实现双布局蓝色外壳、主题记忆、固定首标签与普通标签、地址导航和基础菜单，并用合成网页检查生命周期。双窗、完整消息桥、代理、助手、网页适配、游戏快捷键、MCP、资料迁移和安装仍未交付。完整范围与证据边界见 [移植计划](PORTING_PLAN.md)。

## 在 CachyOS 接续

首次获取源码：

```sh
git clone --branch linux --single-branch https://github.com/kanajin/StarryFish.git
cd StarryFish/hosts/linux
python -B build.py doctor
python -B build.py test --portable
```

已有 checkout 时先查看状态与 diff，再 fetch/switch；不要用 reset 或 clean 覆盖本地工作。

doctor 不下载、不安装、不启动浏览器、不读取用户资料。它报告架构、构建工具、会话、X11 显示、GPU 设备存在性和 pkg-config 依赖；沙箱、GPU 实际渲染、缩放及输入始终需要运行验证。Windows 上返回非零是正常的平台不满足，不代表 Linux 已测试失败。

构建需要 Python 3.10+、CMake 3.21+、Ninja、C/C++20 编译器及 CEF 所需的 Linux 系统开发库。KDE 会话须提供 DISPLAY、XWayland 和有效的 XDG_RUNTIME_DIR。XTest、OpenSSL、Secret Service 是后续输入/资料功能所需能力，当前不会因它们缺失而伪造成功或自动安装。

本次主机未安装系统 CMake，已将官方 CMake 4.4.4 放在忽略的 `.deps/toolchain` 中；使用它的现有 checkout 可先执行：

```sh
export PATH="$PWD/.deps/toolchain/cmake-4.4.4-linux-x86_64/bin:$PATH"
python -B build.py doctor
```

该工具来自 [Kitware 官方发行](https://github.com/Kitware/CMake/releases/tag/v4.4.4)，Linux x86_64 tar.gz 的 SHA-256 为 `e5bb807f7728cb60cd8b27ebc97a2edb469b68655f21e844a600c3575b76f5bb`。不提交工具包，不要求修改系统软件。受限开发执行器可能隐藏 GPU、阻止桌面连接或导致大文件解包异常；必须区分执行器和真实主机环境，不能据此修改 Chromium sandbox。

### 首次锁定 CEF

当前已锁定 `156.0.14+g4fa4ab5+chromium-156.0.8078.48`（stable/linux64/standard），完整摘要见 `cef.lock.json`。现有 checkout 直接执行 fetch；不要再次 lock 或隐式升级。以下步骤仅说明首次建立锁的流程，已有锁的升级必须单独审查。

1. 从 [官方发行索引](https://cef-builds.spotifycdn.com/index.html) 选择 **stable / linux64 / standard** 的完整版本号。
2. 显式传入该版本；以下占位值必须替换为实际完整版本。

```sh
python -B build.py lock --version '完整CEF版本号'
git diff -- cef.lock.json
```

lock 只接受完整版本，核对官方索引中的平台、stable 渠道、standard 类型、文件名、大小和 SHA-1，然后下载并计算 SHA-256 写入锁文件。官方 SHA-1 用于与 HTTPS 索引匹配；本地计算的 SHA-256 是后续内容锁，不冒充上游签名。此命令不展开或执行下载内容。

确认锁文件后选择性提交 cef.lock.json，后续机器直接 fetch，不再解析 latest。已有锁不会被 lock 覆盖；升级须单独审查依赖变更。

### 构建与检查

```sh
python -B build.py fetch
python -B build.py build
python -B build.py test
python -B build.py verify
```

- fetch 校验锁定 SHA-256，限制展开量，拒绝路径穿越、重复成员、链接和设备；不覆盖已有不匹配依赖。
- build 只在普通用户的 Linux x86_64 环境执行，使用 CMake/Ninja、Release 和 sandbox。检查构建前后源码及依赖身份，输出 .build/out/StarryFish 与 .build/build-receipt.json。
- test --portable 是 Python 离线夹具检查，可在 Windows 运行；不等于 Linux C++ 或浏览器验收。
- 完整 test 先做可移植检查，再核对 Linux 收据并运行 CTest 的独立临时目录测试，不启动浏览器或触及现有资料。
- verify 对比源码、锁文件、展开依赖和输出文件；源码或文档变动后必须重建。

.deps/、.build/ 为本地保留内容，不提交。测试目录只清理本次测试自己创建的夹具，失败的历史产物与用户资料不作自动清理对象。

## 首次桌面检查

先在 KDE 会话中以普通用户运行空白页候选，全部 XDG 目录使用新建隔离目录。下面的命令不会借用现有 StarryFish 资料，也不自动删除测试证据：

```sh
test -n "$XDG_RUNTIME_DIR" || exit 1
sf_check_root=$(mktemp -d)
sf_check_runtime=$(mktemp -d "$XDG_RUNTIME_DIR/starryfish-check.XXXXXX")
XDG_CONFIG_HOME="$sf_check_root/config" \
XDG_DATA_HOME="$sf_check_root/data" \
XDG_CACHE_HOME="$sf_check_root/cache" \
XDG_RUNTIME_DIR="$sf_check_runtime" \
  .build/out/StarryFish --smoke-test
```

--smoke-test 只改变初始页面为 about:blank，不是断网模式，也不承诺 Chromium 没有背景网络活动。宿主强制 X11 后端并禁用系统自动扩展加载。只额外接受互斥的 `--diagnostic-page=input|gpu|sandbox`，拒绝其他主进程开关及位置参数。正常入口会打开 GBF My Page，但当前不建议用它登录真实账号。

可使用下列显式桌面检查入口。它先验证构建收据，每次新建私有 XDG 目录，在 `.build/smoke-*` 保留构建身份、启动记录、仅本次进程树的沙箱状态、退出结果和日志。不会读取已有资料，不会强杀或自动清理。完成后正常关闭对应窗口；Ctrl+C 只中断检查器，浏览器与证据保留。

```sh
python -B checks/run_desktop.py --page blank --check-lock
python -B checks/run_desktop.py --page sandbox
python -B checks/run_desktop.py --page gpu
python -B checks/run_desktop.py --page input
```

GPU 页面使用 Shadow DOM，CEF 文本回读可能为空，应查看实际页面，不能把空文本当作 GPU 检查成功。中文检查只输入“星鱼中文测试”等合成内容，观察候选框、提交、删除及中英文切换；Ctrl+加号/减号调整网页缩放，Ctrl+0 恢复 100%，检查页标题显示倍率。页面缩放与 KDE 系统缩放是两项独立检查。这些诊断入口不开放远程调试端口，也没有网页到宿主的特权消息桥。

分别记录窗口显示、关闭后所有本次子进程退出、第二实例资料锁、GPU 渲染、中文输入、缩放和沙箱实际状态。settings.no_sandbox=false 只是代码配置，不是运行验收。CachyOS 若不能正常启用 namespace sandbox，应保留诊断并停在该里程碑；不加 --no-sandbox、不自动提权、不修改系统策略。构建不会执行 SET_LINUX_SUID_PERMISSIONS。

## 当前外壳候选

- CEF Views 无边框窗口提供布局 1 右侧栏、布局 2 双排顶部，以及天蓝／群青主题。菜单可切换布局与主题；Alt+F 打开菜单。
- 首标签固定不可关闭；另可创建最多 7 个普通标签。Ctrl+T 新建，Ctrl+W 关闭普通标签，Ctrl+Tab／Ctrl+Shift+Tab 切换，Ctrl+L 聚焦地址栏；F5 刷新，Alt+左右键后退／前进。
- 地址栏支持 HTTP、HTTPS 和 about:blank，拒绝带用户名密码的网址、文件及脚本协议。标题保留在悬停提示中，图标使用受导航代次约束的异步下载。历史导航后的 favicon 恢复、中键关闭仍待补齐。
- 当前弹出网页窗口明确拒绝，登录弹窗的来源、opener 和生命周期适配未完成；不要用本候选验收真实登录。菜单、下载、助手停靠和页面功能也尚未达到 Windows 范围。
- 主题和布局保存到私有配置目录的 shell-settings.json，原子替换；无效、未知格式或不安全文件保持原样，当前会话使用默认值并停止覆盖该文件。尚无资料选择 UI。

平台无关配色、标签几何及文档代次模型在 hosts/common，通过独立 CMake target 接入；没有链接 app/ 或改动 Windows 宿主。文档代次目前用于过期图标回调门禁，不等于已向游戏暴露完整消息桥。

## 本地数据约定

| 内容 | 路径 |
|---|---|
| 宿主配置 | $XDG_CONFIG_HOME/StarryFish，默认 ~/.config/StarryFish |
| Chromium 持久资料 | $XDG_DATA_HOME/StarryFish/UserData/Default，默认位于 ~/.local/share |
| 未来的自有静态缓存 | $XDG_CACHE_HOME/StarryFish，默认位于 ~/.cache |
| 未来的同用户 IPC | $XDG_RUNTIME_DIR/StarryFish，无 /tmp 回退 |

Chromium profile 内的缓存仍由 Chromium 管理；独立静态缓存尚未实现。相对 XDG 路径按规范忽略，使用 HOME 默认值；runtime 必须为桌面提供的绝对私有目录。应用目录只接受当前用户及私有权限，拒绝符号链接；资料锁保留 inode，不在退出时 unlink。原子写入已用于主题／布局记忆，安全读取有长度、所有者、权限和链接检查；资料切换尚未实现。

## 给接续开发者

先读本目录 AGENTS.md、本页和 PORTING_PLAN.md，再读根目录 handover/DEVELOPMENT.md、docs/UI_REQUIREMENTS.md。公开 fork 可能不带原工作站的私人工作区交接，不需要寻找或复制用户资料。

接续时先核对已保留的证据及账本，避免重复已通过的流程。当前中文输入/页面缩放已获确认，双布局、主题和基础标签已有合成桌面证据，下一步补齐外壳缺口及消息桥、双窗。每项分别填写代码、构建、隔离测试、真实运行和用户反馈状态。不要直接跳到助手 ZIP 加载，也不要把窗口显示当作完整移植成功。

本分支仅发布源码。现有 Windows 发布工作流监听 main 和 v* 标签；不手动触发它，不为 Linux 修改产品版本或建立 Release。

官方参考：[CEF 架构](https://github.com/chromiumembedded/cef/blob/master/docs/architecture.md)、[Linux sample](https://github.com/chromiumembedded/cef/blob/master/tests/cefsimple/cefsimple_linux.cc)、[发行索引](https://cef-builds.spotifycdn.com/index.html)。
