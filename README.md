# SerialMate

**简体中文** | [English](README.en.md)

用 Qt 6 写的串口调试助手，操作习惯对齐 SSCOM 5.13.1，并补上了原版缺的一些能力。
**独立实现**，与 SSCOM 及其作者无关。

![主界面](images/screenshot.png)

**绿色软件**：配置写在 exe 同目录的 `SerialMate.ini`，不写注册表、不碰 `%APPDATA%`。
整个目录拷到别的电脑上双击就能用。

> 接替已归档的 [OhMySerialHelper](https://github.com/RoninQiu/OhMySerialHelper)
> —— 2026-09-29 停更，末版 v1.2.0。

## 特性

- **串口** —— 枚举 / 刷新 / 开关，波特率·数据位·校验位·停止位·流控，DTR / RTS。
  串口对象跑在独立线程，打开后参数控件自动禁用
- **接收区** —— ASCII 与 HEX 切换、时间戳、自动换行、暂停、缓冲上限（200K~20M）、
  右键菜单、字体设置。暂停时继续接收但不刷新画面
- **发送区** —— 文本 / HEX / 追加 CRLF / 定时发送，三档编码（UTF-8 / 系统 ANSI / Latin-1）
- **多字符串发送** —— 99 条，每条独立「间隔」，支持 HEX/文本、单条发送、循环发送。
  可选的 SSCOM 配置导入，一键迁移既有 `sscom51.ini`
- **收发方向着色** —— 接收行**蓝色**、发送行**橙色**，跟随系统深浅色各有一套配色；
  勾了时间戳时箭头排在时间戳之前。SSCOM 没有这个功能，打开回显后收和发在文本里
  长得一模一样。刻意避开红绿：约 8% 男性分不出那一对，且红绿会误示"失败/成功"。
- **绿色交付** —— `package.bat` 一键产出可运行目录与 zip 归档

## 环境要求

| 组件   | 版本                           | 说明                                                  |
| ------ | ------------------------------ | ----------------------------------------------------- |
| Qt     | ≥ 6.5                          | 需包含 **Widgets**、**SerialPort**、**Test** 三个模块 |
| CMake  | ≥ 3.21                         | Qt 在线安装器可一并勾选                               |
| 编译器 | GCC 9+ / Clang 9+ / MSVC 2019+ | 需与 Qt 的 kit 一致                                   |

已实测：**Qt 6.12.0 mingw_64 + MinGW 13.1.0 + CMake 3.30.5 + Ninja**。
MSVC 用户无需额外处理，`CMakeLists.txt` 已带 `/utf-8`。

## 构建

不写死任何路径。`qt-env.bat` 依次尝试：环境变量 `QTDIR` → 从 `PATH` 上的
`windeployqt` / `qmake` 反推 → 若 `QTDIR` 是安装根目录则下钻到第一个套件 →
安装器默认的 `C:\Qt`。`cmake` / `ninja` / `gcc` 若已在 `PATH` 上则完全不干预。

```bat
setx QTDIR "C:\Qt\6.12.0\mingw_64"   REM 一次性设定

build.bat          :: 配置 + 编译 + 启动程序
build.bat test     :: 配置 + 编译 + 跑单元测试（不启动 GUI）
```

单次生效也行：`set QTDIR=C:\Qt\6.12.0\mingw_64 && build.bat test`。

## 打包

```bat
package.bat
```

产物是 `dist\SerialMate\` 这一个目录（**11 个文件 / 34.9 MB**），以及同名的
`dist\SerialMate-<版本>.zip`。版本号从 `CMakeLists.txt` 的 `project(VERSION)`
读，不手抄。zip 用 Windows 自带的 `tar.exe` 生成，**不需要预装 7-Zip**，
且归档顶层就是 `SerialMate\`，解包不会把文件倒在下载目录里。

`windeployqt` 默认会多塞约 3.6 MB 用不上的东西（`Qt6Network`、`Qt6Svg` 及四个
插件目录）。脚本在部署时就用 `--no-network --no-svg --skip-plugin-types` 排掉，
而不是事后删文件 —— **这个脚本不删除任何文件**。代价是它不清空 `dist\SerialMate\`：
换 Qt 版本前请手工删掉那个目录。脚本末尾会打印**递归文件数**，两次跑数字对不上
就是有残留 DLL。

发包那份 ini 会剔掉 `[window]` 段的 `state` / `geometry` / `layoutVersion`
——那是会话痕迹（上次窗口多大、面板当时开着没），带上会发出一个**看不见多字符串
面板**的包，那 99 条就白发了。

## 从 SSCOM 迁移

菜单「文件 → 从 SSCOM 导入配置」选一个 `sscom51.ini` 即可。串口参数、
接收选项、发送草稿、99 条多字符串及其**每条的独立间隔**全部迁移。

导入后编码自动切到「系统 ANSI」：SSCOM 的 ini 里存的就是 GBK 字节，沿用本程序
默认的 UTF-8 会让中文全变乱码。

无法原样迁移的项会在导入后列出（点 Show Details 查看），其中要紧的几条：

- **数据位 / 校验位 / 停止位 / 流控** —— SSCOM 压根不持久化这四项，写死 8N1 无流控，
  本程序默认值恰好一致，不受影响
- **接收区时间戳** —— SSCOM 的「加时间戳」与「分包显示」共用一个复选框，
  配置里只有后者，时间戳还原不出来
- **多字符串「使能」** —— 首段语义无法确定，导入后一律未勾选，请自行勾选

## 配置存放位置

**exe 同目录的 `SerialMate.ini`**：

```ini
[serial]     port=COM1  baud=115200  dataBits=8  parity=0
             stopBits=1  flow=0  dtr=true  rts=false
[recv]       hex=false  timestamp=false  wrap=true  showSent=false
             bufferLimit=1000000  encoding=0  font="Consolas,10,-1,5,400,..."
[send]       text=  appendCrlf=false  hexMode=false  intervalMs=1000  encoding=0
[loop]       enabled=false  intervalMs=100
[sendItems]  size=99
1\name=  1\enabled=false  1\hexMode=false  1\intervalMs=1000  1\content=
```

删掉该文件即可恢复默认。

> 不用 `QSettings(org, app)` 是有意的：那个写法在 Windows 上落到注册表，
> 是「装了软件」的形态 —— 用户看不见、删不掉、换机带不走，卸载后还留残留。

## 已知限制

- **未实现**：校验和等辅助工具（FR-09）。「发送文件」（FR-03-6）已废除
- **打包止于「可运行目录 + zip」**：不产安装程序、不做代码签名。
  也不做单文件 exe —— 静态链接 Qt 已评估并放弃
- **保存接收数据是两个弹窗**（文件选择 → 问要不要剥离方向标记）。
  `QFileDialog` 在 Qt 6 没有挂附加控件的接口，而一个点确认后不返回的对话框
  等于「保存卡死」，比多问一句严重得多
- **多字符串的「间隔」是逐条的**，语义为「**这条发完**停多久再发下一条」，
  取值 1..600000ms。底部输入框退化成「应用到全部」
- **缓冲上限按 UTF-16 码元计数**，对 ASCII/二进制数据约等于字节数，中文正文会偏大
- **HEX / 时间戳切换不回溯重渲染**已显示的文本，只影响新到的数据
- **发送区多行输入按 LF 而非 CRLF 发出**
- **波特率非法输入静默回落 9600**，无提示
- **硬件流控下 RTS 自动禁用**（RTS 线被流控逻辑占用，强行设置会被静默忽略）
- **串口跨线程的收发链路已在真机上验证**（COM5 + TX-RX 回环，27 项断言连跑 4 次全绿）；
  **全双工并发与硬件流控未验** —— 短接的那两根线上没有对端

## 分层约束

```
ui/  →  core/  →  models.h
```

`core/` **绝不能**反向依赖 `ui/`。新增代码时用这条命令自查：

```bash
grep -rn '#include "ui/' src/core/     # 必须无输出
```

## 许可

MIT，见 [LICENSE](LICENSE)。

本项目是**独立实现**，未使用 SSCOM 或其作者的任何源代码、反编译产物、图标或资源素材。
仓库中提及 SSCOM 只为两个用途：说明本工具面向的使用场景；提供一个**可选**的
配置导入功能（只解析用户自己机器上的 INI 文本，不含原软件任何素材），免去手工
重录 99 条。

SSCOM 及其名称、图标归其作者所有，与本项目无关。本项目与 SSCOM 作者无任何关联，
也未获其背书。
