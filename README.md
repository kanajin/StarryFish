<p align="center"><img src="assets/starryfish.png" width="160" alt="StarryFish 蓝色小鱼"></p>

# StarryFish

**面向《碧蓝幻想》（GBF）的 Windows 专用轻量浏览器。**

> **本项目完全是 vibe coding 的产物。** 由维护者提出需求、体验并反馈，AI 编写和迭代代码、界面及构建流程，产品图标也由 AI 生成。已有自动检查和实际使用反馈，但不意味着没有缺陷，也不等同于专业安全审计。

基于 Microsoft Edge WebView2，不自带完整 Chromium。将游戏、太郎、肉鸽地图及常用脚本集中到一个窗口，保留轻量的操作界面。

## 下载与安装

v0.1.8 改进 GBF 战斗历史后退，以及 Mobage 登录弹窗关闭后的页面恢复。

在 [Releases](https://github.com/kanajin/StarryFish/releases/latest) 下载 **StarryFish-版本-windows-x64-setup.exe**，直接运行单个安装器，无需解压。

- Windows 10/11 x64；需要 Microsoft .NET Framework 4.8，以及 **稳定版 WebView2 Runtime 154 或更高**。安装器会检查版本，缺失时停止并提示；[微软官方下载](https://developer.microsoft.com/microsoft-edge/webview2/)。
- 首次安装可联网获取太郎、肉鸽，或选择自己下载的两个本地 ZIP 离线接入；两种方式都核对压缩包、插件角色、权限和资源，原 ZIP 保留。ZeroOmega 固定版本随包提供。联网下载遵循 Windows 系统代理及绕过规则，连接/读取超时为60秒；浏览器内 ZeroOmega 不控制安装器下载。
- 安装向导中选择安装路径（必须可写且为空，或已有本程序安装）；创建当前用户开始菜单快捷方式，安装后可按 Win+S 搜索 **StarryFish**。已有安装更新时保持原路径。
- 安装时选择 **acgpower / 岛风go / 直连**。使用加速器时先启动并启用它；浏览器启动直接访问 GBF My Page，后台恢复本资料的连接选择，不显示准备页。手动切换代理成功后刷新当前页面；连接失败仍可访问页面或重新选择代理。浏览器不提供代理服务。Mobage 登录、验证码由本人处理。
- 首发包未做商业代码签名；可用 Release 的 `SHA256SUMS.txt` 核对下载完整性。不要关闭系统安全功能。
- 首版使用独立 StarryFish 资料，不自动读取或迁移旧 Gbrowser 开发版的登录态和配置。旧目录保留。

## 功能

- 两种布局：紧凑侧栏 / 经典双排工具栏；浅色天蓝、深色群青。两布局采用网页图标标签，空间不足时重叠排列；均提供刷新按钮。界面需求见 [UI_REQUIREMENTS.md](docs/UI_REQUIREMENTS.md)。
- 独立用户资料、代理选择与窗口布局记忆。
- 代理菜单读取 ZeroOmega 的自定义情景模式并支持切换，实际配置匹配的当前模式显示勾选。
- 三点菜单可隐藏 GBF 加载界面中的角色和 “Now Loading...” 文字，保留暗色加载背景。
- 多标签，＋/Ctrl+T 新建，Ctrl+W 或中键关闭普通标签；固定 GBF 主标签保留。
- 双游戏窗口；箭头点击一次把右侧当前网址同步到副窗，相同网址则刷新副窗。
- 太郎与肉鸽侧栏支持开关、分隔调整及拆出窗口。
- 内置书签、汉化；支持书签右键设置、中键新标签。
- 可隐藏 Moba 栏和副窗右侧游戏菜单；支持正常网页下载。
- 静态资源缓存：三点菜单可开关、查看命中和清理；资源放在程序旁的 `Cache/Static-v1`，容量上限 10 GiB；超过一周未命中的资源自动清理，命中时间跨重启保留。只复用服务器允许且未过期的公开 GBF 资源，不缓存账号或游戏接口。实际增量加速效果尚未测量。

不包含 Chrome Web Store、通用油猴管理器、游戏自动操作或自动更新器。助手默认绑定固定主 GBF 标签。

## 更新

关闭浏览器后，运行新版单 EXE 安装器，或给安装器加 `--upgrade` 参数，显式更新本体，保留当前安装路径、组件及资料。首次安装与更新不会静默覆盖未知状态。

太郎与肉鸽后续仍由本人从上游下载，覆盖安装目录中 `Components/tarou`、`Components/arcarum-dungeon` 的原位置；关闭浏览器后从与当前本体对应的安装包运行该版本的单 EXE 安装器并加 `--reload-helpers` 参数，通过检查后下次启动加载。保留包的原始目录结构，不要在目录内放混杂旧文件。

## 卸载

先正常关闭 StarryFish，再从 Windows“已安装的应用”、Geek 的程序列表或开始菜单“卸载 StarryFish”进入卸载向导。确认后清除此安装的程序、登录态、配置、运行缓存及有归属记录的维护文件；关闭卸载窗口后自动清理开始菜单入口、安装器缓存和卸载器自身。卸载按文件身份与安装归属执行，安装目录中无关文件、被替换后无法核实的文件及其他安装的资料保留。遇到占用、链接或归属异常会停止并提示，不能将安装根整体删除。详情见 [安装与卸载需求](docs/INSTALLATION_REQUIREMENTS.md)。

## 隐私与联网

源码与发布包不包含维护者的账号、Cookie、密码、代理备份、浏览器资料、游玩截图、诊断日志或历史测试目录。运行资料留在本机。发布隐私说明见 [PRIVACY.md](PRIVACY.md)。

StarryFish 没有自建遥测服务器；嵌入网页和第三方扩展仍按原逻辑联网。太郎云功能可能发送游戏 UID、配置、编成及统计到其原服务；汉化会获取译文，启用第三方翻译服务时按用户配置发送请求。登录与账号信息只应输入官方游戏/登录页面。

## 从源码构建

已有环境要求：Windows x64、VS2022（C++ 桌面开发、CMake、Windows SDK、Roslyn/.NET Framework 4.8 编译引用）、Python 3.11+、Node.js。不需要 CEF，不依赖维护者电脑上的构建产物。

```powershell
python -B tools/fetch_dependencies.py
cd hosts/webview2
python -B build.py build
python -B build.py test
python -B build.py verify
python -B build.py package-setup
cd ../..
python -B tools/package_release.py 0.1.8
```

依赖从官方固定 URL 获取并验证 SHA256。太郎/肉鸽固定快照仅用于构建检查；终端用户首装可解析上游最新版本，或选择两个本地 ZIP。包输出在 `dist/`。推送 `main` 会构建并保留检查产物，推送 `v*` 标签会额外发布 GitHub Release；工作流不会访问或上传本地用户资料。发布新版本前需同时更新 `VERSION` 与 `RELEASE_NOTES.md`，标签版本必须与 `VERSION` 一致。

## 许可与致谢

StarryFish 自有代码采用 [MIT](LICENSE)。第三方组件遵循各自许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。感谢 ZeroOmega、Tarou、arcarumDungeon、gbf-bookmark、BLHXFY 及 WebView2 项目。

这是非官方玩家工具，与 Cygames/Mobage 无关联。请遵守游戏及第三方服务的使用条款。
