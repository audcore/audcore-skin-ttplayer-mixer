# 声声静听 v0.1.13

在支持原生皮肤 ABI 0.2 的 x64 声核中打开 `声声静听-v0.1.13.skin.audcore` 安装并选用。三个窗口在 100% Windows 显示比例下分别为 327 × 141、327 × 99 和 327 × 188 像素。

- 每次打开主窗或从最小化恢复时优先选中可见的 Mic 或麦克风通道，也识别未命名的 🎙️ 通道。主窗显示所选通道的名称和无底框的实时左右声道电平，长名称会自动横向滚动，音量与静音作用于该通道。播放与停止继续控制宿主传输。
- 原播放列表同时显示八行，按固定列显示通道名、精简输入名（未接入时留白）、增益（dB）、效果器数量（FX）和状态。M、S、R 分别表示静音、独奏和录音。点击行或按上下键选中通道，右键行打开该通道菜单。通道列表按钮或 F3 切换窗口显示，F4 打开并聚焦列表。
- 原均衡器显示所选通道的效果器。效果器按钮或 F2 切换窗口显示。顶部旁通切换整条效果链，删除作用于选中的效果器，新建打开效果器选择。点击行首圆点切换单个效果器旁通，双击名称打开编辑器，在数值上滚动可调整主参数或干湿比。行末 ⋯ 或右键打开效果器菜单。
- 拖动主窗的空白区域，已贴合的面板一起移动。拖动辅助窗的标题区域可单独移动，靠近其他窗或屏幕工作区边缘时自动吸附。
- 播放窗口的打开按钮打开声核工程，F10 或右键主窗空白区打开共用的原生主菜单。原音量条可拖动，底部 −60 dB，顶部 0 dB。前后曲仍显示停用状态。
- 三个窗口默认 5% 透明。Shift + 滚轮或 F6 / Shift + F6 以 5% 步进调整，范围 0–90%。设置作用于当前皮肤会话。

## 本机导入 5.7.9 原版位图

基础单文件使用自绘控件和经典布局。原版背景、按钮与 LED 位图来自 TT.experience 的原始资源。本机导入时把它们写入新皮肤文件的 PE 资源段。两个版本使用同一皮肤 ID，安装导入后的单文件会更新已安装的基础版。

```powershell
python skin-contract/cpp/ttplayer/import-classic.py "D:\TTPlayer\ttpres.dll" "build\ttplayer-skin\声声静听-v0.1.13.skin.audcore" "build\ttplayer-skin\声声静听-v0.1.13-本机原版素材.skin.audcore"
```

也可提供相同的 `DEFAULT_SKIN_579.skn`。脚本只读取原文件数据并核对默认皮肤 ZIP 的 SHA-256，生成的新单文件可直接安装。所导入的背景和四态帧在 100% DPI 使用原始像素和色键。通道、电平、效果器内容和宿主状态文字由声核重绘，原播放器的歌曲信息和进度条未复刻。原版程序图标显示在主窗左上角，并设为主窗的大小窗口图标。导入脚本会读取素材同目录的 5.7.9 `TTPlayer.exe`，也可用 `--icon-source <路径>` 指定。图标取自原版程序资源组 128，核验整组 SHA-256 后嵌入本机单文件。

## 源码与验证

固定上游为 [Sonic853/TTPlayer 6c05b21d698dd50515e89cfaf722cd9e223a6e99](https://github.com/Sonic853/TTPlayer/tree/6c05b21d698dd50515e89cfaf722cd9e223a6e99)。`window_drag.cpp/.h` 移植自同名 Win32 几何实现，`button_state.hpp` 提取自 `include/ttplayer/skin/skin_button.h`，`classic_art.hpp` 适配 `player_window.cpp` 的帧绘制。参考了 `player_window_equalizer.cpp`、`player_window_visual.cpp`、`player_window_playlist.cpp`、`player_window_internal.h`、`src/skin/skin.cpp` 及 `src/skin/skin_image.cpp`。完整 Sonic853 MIT 许可嵌入皮肤 PE 资源。原版美术资源来自上游文档标明的 `ttpres.dll` → `ZIP/<DEFAULT_SKIN>`，只供本机导入。 [上游许可](https://github.com/Sonic853/TTPlayer/blob/6c05b21d698dd50515e89cfaf722cd9e223a6e99/LICENSE) · [5.7.9 资源来源与哈希](https://github.com/Sonic853/TTPlayer/blob/6c05b21d698dd50515e89cfaf722cd9e223a6e99/docs/SKIN_PROFILE_LOADING.md)

```powershell
python skin-contract/cpp/ttplayer/build.py --tests
build/ttplayer-skin/ttplayer-test.exe <基础单文件的绝对路径> <本机导入单文件的绝对路径>
```

构建持有全机直播编译锁，编译与链接串行，使用 BelowNormal 优先级及 1.5 GiB Job 提交内存硬上限。无窗口测试检查 ABI 重载、订阅释放、命令与状态、组合移动、拆分、吸附阈值和 DPI 几何。测试用实际 DLL 入口加载位图，再逐像素比较离屏渲染和原版 BMP，覆盖原色及色键。实际鼠标拖动、跨屏和声核内安装交互尚未实测。
