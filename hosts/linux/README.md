# StarryFish Linux：CachyOS 接续入口

目标为 **CachyOS x86_64、KDE Plasma Wayland 桌面、XWayland**。CEF Views + Alloy style 保留 StarryFish 自定义外观；现代 CEF 使用 Chrome bootstrap。Linux 与 Windows WebView2 为独立宿主，不直接链接带 Win32 依赖的 app/ 目标。

**当前是移植基线，不是日用版本。** 已有构建入口、依赖锁定机制、XDG 路径、资料锁、原子写入和 CEF 启动骨架的源码。尚未在 Linux 编译或运行；只有 Python 构建工具的隔离检查已通过。标签、双窗、主题、代理、助手、网页适配、快捷键、MCP、资料迁移和安装均未交付。完整范围与验收顺序见 [移植计划](PORTING_PLAN.md)。

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

构建需要 Python 3.10+、CMake 3.21+、Ninja、C++17 编译器及 CEF 所需的 Linux 系统开发库。KDE 会话须提供 DISPLAY、XWayland 和有效的 XDG_RUNTIME_DIR。XTest、OpenSSL、Secret Service 是后续输入/资料功能所需能力，当前不会因它们缺失而伪造成功或自动安装。

### 首次锁定 CEF

当前 cef.lock.json 的 cef 为 null：这是明确的未配置状态，fetch/build 会拒绝继续。按两台机器接续方案，第一次依赖选择和下载在 CachyOS 完成，不在 Windows 下载一个无法验证的运行库。

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

--smoke-test 只改变初始页面为 about:blank，不是断网模式，也不承诺 Chromium 没有背景网络活动。当前没有额外命令行开关；宿主强制 X11 后端，拒绝浏览器主进程的其他开关。正常入口会打开 GBF My Page，但当前不建议用它登录真实账号。

分别记录窗口显示、关闭后所有本次子进程退出、第二实例资料锁、GPU 渲染、中文输入、缩放和沙箱实际状态。settings.no_sandbox=false 只是代码配置，不是运行验收。CachyOS 若不能正常启用 namespace sandbox，应保留诊断并停在该里程碑；不加 --no-sandbox、不自动提权、不修改系统策略。构建不会执行 SET_LINUX_SUID_PERMISSIONS。

## 本地数据约定

| 内容 | 路径 |
|---|---|
| 宿主配置 | $XDG_CONFIG_HOME/StarryFish，默认 ~/.config/StarryFish |
| Chromium 持久资料 | $XDG_DATA_HOME/StarryFish/UserData/Default，默认位于 ~/.local/share |
| 未来的自有静态缓存 | $XDG_CACHE_HOME/StarryFish，默认位于 ~/.cache |
| 未来的同用户 IPC | $XDG_RUNTIME_DIR/StarryFish，无 /tmp 回退 |

Chromium profile 内的缓存仍由 Chromium 管理；独立静态缓存尚未实现。相对 XDG 路径按规范忽略，使用 HOME 默认值；runtime 必须为桌面提供的绝对私有目录。应用目录只接受当前用户及私有权限，拒绝符号链接；资料锁保留 inode，不在退出时 unlink。原子写入和锁仅是平台原语，尚未实现设置 UI 或资料切换。

## 给接续开发者

先读本目录 AGENTS.md、本页和 PORTING_PLAN.md，再读根目录 handover/DEVELOPMENT.md、docs/UI_REQUIREMENTS.md。公开 fork 可能不带原工作站的私人工作区交接，不需要寻找或复制用户资料。

下一步依次执行：本机 doctor → 明确锁定 CEF → build/test/verify → 隔离空白页和沙箱验收 → 修复确认的基线问题 → 继续外壳与消息边界。每项填写代码、构建、隔离测试、用户反馈四种状态。不要直接跳到助手 ZIP 加载，也不要把窗口显示当作完整移植成功。

本分支仅发布源码。现有 Windows 发布工作流监听 main 和 v* 标签；不手动触发它，不为 Linux 修改产品版本或建立 Release。

官方参考：[CEF 架构](https://github.com/chromiumembedded/cef/blob/master/docs/architecture.md)、[Linux sample](https://github.com/chromiumembedded/cef/blob/master/tests/cefsimple/cefsimple_linux.cc)、[发行索引](https://cef-builds.spotifycdn.com/index.html)。
