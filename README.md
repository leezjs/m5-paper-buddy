# m5-paper-buddy

<p align="center">
  <a href="README.md"><b>中文</b></a> · <a href="README.en.md">English</a>
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/49a543f3-ad1a-4735-a5d1-ef98867cff1e" alt="m5-paper-buddy on an M5Paper V1.1" width="540">
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg" alt="GPL-3.0"></a>
  <img src="https://img.shields.io/badge/hardware-PaperS3-orange" alt="PaperS3">
  <img src="https://img.shields.io/badge/firmware-ESP32%20%2B%20PlatformIO-brightgreen" alt="ESP32">
  <img src="https://img.shields.io/badge/daemon-Python%203-yellow" alt="Python">
  <img src="https://img.shields.io/badge/integration-Claude%20Code%20Plugin-7F52FF" alt="Claude Code">
  <img src="https://img.shields.io/badge/i18n-EN%20%2F%20中文-lightgrey" alt="i18n">
</p>

<p align="center"><b>把 M5Paper 变成 Claude Code 的物理桌面搭档</b></p>

---

## ✨ 简介

用一块 **PaperS3**（4.7" 电子墨水屏 / 540×960 / 触摸 / ESP32-S3）做你的 Claude Code 伴侣屏。开着多个 Claude Code 窗口的时候，这块墨水屏会**实时镜像**每个 session 的项目、分支、上下文占用、最新回复和活动日志。Claude 要调用工具时，命令 / diff / 内容会直接显示出来，方便你看它在做什么；普通工具执行**不再要求你手工确认**，只有 `AskUserQuestion` 这类明确提问还会保留触屏回答。

---

## 🎛️ 功能

| | |
| --- | --- |
| 📊 **多会话 Dashboard** | 左列显示所有在跑的 Claude Code 窗口，点一下切换 focus；右列显示 model + 上下文窗口占用进度条 |
| 🔐 **操作展示** | `PreToolUse` 会把 Bash 命令 / Edit diff / Write 预览等内容展示到屏幕上，方便你观察 Claude Code 正在做什么；普通工具调用默认直接放行 |
| 💬 **触屏回答** | `AskUserQuestion` 最多 4 个选项做成**大按钮**，点一下选项 label 直接回传给 Claude |
| 🔁 **FIFO 队列** | 多个窗口同时弹出 `AskUserQuestion` 时，一次只显示一个，当前处理完自动切到下一个 |
| 🀄 **中英双语** | 内置 CJK 字体与 UTF-8 文本渲染，UI 支持 EN / 中文 切换；所有 prompt / 回复 / 活动都能显示中文 |
| 🔌 **双 Transport** | USB 串口（默认、零配置）或 BLE（Nordic UART、macOS 配对 passkey），自动选择 |
| ↔️ **横竖版切换** | `Settings` 里可在 **Landscape / Portrait** 之间切换，立即生效并写入 NVS，重启后保持 |
| ⚙️ **设置页** | 右上角点 `SETTINGS` / `设置`，查看 transport、电量、会话数、DND、预算、运行时长、最后消息；底部 `LANG / LAYOUT / CLOSE` 三段触摸条负责切语言、切布局、关闭页面 |
| 🐱 **猫伙伴** | 底部一只 ASCII 猫跟状态变化表情（idle/busy/attention/celebrate/DND/sleep） |
| 🔌 **Claude Code 插件** | 一条 `/buddy-install` 搞定 PlatformIO + Python 依赖 + mklittlefs 架构补丁 + hooks 合并 + 固件/字体刷录 + daemon 后台启动 |

---

## 🛠️ 硬件

- **PaperS3**（4.7" 电子墨水屏、540×960、触摸、ESP32-S3、16MB Flash）
- 一条 USB-C 线（初次烧录必须，之后可以换 BLE）

---

## 🚀 快速开始

**前置**：[PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/)、Homebrew（Apple Silicon 下装 `mklittlefs` 用）、一台 PaperS3。

