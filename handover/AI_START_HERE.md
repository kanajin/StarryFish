# 给接手 AI 的说明

先阅读 [交接入口](README.md)、[功能状态](FEATURE_STATUS.md)、[源码地图](DEVELOPMENT.md)、仓库 [README](../README.md) 和 [发布说明](../RELEASE_NOTES.md)。若 checkout 中存在 AGENTS.md，按目录层级读取适用规则。不要假定另一台机器具有维护者的私人文档、全局 skill 或本地构建产物。

开始修改前运行 `git status --short --branch`，阅读已有 diff，保留未提交工作。确认当前提交和 VERSION；本交接的公开基线为 v0.1.12，未来 fork 的状态可能已经改变。

稳定功能要求分别查阅 [界面需求](../docs/UI_REQUIREMENTS.md)、[安装与卸载需求](../docs/INSTALLATION_REQUIREMENTS.md)、[资料迁移](../docs/PROFILE_TRANSFER.md)、[网络采集](../docs/NETWORK_CAPTURE.md)。有矛盾时先对照当前代码和版本记录，不能拿历史候选描述覆盖现役要求。

Windows 产品入口是 hosts/webview2，共享 app 仍含 Windows 依赖。公开源码没有可用 Linux 产品，不能仅凭目录名称或编译宏推定跨平台。Chrome/Iron 快捷键实验已经停止，继续集成请从仓库内 WebView SDK 开始。

保护用户 profile、密码、令牌、Cookie 和代理凭据。排查使用独立虚构资料，不读取私人归档、不操作密码管理器，也不为了补日志重复登录、扩展初始化或游戏操作。保留现有构建目录、依赖缓存、测试资料和运行证据；整理文档不授权删除。

按 Windows 宿主 build.py 构建与验证。记录代码实现、隔离检查、真实用户反馈和公开发布各自的状态。系统调用成功或构建通过不足以证明游戏操作成功。对于尚未验收的组合，写明具体缺口，不扩大已有反馈。

除非用户明确要求，不推送、不打包、不创建 Release、不安装到用户环境。发布前检查隐私、版本、许可与实际产物身份；不要整目录暂存私人记录或运行资料。

可直接使用的接手请求：

> 阅读 handover 目录和相关需求，检查 Git 状态与已有 diff，然后处理我指定的功能。先找现有实现和检查入口，保留本地工作及用户资料。按宿主脚本进行必要验证，报告修改内容、验证范围及未验收场景。未经授权不要推送、打包或发布。
