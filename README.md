# Jimaku 字幕

把日语视频拖进窗口，自动生成**中日双语字幕**（Windows / macOS）。Qt 6 + [whisper.cpp](https://github.com/ggml-org/whisper.cpp)，语音识别全部在本机完成，不需要 Python。

## 功能

- **拖进视频就开始**：主界面只有一个拖放区；模型、设备都自动选择，不常改的选项都在“设置”里
- **边处理边预览**：拖入后立即可以播放，字幕随识别逐行出现、译文随后补上，播放到哪一段画面上就显示那一段的中日字幕；进度条上标出已识别的范围
- **本地识别**：whisper.cpp 固定按日语识别，macOS 用 Metal（Apple 芯片 GPU）加速，模型只加载一次，处理下一个视频时直接复用
- **长视频**：按 5 分钟一段边解码边识别（在最安静的地方切开），内存只占一段；第一段只有 1 分钟，几秒内就出第一行字幕；
  每段重新开始识别，不会像整段识别那样卡在一句话上反复输出；预览时跳到还没识别的地方，会优先识别那里
- **自动保存进度**：中途退出或崩溃后，再次打开同一个视频从中断处继续；处理完的视频直接打开上次的结果（包括手工修改）；处理期间不让电脑睡眠
- **按词断句**：用词级时间戳重新切分字幕，单行不超过 18 个字、不超过 6 秒；优先在句号、逗号、助词和停顿处换行，不会把一个片假名词从中间断开
- **大模型翻译**：DeepSeek（默认）、OpenAI、本地 Ollama 或任何 OpenAI 兼容服务；分批并发翻译，带前后文、术语表，译文统一为简体中文
  - 被切成几行的一句话先整句翻译，再按中文语序拆回各行
  - 翻译完成后全片校对：统一人名等专有名词、纠正有把握的识别错误；改过的单元格标成浅蓝色，悬停可以看到改动前的内容
  - 字幕风格（句末不加标点、逗号换空格、译文尽量短），英文歌词和口号默认保留原文（设置里可改）
  - 翻译结果按请求缓存：同一个视频重新处理不再重复花钱；完成后显示用了多少 token
- **失败不装成功**：某一行没翻出来会标黄并在顶部提示，可一键补译；API key 错误、没余额、连不上服务时明确报错，识别结果仍然保留
- **编辑与导出**：双语字幕表格直接修改；导出 SRT / ASS（中日双语 / 仅中文 / 仅日文），或用 ffmpeg 把字幕烧录进视频
- API key 只保存在系统钥匙串（macOS 钥匙串 / Windows 凭据管理器），不写进任何配置文件

## 安装

从 [Releases](https://github.com/wangchaozhi/Jimaku/releases) 下载：

- **Windows**：`Jimaku-x.y.z-windows-x64.zip`，解压后运行 `Jimaku.exe`（已附带 ffmpeg）
- **macOS 14+（Apple 芯片）**：`Jimaku-x.y.z-macos-arm64.zip`，解压后把 `Jimaku.app` 拖到“应用程序”；需要先安装 ffmpeg：`brew install ffmpeg`。
  安装包没有苹果开发者签名，第一次打开需要在“系统设置 > 隐私与安全性”里允许

第一次处理视频时会下载识别模型（默认 large-v3-turbo，约 1.6 GB，只下载一次），并提示填写翻译服务的 API key。

## 使用

1. 把视频拖进窗口（或点“选择视频…”，也可以 `Jimaku 视频.mp4` 直接打开）
2. 处理中就可以点播放预览；单击字幕表的一行跳到那里（3 分钟视频在 M4 上约 30 秒处理完）
3. 处理完成后双击修改字幕，点“导出字幕…”或“烧录到视频…”

预览用系统自带的解码器：mp4 / mov / m4v 可以预览，mkv、webm 等格式照常生成字幕，只是不能在窗口里播放。

用本地 Ollama 离线翻译：先 `ollama pull qwen2.5:7b`，在设置里把翻译服务改成“本地 Ollama”。

### 命令行

```bash
Jimaku --cli 视频.mp4 [-o 输出.srt] [--format srt|ass] [--layout both|zh|ja] [--model large-v3-turbo|medium|small]
       [--no-translate] [--no-review] [--no-cache] [--chunk 秒数] [--decode greedy|nofallback|beam]
```

识别 + 断句 + 翻译 + 校对后写出字幕，统计信息（行数、最长一行的秒数、未翻译行数、校对结果、token 用量、各阶段耗时）以 JSON 打到标准输出。
退出码：0 成功；1 失败；2 字幕已生成但有行没有翻译成功。
API key 默认从系统钥匙串读取，也可以用环境变量 `JIMAKU_API_KEY` 临时指定。

`--decode` 用来对比解码策略，默认 greedy（贪心 + 温度回退）。实测三种策略两次运行结果都完全一致；
nofallback 快约 40% 但容易卡在一句话上反复输出，beam 会把开头的旁白、英文歌词并进重复的口号里，所以默认不变。

## 从源码构建

需要 Qt 6.7+、CMake 3.21+、C++17 编译器和 ffmpeg。whisper.cpp 和 qtkeychain 在配置时自动下载并静态编译。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

- macOS（Homebrew 的 Qt）：加 `-DCMAKE_PREFIX_PATH=$(brew --prefix qt)`
- Windows 上用 NVIDIA 显卡加速：加 `-DJIMAKU_CUDA=ON`（需要 CUDA Toolkit）

## 结构

```
src/core/     与界面无关的处理流程（解码、识别、断句、翻译、字幕导出、烧录、设置）
src/ui/       两页主窗口：拖放 → 工作台（视频预览 + 字幕表，边处理边显示），以及设置、模型下载对话框
src/cli.cpp   无界面模式
tests/        断句、字幕格式、翻译回复解析、错误分类、UTF-8 拼接的单元测试
```

## 许可

MIT。识别使用 whisper.cpp（MIT）和 OpenAI Whisper 模型（MIT）；钥匙串使用 qtkeychain（BSD）；界面使用 Qt（LGPLv3）。
Windows 安装包附带的 ffmpeg 为 LGPL 版本，来自 [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds)。
