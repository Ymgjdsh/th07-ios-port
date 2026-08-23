# TH07 iOS 14 构建说明

这是独立的 TH07 移植工程，不会修改 TH06。上游基线为 `some100/th07` 的
`reallyportable` 分支，提交 `50c20024783445f4044b4faec6d7f31bb830d78e`。

## Mac 要求

- macOS 12
- Xcode 14.0
- CMake 3.20 或更高
- Python 3（执行 `python3 --version` 检查）
- 至少 6 GB 可用空间

## 构建

在终端进入本目录后执行：

```sh
chmod +x ios/*.sh ios/*.py
./ios/build_ios.sh
```

### Windows 一键远程构建

先复制 `ios/mac_build.local.psd1.example` 为 `ios/mac_build.local.psd1`，并在本机私有
配置中填写 Mac 地址和用户名。该文件已被 Git 和源码打包脚本排除，不会上传 GitHub。
然后在 Mac 的“系统设置 -> 通用 -> 共享”中开启“远程登录”，以后在 Windows
PowerShell 中只需执行同一条命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\ios\build_on_mac.ps1
```

首次执行会自动创建项目专用 SSH 密钥，并要求输入一次 Mac 登录密码；随后会直接继续
上传和构建。以后的构建会免密码登录，而且三个大型资源文件只有发生变化时才会重新上传。

IPA 下载并通过校验后，脚本会运行 `tools/publish_github.ps1`：排除游戏资源、构建产物、
本机配置和签名文件，扫描暂存源码中的密钥/令牌特征与超大文件，然后提交到独立的
`backup` 远程并创建 `ios-v<版本>-build<构建号>` 标签。任何安全检查失败都会停止提交和推送。

需要恢复时执行 `git clone --recurse-submodules https://github.com/Ymgjdsh/th07-ios-port.git`。
仓库只保存可公开的项目源码和第三方依赖引用；`assets` 游戏数据、IPA、构建日志、签名证书、
SSH 密钥和 `mac_build.local.psd1` 必须从你自己的备份恢复，或在新电脑上重新配置。

脚本会打包并上传源码，只在 SHA-256 变化时重新上传三个大型资源，在 Mac 上运行
源码预检和 Xcode 构建，再把 IPA 下载到：

```text
dist/mac-build/th07-ios-0.4.0-29.ipa
```

远程构建失败时，日志会自动尝试下载到 `dist/mac-build/logs-build29`。

如果 Mac 上安装了多个 Xcode，显式指定 Xcode 14：

```sh
XCODE_APP=/Applications/Xcode.app ./ios/build_ios.sh
# 或：DEVELOPER_DIR=/Applications/Xcode-14.0.app/Contents/Developer ./ios/build_ios.sh
```

打包源码（包含 `assets`、iOS 脚本和全部修复，不包含临时构建目录）：

```sh
python3 ios/package_source.py --root . --output ../th07-ios14-port-build29-source.zip
shasum -a 256 ../th07-ios14-port-build29-source.zip
```

成功后的文件（Build 29）：

```text
build-ios/th07-ios-0.4.0-29.ipa
```

构建失败时请发送：

```text
build-ios/logs/build-ios.log
build-ios/logs/last-120-lines.txt
```

## Build 29 Online 联机与标题菜单

- Online 已成为原标题菜单第九项，位于 Quit 下方并跟随原菜单光标/触摸，不再使用左上角悬浮按钮。
- 点击 Online 后使用 iOS UIKit 系统 Sheet，不再绘制 OpenGL 矩形面板；提供附近局域网、直接 IPv4/域名、中转房间和蓝牙附近设备四种方式，Direct 固定使用 UDP 37707 端口。
- 局域网搜索会先等待 iOS 本地网络权限，然后同时使用 Bonjour、全局广播和各网络接口的定向广播。
- 握手采用 `HELLO/WELCOME/ACK` 重试；双方协商同一个会话号，修复“主机显示客机、客机仍在搜索”。
- 联机协议为 v9，并校验构建身份和资源身份。两台设备必须安装由同一份源码和同一套
  `th07.dat`、`thbgm.dat`、`msgothic.ttc` 生成的同一个 IPA；版本不一致时会明确提示并自动回到局域网搜索，
  不会把不兼容的资源强行接入游戏。
- 蓝牙附近设备使用 iOS MultipeerConnectivity；直接地址、中转设置、输入延迟、状态及 RTT 都在系统原生控件中操作和显示。
- 启动协议依次确认准备、难度菜单、最终机体配置和游戏提交；任一阶段超时都会终止锁步并显示错误，不再无限白屏等待。
- 标题画面不再因 900 帧无输入自动播放 Demo Replay，长时间设置联机参数不会被打断。

