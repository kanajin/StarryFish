# GBF WebView Hotkeys SDK · ABI 1

Windows x64 WebView 宿主接入包，使用 MIT 许可。不是浏览器扩展；不包含 Chrome/Iron 扩展、Native Messaging 程序或注册脚本。StarryFish 保持原生实现，不依赖此 DLL。

第三方宿主需要按 [接入指南](docs/SDK.md) 集成 C ABI、只读页面探测、焦点/导航生命周期和自己的配置界面。需要真实游戏视口子 HWND；尚未验证 Grancypher 或其他第三方浏览器，发布为开发者预览，不能直接侧载到任意浏览器。

附件包含 DLL、导入库、头文件、C++/C# 示例及可构建源码。源码构建：在此目录运行 `python -B build.py build`、`python -B build.py test`；需要 Visual Studio 2022 C++、Windows SDK、Python、Node.js。C# 示例使用 .NET 9 SDK。

快捷键规则来自同一版本 StarryFish；支持角色/技能/药水选择、攻击与取消、FC、奥义、菜单、召唤及刷新。由宿主接收真实按键，只操作前台游戏视图，不调用游戏内部接口。隔离测试不等于第三方宿主实战验收。
