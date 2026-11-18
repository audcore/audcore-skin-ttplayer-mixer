# 声核TTPlayer 三窗口调音台

TTPlayer 风格的主控制、通道列表和效果器三个窗口，支持窗口拖动、吸附与组合移动。

当前源码版本：**0.1.13**。原生 Windows 界面，适用于 Windows x64 声核。

![布局预览](assets/preview.png)

## 界面与操作

- 主窗口控制所选通道的音量与静音，并显示实时左右电平。
- 通道列表用于选择通道，效果器窗口用于旁通、调参和打开编辑器。
- 三个窗口可单独拖动，也可贴合后组合移动。

- 界面语言：中文、英文、日文。

## 安装

[先下载声核](https://audcore.utae.cn/download/)

在支持原生皮肤的声核中打开构建生成的 `.skin.audcore` 文件，再到皮肤设置选择「声声静听」。

所需原生接口为 ABI 0.2，皮肤在宿主进程内运行。

公开源码不代表已经更新官方在线版本，下载仍以官方签名列表为准。

## 从源码生成安装包

构建工具：

- MSVC 编译器 **19.44.35222**（VC Tools 14.44.35207）。
- Windows SDK **10.0.26100.0**。
- Python **3.14.2**。

```powershell
git clone https://github.com/audcore/audcore-skin-ttplayer-mixer.git
cd audcore-skin-ttplayer-mixer
powershell -NoProfile -File build.ps1
```

中间产物为 `build/skin.dll`。最终安装包：

`dist/cn.audcore.ttplayer-v0.1.13.skin.audcore`

原生安装包本质是 DLL，不是独立运行的播放器或调音程序。构建串行执行，使用 BelowNormal 优先级和资源限制。

## 开发与反馈

- [声核官网](https://audcore.utae.cn/)
- [声核使用指北](https://audcore.utae.cn/guide/)
- [skin-dev：SDK、接口资料与模拟开发工具](https://audcore.utae.cn/skin-dev/)
- [皮肤开发指引](https://github.com/audcore/audcore-document/blob/main/docs/皮肤开发指引.md)
- [manifest 与构建发布规范](https://github.com/audcore/audcore-document/blob/main/docs/皮肤发布规范.md)
- [所有皮肤的源码列表](https://github.com/audcore/audcore-document/blob/main/skins.md)
- [此皮肤的问题反馈](https://github.com/audcore/audcore-skin-ttplayer-mixer/issues)
- [声核本体的问题反馈](https://github.com/audcore/audcore-document/issues)

修改名称、版本、语言或构建规则时编辑根目录 manifest.json；界面源码和资源按清单维护。每款皮肤只在自己的仓库更新，构建输入无变化时跳过。

## TTPlayer 参考来源

参考项目为 Sonic853 的 TTPlayer 社区复刻版，固定使用以下提交：

[Sonic853/TTPlayer · 6c05b21](https://github.com/Sonic853/TTPlayer/tree/6c05b21d698dd50515e89cfaf722cd9e223a6e99)

本皮肤将经典播放器的三个窗口用于声核的主控制、通道列表和效果器操作。它不是音乐播放器，音频处理仍由声核完成。

### 移植与适配范围

- `window_drag.cpp/.h`：移植窗口拖动、贴合与组合移动的 Win32 几何实现。
- `button_state.hpp`：提取自上游 `include/ttplayer/skin/skin_button.h` 的按钮状态处理。
- `classic_art.hpp`：适配上游 `player_window.cpp` 的帧绘制方式。
- 窗口布局另参考 `player_window_equalizer.cpp`、`player_window_visual.cpp` 和 `player_window_playlist.cpp`。
- 窗口与皮肤加载另参考 `player_window_internal.h`、`src/skin/skin.cpp` 和 `src/skin/skin_image.cpp`。

### 版权与许可证

Sonic853 的 MIT 版权与许可声明保留在源码中，并随 DLL 的 PE 资源交付。

- [本仓库保留的 Sonic853 MIT 许可](skin-contract/cpp/ttplayer/LICENSE-Sonic853.txt)
- [固定提交中的上游 LICENSE](https://github.com/Sonic853/TTPlayer/blob/6c05b21d698dd50515e89cfaf722cd9e223a6e99/LICENSE)

### 原版美术资源

仓库不分发原版播放器的背景、按钮或程序图标。基础皮肤使用自绘控件和经典布局。

可选导入工具需要用户自行提供 5.7.9 的 `ttpres.dll` 或对应 `DEFAULT_SKIN_579.skn`，并核验资源 SHA-256；图标还需用户自备原版 `TTPlayer.exe`。

[上游的 5.7.9 资源来源、加载方式与哈希说明](https://github.com/Sonic853/TTPlayer/blob/6c05b21d698dd50515e89cfaf722cd9e223a6e99/docs/SKIN_PROFILE_LOADING.md)

从仓库根目录执行本机导入，例如：

```powershell
python skin-contract/cpp/ttplayer/import-classic.py "D:\TTPlayer\ttpres.dll" "dist/cn.audcore.ttplayer-v0.1.13.skin.audcore" "build/声声静听-v0.1.13-本机素材.skin.audcore"
```

感谢 Sonic853 公开窗口和皮肤实现，让这些经典交互能够用于新的调音界面。

## 许可证与验证范围

自有代码采用 [MIT 许可证](LICENSE)。系统字体只按名称引用，不随包分发。

- JSON 解析库保留 [nlohmann/json 的原始许可](tools/mixer-console/vendor/LICENSE.json)。
- 已使用上述工具链独立编译，并通过最终 PE 产物检查。
- 本轮仓库改名与说明整理没有重新编译或替换在线皮肤包。
- 未重新启动宿主进行窗口、鼠标交互或跨屏验收。
