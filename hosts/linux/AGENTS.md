# Linux 宿主移植规则

- 本目录是 Linux 移植实验，不是可安装产品。用户已确认保留 StarryFish 自定义外观，并接受由宿主替代 Chrome 扩展功能；按 `PORTING_PLAN.md` 实施。
- 首个目标为 CachyOS x86_64 / KDE Plasma Wayland，通过 XWayland 运行；CEF Views + Alloy style，现代 Chrome bootstrap。先锁官方 CEF 完整稳定版本及摘要，再构建；不隐式选择 latest，不运行未审核的助手 ZIP 脚本。
- 本目录公开接续入口为 README.md 和 PORTING_PLAN.md。Windows 阶段仅做源码准备与可移植检查；真实 Linux 构建、桌面和沙箱验收在 CachyOS 本机完成。不能把旧交接中的工具链状态当作新机器事实；先运行 python -B build.py doctor。
- 使用 build.py 的 lock、fetch、build、test、verify 入口。test --portable 只检查构建工具；完整 test 需要真实 Linux 构建。修改源码后必须重建；输入变化或依赖/产物不匹配时停止。
- 保留 Chromium sandbox。不得增加 `--no-sandbox`、设置 `no_sandbox=true` 或放宽对本地数据/网页来源/外部协议的安全策略。
- 首阶段只做自绘浏览器外壳及宿主状态/消息边界；代理模式、ZeroOmega 情景、太郎/肉鸽面板、书签/汉化、缓存和用户数据分别设计、实现、验证。扩展 ZIP 不等价于 Linux 插件包。
- Linux 的 XDG 资料路径必须与 Windows 资料隔离。测试只用新建合成目录，保留已有源码、资料、测试包和历史证据。禁止浏览器以 root 运行；构建不 sudo，也不设置 sandbox helper 的 setuid 权限。
- 本轮授权仅限审查后的源码提交和推送 linux 分支，不推 main、不打版本标签、不创建安装包或 Release。私人交接、用户资料、抓包、构建证据、.deps、.build 和第三方原包不得提交。
