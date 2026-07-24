# SXR EGO 头环数据采集系统 1.0.0 使用说明

本版本面向头环数据采集使用。系统负责把头环上的相机、IMU、音频、头部位姿、手势和手柄数据保存为一轮完整采集 session，并为不同数据提供统一的时间戳，方便后续进行图像与传感器数据对齐。

## 一、支持的数采功能

### 1. 采集的数据

- RGB 双目相机数据。
- Tracking 双目灰度相机数据。
- Ctrl 双目灰度相机数据。
- 加速度计和陀螺仪数据。
- 麦克风音频数据。
- 头部 6DoF 位姿数据。
- 手势模式下的左右手关节数据。
- 手柄模式下的左右手柄位姿数据。
- 相机内参、外参和 IMU 标定信息。

手势模式和手柄模式是两种采集模式，采集时二选一：

```bash
# 手势模式：保存 hand_tracking.csv
adb shell setprop persist.xr.usecontroller false

# 手柄模式：保存 controller_poses.csv
adb shell setprop persist.xr.usecontroller true
```

切换模式后需要重启采集应用。两种模式都会保存相机、IMU、音频和头部位姿数据，区别只在于额外保存手部关节还是手柄位姿。

### 2. 统一时间戳

每轮采集开始时，设备建立统一的时间基准。相机、音频、IMU、头部位姿、手势和手柄数据都会写入同一条 UTC 纳秒时间线。

这意味着数据虽然分开保存在不同文件中，但可以通过时间戳对应起来：

| 数据 | 主要时间字段 |
|---|---|
| RGB、Tracking、Ctrl 帧 | 对应 `*_metainfo.csv` 中的 `mid_exposure_utc_ns` |
| 音频数据包 | `audio_metainfo.csv` 中的 `capture_utc_ns` |
| 加速度、陀螺仪 | `accel.csv`、`gyro.csv` 中的 `timestamp_ns` |
| 头部位姿 | `head_pose.csv` 中的 `timestamp_ns` |
| 手势数据 | `hand_tracking.csv` 中的 `timestamp` |
| 手柄数据 | `controller_poses.csv` 中的 `timestamp_ns` |

建议以后处理时以 RGB 帧的 `mid_exposure_utc_ns` 作为图像采样时刻，再匹配对应的头部位姿、手势、手柄和 IMU 数据。不同传感器的采样频率不同，不要求每个文件的每一行时间戳都相同；重要的是它们使用同一时间基准。

视频文件只是图像载体，做数据对齐时应优先使用旁边的 `*_metainfo.csv` 和各传感器 CSV，不要只使用视频播放器显示的时间。

## 二、如何开始一轮采集

### 1. 使用设备按键

| 操作 | 作用 |
|---|---|
| `Right B` 或 `DPAD_CENTER` | 开始或停止本地采集 |

按键开始后，设备会创建一个新的 session，并开始同时保存各路采集数据。再次按键后，设备会先完成编码器、音频、IMU、位姿、手势或手柄数据的收尾，再结束本轮采集。

停止后不要立即拔 U 盘或开始下一轮，等待设备提示“录制已保存”，或者确认 session 中的 `capture_status.json` 已变为 `complete`。

### 2. 使用 ADB

```bash
# 开始本地采集
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING

# 停止本地采集
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING

# 查看已保存的采集 session
adb shell ls /sdcard/Android/data/com.ssnwt.helloxr/files/dataset/

# 拉取到电脑
adb pull /sdcard/Android/data/com.ssnwt.helloxr/files/dataset/ ./dataset/
```

设备按键和 ADB 都是“本地采集”。如果手机 App 正在预览，设备按键或 ADB 开始采集后，会进入“本地采集并预览”状态，不会再创建第二个 session。

## 三、手机 App 的使用方法

手机 App 的实际操作流程如下：

```text
Device Scan → 选择头环 → WiFi Setup → 输入 Wi-Fi → Control Panel
```

### 1. 连接头环

