# 开发接手

## 源码地图

| 入口 | 负责内容 |
|---|---|
| [Windows 主入口](../hosts/webview2/src/main.cpp) | WebView 生命周期、窗口布局、输入与各项功能接入 |
| [Windows 构建脚本](../hosts/webview2/build.py) | 构建、检查、输入指纹和产物校验 |
| [共享源码](../app/) | C++/Python 支持代码；仍包含 Windows 专用宏和系统库 |
| [安装器](../hosts/webview2/setup/) | 安装、更新、组件重载和卸载 |
| [双窗状态](../hosts/webview2/src/dual_transition.h) | 单次外框调整、创建门禁和取消处理 |
| [快捷键状态](../hosts/webview2/src/character_shortcuts.h) 与 [页面探测](../hosts/webview2/src/character_shortcuts.js) | 输入队列、动作状态与目标定位 |
| [资料归档](../hosts/webview2/src/profile_transfer.cpp) 与 [浏览器存储恢复](../hosts/webview2/src/profile_transfer_browser.cpp) | 加密、配置过滤和恢复回读 |
| [网络观察](../hosts/webview2/src/network_capture.cpp) 与 [MCP](../hosts/webview2/src/capture_mcp.cpp) | 网络记录、内存留存与本机读取接口 |
| [宿主检查](../hosts/webview2/checks/) | 功能与生命周期的隔离检查 |
| [WebView SDK](../sdk/webview-hotkeys/README.md) | 独立 DLL、接入示例和自己的构建流程 |

## 从源码开始

需要 Windows x64、VS2022 C++ 桌面工具链及 Windows SDK、CMake、Roslyn/.NET Framework 4.8 编译引用、Python 3.11+ 和 Node.js。完整环境说明见 [README](../README.md)。在仓库根目录执行：

```powershell
python -B tools/fetch_dependencies.py
cd hosts/webview2
python -B build.py build
python -B build.py test
python -B build.py verify
```

使用宿主脚本，保留输入指纹和 receipt 检查。构建期间不要修改其输入文件；修改源码后先重新构建，再 verify。构建、隔离测试和文件身份校验分别回答不同问题，不能替代真实浏览器验收。SDK 按其 README 单独构建。

打包与发布属于额外操作。先取得明确授权，再查看 [打包脚本](../tools/package_release.py)、[隐私说明](../PRIVACY.md) 和 [第三方许可](../THIRD_PARTY_NOTICES.md)。不要把推送分支或标签当作无副作用的备份操作，仓库工作流可能触发构建或发布。

## 已经做过的取舍

双窗采用一次拓宽或收窄，随后加载或卸载副窗。早期外框动画曾引起画面畸变，接手时不要默认恢复动画。退出需要区分可放弃的页面回调和必须等待的控制器创建、磁盘任务，不用强杀掩盖生命周期问题。

快捷键排查要依次确认按键接收、队列、页面状态、输入投递和游戏结果。页面可能残留相互冲突的显示类，外层容器尺寸也不一定代表实际目标可见范围。SDK 的输入接入与原生宿主不同，需要独立验收。

资料恢复曾遇到扩展清空存储后异步写回默认值，与恢复内容竞争。当前扩展恢复写入归档键并严格回读，不应机械套用网页 localStorage 的清空方式。错误按失败阶段调查，不把所有恢复错误归因于密码。当前导入不恢复 Cookie。

稳定细节请回到 [界面需求](../docs/UI_REQUIREMENTS.md) 与 [资料迁移说明](../docs/PROFILE_TRANSFER.md)。新反馈只验证相关场景，不重复登录或已通过的游戏流程来补充记录。
