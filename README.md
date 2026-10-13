<p align="center"><img src="assets/starryfish.png" width="160" alt="StarryFish 蓝色小鱼"></p>

# StarryFish

**面向《碧蓝幻想》（GBF）的 Windows 专用轻量浏览器。**

> **本项目完全是 vibe coding 的产物。** 由维护者提出需求、体验并反馈，AI 编写和迭代代码、界面及构建流程，产品图标也由 AI 生成。已有自动检查和实际使用反馈，但不意味着没有缺陷，也不等同于专业安全审计。

基于 Microsoft Edge WebView2，不自带完整 Chromium。将游戏、太郎、肉鸽地图及常用脚本集中到一个窗口，保留轻量的操作界面。

## 下载与安装

在 [Releases](https://github.com/kanajin/StarryFish/releases/latest) 下载 **StarryFish-版本-windows-x64-setup.zip**，完整解压后运行 **StarryFishSetup.exe**。不要只取出其中一个 EXE。

- Windows 10/11 x64；需要 Microsoft .NET Framework 4.8，以及 **稳定版 WebView2 Runtime 154 或更高**。安装器会检查版本，缺失时停止并提示；[微软官方下载](https://developer.microsoft.com/microsoft-edge/webview2/)。
- 安装过程需要网络，自动从上游获取太郎、肉鸽并加载；ZeroOmega 使用固定版本随包提供。
- 安装到当前用户 `%LOCALAPPDATA%/Programs/StarryFish/Product`，无需管理员权限。安装完成后启动 `StarryFish.exe`；可以自行为它创建桌面快捷方式。
- 先自行启动 acgpower，再在浏览器代理菜单选择对应配置；确认生效后进入 GBF。浏览器不提供代理服务。Mobage 登录、验证码由本人处理。
- 首发包未做商业代码签名；可用 Release 的 `SHA256SUMS.txt` 核对下载完整性。不要关闭系统安全功能。
- 首版使用独立 StarryFish 资料，不自动读取或迁移旧 Gbrowser 开发版的登录态和配置。旧目录保留。

## 功能

- 两种布局：紧凑侧栏 / 经典双排工具栏；浅色天蓝、深色群青。
- 独立用户资料、代理选择与窗口布局记忆。
- 多标签，＋/Ctrl+T 新建，Ctrl+W 或中键关闭普通标签；固定 GBF 主标签保留。
- 双游戏窗口；箭头点击一次把右侧当前网址同步到副窗，相同网址则刷新副窗。
- 太郎与肉鸽侧栏支持开关、分隔调整及拆出窗口。
- 内置书签、汉化；支持书签右键设置、中键新标签。
- 可隐藏 Moba 栏和副窗右侧游戏菜单；支持正常网页下载。

不包含 Chrome Web Store、通用油猴管理器、游戏自动操作或自动更新器。助手默认绑定固定主 GBF 标签。

## 更新

关闭浏览器后，从新版本解压目录运行 `StarryFishSetup.exe --upgrade`，显式更新本体，保留当前安装路径、组件及资料。首次安装与更新不会静默覆盖未知状态。

太郎与肉鸽后续仍由本人从上游下载，覆盖安装目录中 `Components/tarou`、`Components/arcarum-dungeon` 的原位置；关闭浏览器后从与当前本体对应的安装包运行 `StarryFishSetup.exe --reload-helpers`，通过检查后下次启动加载。保留包的原始目录结构，不要在目录内放混杂旧文件。

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
python -B tools/package_release.py 0.1.0
```

依赖从官方固定 URL 获取并验证 SHA256。太郎/肉鸽固定快照仅用于构建检查；终端用户首装仍解析上游最新版本。包输出在 `dist/`。推送 `main` 会构建并保留检查产物，推送 `v*` 标签会额外发布 GitHub Release；工作流不会访问或上传本地用户资料。发布新版本前需同时更新 `VERSION` 与 `RELEASE_NOTES.md`，标签版本必须与 `VERSION` 一致。

## 许可与致谢

StarryFish 自有代码采用 [MIT](LICENSE)。第三方组件遵循各自许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。感谢 ZeroOmega、Tarou、arcarumDungeon、gbf-bookmark、BLHXFY 及 WebView2 项目。

这是非官方玩家工具，与 Cygames/Mobage 无关联。请遵守游戏及第三方服务的使用条款。