1. 打开 App，进入 `Device Scan`。
2. 首次扫描时，允许蓝牙权限；如果系统提示，也需要打开蓝牙和定位服务。
3. 点击 `Scan`，在设备列表中选择目标头环。
4. 进入 `WiFi Setup` 后输入 Wi-Fi 名称和密码，点击 `Send WiFi Info`。
5. 等待页面显示 Wi-Fi 已连接并获取设备 IP，App 会自动进入 `Control Panel`。

App 会在成功配网后保存最近使用过的 Wi-Fi，下一次可以从 `Choose Saved WiFi` 中选择。Wi-Fi 名称和密码只在手机端本地保存，选择错误的网络时可以返回重新配置。

如果头环已经连接到 Wi-Fi，也可以在 `Device Scan` 页面点击 `Direct`，直接输入头环 IP 地址，跳过蓝牙配网进入控制页面。

### 2. Control Panel 页面

Control Panel 页面包含：

- 设备连接状态。
- 电池电量和充电状态。
- Wi-Fi 信号强度。
- 设备当前采集状态。
- 设备剩余存储空间。
- 故障提示。
- RGB 实时预览画面。
- `开始录制`、`停止录制`、`开始预览`、`停止预览` 四个操作。

App 以设备当前状态为准。按钮变灰表示当前操作不允许，并不是 App 失效。

## 四、App 操作能否切换

下面是用户实际可进行的操作关系。

| 当前状态 | 可以做什么 | 不能做什么 |
|---|---|---|
| 空闲 | 开始录制；开始预览 | 停止录制；停止预览 |
| 手机预览中 | 开始录制；停止预览 | 重复开始预览 |
| 本地录制中 | 停止录制；开始预览 | 开始另一轮录制；停止预览 |
| 本地录制并预览 | 停止录制；停止预览 | 再开始录制；再次开始预览 |
| 手机发起录制 | 停止录制 | 开始/停止预览；再次开始录制 |

### 1. 空闲 → 手机预览

在空闲状态点击 `开始预览`，手机会看到 RGB 实时画面，但设备不会创建采集 session，也不会保存本轮数据。

此时可以：

- 点击 `开始录制`，直接进入手机发起的采集；预览会继续保持。
- 点击 `停止预览`，回到空闲状态。

### 2. 空闲 → 本地录制

本地录制由设备按键或 ADB 开始。开始后，App 会看到设备进入本地录制状态。

此时可以：

- 点击 `开始预览`，进入“本地录制并预览”；采集继续保存，手机同时显示 RGB 画面。
- 点击 `停止录制`，结束本轮本地采集。

App 不能把正在进行的本地录制切换成“手机发起录制”。如果要改变采集方式，应先停止当前采集，确认结束后再重新开始。

### 3. 本地录制并预览 → 本地录制

点击 `停止预览` 只会关闭手机预览，不会停止设备本地采集。设备会继续保存 session，状态回到“本地录制中”。之后仍可重新点击 `开始预览`，也可以点击 `停止录制`。

### 4. 手机预览 → 手机发起录制

在手机预览中点击 `开始录制`，会直接进入手机发起的采集。预览连接保持不变，设备开始创建并保存本轮 session。

手机发起录制后，App 只能点击 `停止录制`。不能单独关闭预览，也不能再次开始录制。

### 5. 采集过程中的连接问题

- 手机控制连接断开时，设备不会因为手机离线而自动丢弃正在保存的本地数据。
- 手机预览连接断开时，App 会尝试重新连接预览；预览连接不代表采集是否正在进行。
- 重新连接 App 后，应以 Control Panel 显示的设备采集状态为准，再决定是否点击 `停止录制`。
- 设备处于 `开始中` 或 `停止中` 时，App 会暂时锁定按钮。停止操作在录制启动过程中仍可用，用于取消等待时间过长的启动。
- 同一个操作在等待设备状态更新期间不能重复点击，避免产生重复的开始或停止请求。

## 五、采集文件在哪里

设备内部存储路径：

```text
/sdcard/Android/data/com.ssnwt.helloxr/files/
├── dataset/<YYYYMMDD_HHMMSS>/   # 每一轮采集
├── logs/app.log                 # 应用级日志
└── usb_debug.log                # U 盘识别和导出日志
```