- 网络输入按逻辑帧锁步交换，并带累计 ACK、选择 ACK、历史补发、乱序去重和周期状态校验。
- 联机协议 v9 为每次重开分配独立局次编号；旧局的握手包和输入包不能进入第二局的帧历史。
- 输入延迟的中性前缀在主机和客机两侧同时初始化，避免第 0 帧等待远端输入超时；附近设备短暂断链时先进入重连宽限，不立即结束锁步。
- Build 29 已加入两人锁步输入、独立 P1/P2 玩家实体、角色与机体 ANM/SHT、
  敌人/子弹/激光碰撞、道具拾取和独立生命/炸弹/Power 旁路。
- 共享暂停、续命、重开和退出使用两阶段 P1/P2 投票；退出/重开会先进入 Yes/No 确认，
  只有双方选择相同结果才关闭菜单。菜单、加载和战斗之间的多点触摸脉冲会被场景戳隔离，
  不会把菜单双指手势带入战斗暂停。
- 续命提交后会在同一逻辑帧重置两名玩家和资源，并丢弃旧的死亡权威状态，避免只有 P1 复活。
- LAN/Direct/Relay 默认输入缓冲提升到 5 帧，UDP 输入补发按批次限流并扩大系统收发队列，避免短暂 Wi-Fi 抖动形成重复包洪峰。
- 主机在菜单或游戏屏障提交后会持续补发 COMMIT，且会响应客机上一 epoch 的 READY 重传；即使客机加载较慢或提交包丢失，也不会在角色选择界面进入 12 秒输入超时。
- 主机权威快照只校正生命、Bomb、Power、死亡/复活和激活等离散状态；普通移动完全由锁步输入推进，不再每帧用量化坐标覆盖客机位置和重置插值，从根源上避免“60 FPS 但画面一顿一闪”。
- 每次共享暂停都会重置原生暂停菜单的旧关闭动画，并隔离打开暂停的 MENU 边沿，点击暂停后不会自动返回战斗。
- 真正的 ARM64 IPA 仍需在 macOS + Xcode 14 上构建，Windows 端可执行源码预检但不能链接 iOS SDK。

## Build 18 controller and dialogue input fixes

- iOS 手柄长按射击不再自动附加 Shift/低速；Focus 仍由映射的实体按键独立控制。
- 进入剧情时自动关闭开发者、设置及布局编辑面板，避免覆盖层拦截剧情操作。
- 进入剧情时释放 Z TOGGLE 和 S TOGGLE；每次剧情轻触会写入“一帧释放 Z + 一帧按下 Z”，即使手柄仍按住射击也能可靠推进，并保持 Replay 输入一致。
- 日志会记录 `mobile/dialogue` 的进入、排队、Z 释放帧及 Z 按下帧，便于确认完整输入过程。

## Build 17 full-screen dialogue touch, touch replay, audio and Extra unlock

- During boss/stage dialogue, a short tap anywhere on screen emits one Z/Shoot press and advances
  exactly one step. A hold of 500 ms or longer keeps the existing Skip behavior without also
  advancing again on release.
- Dialogue touch is handled before the virtual-control layer, so it works with `NO BUTTON` and when
  the tap lands where a hidden or visible action button would normally be.
- The resulting Z/Skip input uses the ordinary input path and is included in Replay recording.

- No-continue touch runs can now be saved from the normal Replay result flow.
- Virtual buttons are stored as ordinary game inputs. Exact post-clamp drag X/Y movement is stored
  in extension records and injected before Player update during playback.
- Build 16 remains compatible with existing keyboard/gamepad replays. New touch replays require
  Build 16 or newer because older builds do not understand the movement extension. Recording emits
  `replay/touch` diagnostics, including an explicit guard for the fixed stage input buffer.

- Restores speaker, wired-headset and Bluetooth output by using a valid iOS Playback session.
- If the custom CoreAudio context fails, retries with miniaudio's default context and logs both results.
- Extra unlocks globally after one no-continue clear on Easy, Normal, Hard or Lunatic with any shot type.
- Existing saves are recognized from either their CLRD record or no-continue clear counter.

- iOS now uses a playback-only AVAudioSession. It no longer selects PlayAndRecord or forces the
  speaker, so wired headphones, Bluetooth A2DP and AirPlay remain valid output routes.
- Audio route changes, interruptions and foreground resume reactivate the session and restart a
  stopped miniaudio device. `th07-mobile.log` records the selected output port and recovery result.