```bash
# 克隆
git clone https://github.com/leezjs/m5-paper-buddy.git
cd m5-paper-buddy

# 作为 Claude Code 插件安装（推荐）
# 把本仓库 plugin/ 目录加到 Claude Code 的 plugin 路径下，然后：
/buddy-install
```

`/buddy-install` 会自动：

1. 验证 PlatformIO 已装
2. 装 Python 依赖（`pyserial`、BLE 模式额外装 `bleak`）
3. Apple Silicon 下**自动修复** PlatformIO 自带 x86_64 `mklittlefs`（`brew install mklittlefs` + symlink）
4. 把 hook 配置合并进 `~/.claude/settings.json`（自动备份原文件）
5. 如果 Paper 已插 USB，**刷 firmware + 字体**
6. 后台启动 daemon

**手工模式（不走插件）**：

```bash
pio run -e papers3 -t uploadfs          # 刷字体进 LittleFS（~90s）
pio run -e papers3 -t upload            # 刷固件（~30s）
python3 tools/claude_code_bridge.py --budget 200000

# 然后把 plugin/settings/hooks.json 的 hooks 块手动合并到
# ~/.claude/settings.json
```

默认刷机环境现在是 `papers3`。如果你要继续刷旧的 M5Paper 固件，可手动改用 `-e m5paper` 或设置 `BUDDY_PIO_ENV=m5paper`。

---

## 📟 日常使用

装完以后 Claude Code 里有这几个斜杠命令：

| 命令 | 作用 |
| --- | --- |
| `/buddy-install` | 首次安装 / 重新校验环境 |
| `/buddy-start` | 启动 daemon（幂等） |
| `/buddy-stop` | 停止 daemon |
| `/buddy-status` | 看 daemon pid、串口、hooks 安装情况、日志尾部 |
| `/buddy-flash` | 重新编译 + 烧录固件和字体（stop → flash → start） |

状态目录：`~/.claude-buddy/`（pid、log）。

---

## ⌨️ 触控交互

| 页面 / 区域 | 操作 |
| --- | --- |
| Dashboard | 点会话行切换当前 focus；点右上 `SETTINGS` / `设置` 打开设置页 |
| Settings | 优先使用底部三段触摸条：`LANG` 切中英文，`LAYOUT` 切横版 / 竖版，`CLOSE` 关闭设置页 |
| Permission 卡片 | 使用屏幕下方 `A / B` 操作区进行同意 / 拒绝；卡片会显示当前工具、来源 session 和完整内容 |
| Question 卡片 | 直接点大按钮回答 `AskUserQuestion` |
| BLE 配对页 | 屏幕会显示 6 位 passkey，在电脑弹窗里输入即可 |

---

## 🔌 Transport

默认 `BUDDY_TRANSPORT=auto` —— 有 USB 走 USB，没有就 BLE。

```bash
BUDDY_TRANSPORT=ble    /buddy-start
BUDDY_TRANSPORT=serial /buddy-start
```

BLE 首次连接会触发 macOS 系统配对对话框，Paper 屏幕上显示 6 位 passkey，你输进去即可。以后自动重连。

---

## 💰 上下文预算

屏幕上的进度条显示 **当前 focus 的 session 的上下文窗口占用量** ÷ 上限，读取自 session transcript JSONL 里最后一条 assistant 消息的 `usage.input_tokens + output_tokens`。

默认上限 200K（Claude 4.6 标准上下文）。要用 1M 上下文的 4.7 beta：

```bash
BUDDY_BUDGET=1000000 /buddy-start
```

设 0 隐藏进度条。

---

## 🌐 语言切换

默认英文。点 `SETTINGS` 后，优先用底部 `LANG` 触摸条切换；也可以点 **language / 语言** 行本身。选择写入 NVS，重启保留。

## ↔️ 横竖版切换

