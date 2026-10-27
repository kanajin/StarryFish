# 浏览器平台无关模型

`shell_model` 抽取 Windows `ui_theme.h`、`icon_tabs.h`、`compact_chrome.h` 的蓝色配色、图标标签重叠顺序和双布局几何，使用逻辑像素及标准 C++ 类型。Linux 通过独立 CMake target 接入，不链接 Win32 的 app/ 或 Windows 宿主目标。

`DocumentEpoch` 为异步回调提供资料、视图、主 frame、导航代次绑定；可信来源必须由宿主从实际 frame 解析，不能使用网页消息自报的来源。它不提供网页特权 API。Linux 当前仅用它阻止过期 favicon 回调；完整消息桥另行实施。

Windows 现有接线保持独立；在 Windows build/test/verify 可执行前，不宣称 Windows 已迁入该模型。Linux 收据同时纳入本目录的源码身份。