- `CONTROL -> AUTO BOMB` is persistent and defaults to OFF. When enabled, a lethal bullet or laser
  collision consumes one remaining Bomb through the original Bomb state machine before life loss.
  The Bomb input is stored in replay data; replay playback never applies the local Auto Bomb switch.
- Physical keyboard input remains available together with touch controls. SDL gamepads support
  analog stick, D-pad, face buttons and Start-to-pause; another connected pad is selected after the
  active pad disconnects.

## Build 13 mobile controls

- Replaced the mistaken `X TOGGLE` with `S TOGGLE`. It latches the focus/slow-movement key.
- X/Bomb is always momentary again. Existing v4 X-toggle state is deliberately discarded during
  migration, so the new S toggle starts OFF instead of unexpectedly locking focus after an update.

- Portrait mode now follows the TH06 mobile layout: the HUD uses a compact 22% top band and the
  playfield stretches across the full width and all remaining height. No presentation black bars remain.
- Touch coordinates are mapped through the same stretched playfield rectangle, so dragging and menu
  overlays stay aligned with the displayed game.

- Character taps confirm the currently displayed Reimu, Marisa or Sakuya instead of mapping the
  screen into three incorrect columns. Difficulty and shot-type entries support direct vertical taps.
- `PERF` contains a persistent `DEVELOPER MODE` switch. Turning it off hides the in-game `DEV` tool.
- Tablet portrait mode anchors the aspect-safe playfield to the bottom and compresses the HUD into
  the remaining top area, removing the unused bottom strip while keeping gameplay coordinates intact.

- `LOW EFFECTS` now suppresses ordinary particles, bullet spawn/despawn rings, enemy and boss
  auxiliary casting animations, enemy trails, spell-card background VMs and the spell-card start flash.
- Enemy bodies, bullets, lasers, items, collision, scripts, timers and the HUD remain enabled, so the
  performance option does not alter the playable attack pattern.

- Fixed the settings overlay GPU buffer upload that could trigger iOS `IOAF code 11`.
- The title settings button uses the short ASCII label `CFG`.
- Character selection uses a shorter swipe threshold and advances at most once per gesture.
- Two-finger back is handled by title submenus, settings, pause and retry overlays.
- Pause, retry and confirmation entries can be selected and confirmed by tapping their text.
- Portrait mode preserves the native aspect ratio of both HUD panes and the 384x448 playfield on iPhone and iPad; extra tall-screen space is reserved for touch controls.

- `Z`：射击；`X`：Bomb；`S`：低速；`II`：暂停。
- `CONTROL` 菜单的 `Z TOGGLE`、`S TOGGLE` 分别控制射击和低速键锁定，默认均为 OFF。
  打开后每点一次切换持续按住或释放，按钮会保持按下视觉状态。
- `EDIT LAYOUT` 进入布局编辑。直接拖动摇杆和 Z/X/S/暂停键，`SAVE` 保存，
  `RESET` 恢复当前方向默认布局，`CANCEL` 放弃。横屏、竖屏位置分别保存。
- 纯拖拽模式仍显示 Z/X/S/暂停键，只隐藏摇杆；拖拽采用当帧 1:1 相对位移。
- 角色选择页可左右滑动，菜单项目可直接点击并在同一帧确认。
- 键盘和 SDL3 物理手柄可以与触摸同时使用。

## 性能调整

- 移动 UI 顶点先在 CPU 端组装，每帧只上传一次；同类型三角形合并提交。
- 不显示移动 UI 时完全跳过 UI 缓冲更新和 OpenGL 状态切换。
- 详细 `perf/frame` 采集只在 `PERF -> SHOW FPS` 开启时运行，正常游玩不会在
  每个逻辑更新和渲染阶段调用额外高精度计时。
- BGM 静音状态只在变化时提交给音频引擎。
- iOS 使用 640x480 原生游戏缓冲，不创建 Retina 游戏 framebuffer。

如果仍有帧率异常，开启 `SHOW FPS` 后游玩 15 秒，再从文件共享导出
`th07-mobile.log`。日志中的 `calc`、`draw`、`present`、draw call、上传量、弹幕数和
特效数可用于区分 CPU、GPU、垂直同步或场景复杂度问题；完成采样后关闭 `SHOW FPS`
可恢复最低诊断开销。

## 已知限制

- Windows 可以完成 C++17 源码编译检查，但 iOS ARM64 最终链接必须由 Xcode 完成。
- 这是测试版本；六关、Extra、Replay、结算流程仍需要真机回归验证。