点 `SETTINGS` 后，使用底部 `LAYOUT` 触摸条在 **Landscape / Portrait** 之间切换。切换会立即触发整页重绘，并写入 NVS，所以下次开机仍然保持上次的方向。

---

## 📂 目录结构

```
src/
  ble_bridge.cpp/h       # Nordic UART Service，双向行缓冲 TX/RX
  stats.h                # NVS 状态（approvals/denials/level/DND/language）
  paper/
    main.cpp             # UI、状态机、触屏、设置页、i18n
    data_paper.h         # TamaState + JSON 协议解析（UTF-8 安全）
    xfer_paper.h         # status 响应、name/owner/unpair 命令
    buddy_frames.h       # ASCII 猫的 6 个状态帧
data/cjk.ttf             # 兼容保留的字体资源；PaperS3 当前优先使用内置 M5GFX CJK 字体
partitions-m5paper.csv   # 3MB app + 13MB LittleFS 分区表
platformio.ini
plugin/                  # Claude Code 插件打包
  plugin.json            # manifest
  commands/              # /buddy-* 斜杠命令
  scripts/               # install / start / stop / status / flash / common
  settings/hooks.json    # 要合并到 ~/.claude/settings.json 的 hooks 块
  README.md              # 插件自身的 README
tools/claude_code_bridge.py   # daemon: HTTP → serial/BLE 桥接
```

---

## 📖 深入阅读

- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** —— 技术架构 / 通信协议 / daemon 与固件内部 / 踩过的坑
- **[docs/PRODUCT.md](docs/PRODUCT.md)** —— 产品思考 / 设计取舍 / 未来畅想 / 给想 fork 的人

---

## 🧩 开发

固件改完后：

```bash
pio run -e papers3              # 只 build
pio run -e papers3 -t upload    # 烧固件
pio run -e papers3 -t uploadfs  # 更新 LittleFS（字体变了才需要）
```

### Multi-firmware groundwork

仓库现在新增了两个 PaperS3 构建目标：

- `papers3_buddy` — 使用新 launcher/runtime 分区图的 buddy runtime
- `papers3_launcher` — 用来验证新 flash 布局的最小 launcher stub

迁移期间，原有的 `papers3` 目标仍然保留。

daemon 改完直接重启：

```bash
/buddy-stop && /buddy-start
# 或：
plugin/scripts/stop.sh && plugin/scripts/start.sh
```

看日志：`tail -f ~/.claude-buddy/daemon.log`

---

## 🙏 致谢

本项目参考了 Anthropic 的 [`claude-desktop-buddy`](https://github.com/anthropics/claude-desktop-buddy) —— Nordic UART Service + heartbeat-JSON 通信协议沿用它的形状，因此理论上这块 Paper 也能被原项目的桌面端 bridge 驱动。

内置字体是 GenSenRounded Regular，来自 M5Stack 的 `M5EPD` 库示例。

---

## 📜 协议

本项目使用 **[GPL-3.0](LICENSE)** 协议，额外附加**署名要求**：

> **任何 fork / 修改 / 再分发，都必须：**
>
> 1. 保留 `Copyright © 2026 op7418` 版权声明
> 2. 在 README 或 About 里**显眼地署名** "op7418 / m5-paper-buddy"
> 3. 衍生作品自身也必须 **以 GPL-3.0 或更高版本开源**，并**公开完整源代码**

换句话说：你可以自由 fork / 改 / 用在商业场景里，但改完的版本也必须开源 + 署名。合了就必须开源，不接受闭源衍生。

详见 [LICENSE](LICENSE) 文件里的 "Attribution & derivative obligations" 段落。

<details>
<summary>第三方组件</summary>

- `data/cjk.ttf`：GenSenRounded Regular，来自 M5EPD 库示例，字体本身的许可条款适用于该文件
- Nordic UART Service UUID 与 heartbeat JSON schema：参考自 anthropics/claude-desktop-buddy（MIT）

</details>