一轮完整 session 通常包含：

```text
dataset/<YYYYMMDD_HHMMSS>/
├── rgb.mp4 / rgb_metainfo.csv
├── tracking.mp4 / tracking_metainfo.csv
├── ctrl.mp4 / ctrl_metainfo.csv
├── audio.m4a / audio_metainfo.csv
├── accel.csv / gyro.csv
├── head_pose.csv
├── hand_tracking.csv          # 手势模式
├── controller_poses.csv       # 手柄模式
├── imu_calibration.json       # 设备支持时生成
├── camera_params_rgb.json
├── camera_params_tracking.json
├── camera_params_ctrl.json
├── capture_status.json
└── capture.log
```

手势模式和手柄模式不会同时生成两个额外文件：

- 手势模式重点查看 `hand_tracking.csv`。
- 手柄模式重点查看 `controller_poses.csv`。
- 两种模式都会保存 RGB、Tracking、Ctrl、IMU、音频和头部位姿数据。

### 如何判断一轮采集是否结束

查看 session 下的 `capture_status.json`：

- `recording`：仍在采集，不能导出。
- `finalizing`：已经停止采集，但各路数据还在收尾。
- `complete`：本轮数据已经完整结束，可以导出和后处理。

## 六、U 盘导出

1. 停止采集。
2. 确认 session 的 `capture_status.json` 为 `complete`。
3. 插入可写 U 盘。
4. 系统识别 U 盘后，会在 U 盘根目录创建 `Export` 文件夹。
5. 已完成的 session 会自动导出到：

```text
U盘根目录/Export/<YYYYMMDD_HHMMSS>/
```

U 盘导出以完整 session 为单位，会同时导出视频、传感器 CSV、位姿、标定信息和日志，不会只导出某一个视频文件。

复制成功后，设备才会删除本地对应 session，释放设备存储空间。复制失败或 U 盘中途拔出时，本地 session 会保留，重新插入 U 盘即可再次尝试。

听到“U 盘拷贝已完成”之前不要拔出 U 盘。以下提示可以帮助判断状态：

- U 盘已识别。
- U 盘拷贝已完成。
- 当前无数据需要拷贝。
- 本轮存在拷贝失败。
- U 盘已卸载。

## 七、日志在哪里

### 1. 应用级日志

```text
/sdcard/Android/data/com.ssnwt.helloxr/files/logs/app.log
```

用于查看应用启动、设备连接、采集状态、相机/传感器状态和错误信息。日志文件达到大小限制后会滚动保存，旧文件通常为 `app.log.1`。

### 2. 单轮采集日志

```text
dataset/<YYYYMMDD_HHMMSS>/capture.log
```

跟随 session 保存，用于确认本轮采集何时开始、何时停止、各路数据是否完成收尾，以及是否发生过异常。提交采集数据时建议保留整个 session 和 `capture.log`。

### 3. U 盘调试日志

```text
/sdcard/Android/data/com.ssnwt.helloxr/files/usb_debug.log
```

记录 U 盘是否识别、U 盘路径、`Export` 目录是否可写、导出器是否启动以及拔盘事件。U 盘没有反应时优先查看此文件。

### 4. 实时查看日志

```bash
adb logcat | grep -E "HelloXr|DatasetRecorder|DatasetExporter|OperationCoordinator|Encoder"
```

Windows 电脑可使用：

```bat
adb logcat | findstr /I "HelloXr DatasetRecorder DatasetExporter OperationCoordinator Encoder"
```

## 八、采集使用建议

- 采集前先确认设备有足够存储空间，并确认已经选择正确的手势模式或手柄模式。
- 采集过程中不要频繁切换手势/手柄模式；模式切换需要重启应用。
- 停止后先等待 `complete`，再拔 U 盘、拉取数据或开始下一轮。
- 数据分析时优先使用 UTC 时间戳，不要只根据视频播放时间对齐传感器。
- 交付数据时提交完整 session，不要只提交 MP4；`capture.log` 和标定文件对排查和后处理都很重要。
