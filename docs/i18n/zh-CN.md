# Lightshot HDR Fix

[English](../../README.md) · [Português (Brasil)](pt-BR.md) · [Español](es.md) · [Français](fr.md) · [Deutsch](de.md) · [简体中文](zh-CN.md) · [日本語](ja.md)

![保持 HDR 开启，输出 SDR 截图。](../assets/hero.svg)

**保持 HDR 开启，输出 SDR 截图。** 通过 FP16 捕获和 GPU 色调映射，改善 Lightshot 截图发白或过曝的问题，并将 SDR 图像交给熟悉的编辑器。捕获后端不会添加 Windows Graphics Capture 的黄色边框。

[**下载 Windows 安装程序**](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases/latest/download/Lightshot-HDR-Fix-Setup.exe) · [所有版本](https://github.com/Joaovitorsw/lightshot-hdr-fix/releases) · [验证记录](../VALIDATION.md)

## 安装

1. 先安装[受支持的 Lightshot 原版](https://app.prntscr.com/en/)。
2. 下载并运行 **Lightshot-HDR-Fix-Setup.exe**。不需要 Python 或管理员权限。
3. 安装程序会检测兼容版本；如果未找到，请选择版本文件夹。如果 Lightshot 正在运行，关闭前会询问确认。
4. 完成后从开始菜单启动 **Lightshot HDR Fix**。桌面快捷方式和安装结束后启动均为可选项。

安装程序 **0.3.0** 将 Lightshot 复制到当前用户的本地文件夹，**仅修改副本**。原安装保持完整，也不会自动修改 Windows 开机启动。恢复时，关闭修正版并启动原版。一次只运行一个实例：快捷键、首选项和互斥体可能共用。

> 安装程序和附加组件未签名，SmartScreen 可能显示警告。运行前请确认下载来自本仓库的 Releases。

## 兼容性与限制

Windows 10/11，支持 DXGI 1.5 的显卡。需要 x86 Lightshot：安装目录 **5.5.0.7**、可执行文件版本资源 **5.5.0.4**，且文件哈希必须完全匹配。其他版本会被拒绝。**不分发 Lightshot 原版。** 输出为 SDR，显示器 HDR 保持开启。

独占全屏、受保护内容、旋转显示器以及完整的编辑/保存/剪贴板流程仍需进一步测试。捕获失败时，GDI 回退仍可能过曝。光标可能重复显示。

## 受控对比

![受控灰阶色块测试](../assets/gray-patches.svg)

这是受控灰阶色块测试，**并非游戏截图**：原输出 **210 / 255 / 255 / 255**，修正后 **118 / 240 / 250 / 253**。色调曲线有所变化，以保留高光层次；不代表通用的色彩等效性。

在一台 RTX 5080 电脑上（3440×1440 HDR + 1920×1080 SDR），完整核心捕获预热后为 **62–78 毫秒**，首次约 **312 毫秒**。这不是端到端延迟保证。[验证与限制](../VALIDATION.md)。

## 手动 ZIP 与构建

ZIP 安装是高级备用方式，需要 Python 3.10+。[命令、哈希和技术细节请参阅英文 README](../../README.md#manual-zip-installation)。

本项目是独立附加组件，与 Skillbrains 无官方关联，也不是对 Lightshot 闭源核心的重新编译。新编写的代码尚未选择通用开源许可证；第三方组件保留各自的许可证。
