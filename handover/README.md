# 接手 StarryFish

这份交接以 2026-10-24 的公开版本 v0.1.12 为起点。维护者目前不计划继续积极开发；即使后续有本地修改，也不保证上传。Fork 后请以自己拿到的提交和实际验证结果维护版本状态。

StarryFish 是面向《碧蓝幻想》的 Windows 浏览器，将游戏、太郎、肉鸽地图、书签和汉化集中在一个窗口。面向玩家的功能介绍见 [features.html](../features.html)，开发入口如下：

- [功能状态与尚未验收的场景](FEATURE_STATUS.md)
- [源码地图、构建与维护](DEVELOPMENT.md)
- [交给其他 AI 的接手说明](AI_START_HERE.md)

稳定行为仍在 [界面需求](../docs/UI_REQUIREMENTS.md)、[安装与卸载需求](../docs/INSTALLATION_REQUIREMENTS.md)、[资料迁移说明](../docs/PROFILE_TRANSFER.md) 中维护。这里记录接手时需要知道的状态和定位，不另立一套功能要求。

公开版本身份由 [VERSION](../VERSION) 和 [发布说明](../RELEASE_NOTES.md) 对照确认。Windows 是当前完整产品。Linux 仅有未纳入公开源码的本地实验骨架，没有真实 Linux 构建或运行通过记录，不能据此承诺 Linux 支持。Chrome/Iron 快捷键方案已停止，公开仓库提供的是 [WebView SDK 预览](../sdk/webview-hotkeys/README.md)。

这些文档只依赖仓库内文件。接手者无需取得维护者的私人交接、账号资料、历史候选安装器或机器上的临时测试目录。
