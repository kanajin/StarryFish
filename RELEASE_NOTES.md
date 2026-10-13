StarryFish 首个公开版本：Windows x64 GBF 专用轻量浏览器。

这是完全通过 vibe coding 开发的项目。首版提供两套布局、浅/深主题、多资料、多标签、单双游戏窗口、一次性网址同步、太郎/肉鸽停靠与拆窗、书签及汉化。

下载 `StarryFish-0.1.0-windows-x64-setup.zip`，完整解压后运行 `StarryFishSetup.exe`。需要 .NET Framework 4.8 与稳定版 WebView2 Runtime 154+；安装时联网从上游获取太郎、肉鸽。用户需自行提供并启动代理，手动完成 Mobage 登录。

包未做商业代码签名。`SHA256SUMS.txt` 提供完整性校验；ZeroOmega 对应源码随独立 ZIP 提供，第三方许可保留在安装包内。

首发使用独立 StarryFish 目录，不自动迁移 Gbrowser 开发资料。用户资料、登录态、日志及测试目录均未打包。没有游戏自动操作、通用扩展商店或自动更新器。

构建和自动检查通过不代表所有电脑/运行库版本均已实测。遇问题请提交不含账号、Cookie、密码或密钥的复现信息。
