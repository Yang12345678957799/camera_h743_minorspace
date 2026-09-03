# STM32H743 LVDS 摄像头工程说明

## 一、这个工程是做什么的？

这是一个基于 **STM32H743AGI6**（Cortex-M7，UFBGA169 封装）的**摄像头采集与传输工程**，
由 STM32CubeMX 生成外设初始化代码，使用 **Keil MDK-ARM** 编译（见 `MDK-ARM/` 目录）。

核心功能链路：

```
OV5640 摄像头 ──DVP并口──> DCMI 接口 ──DMA──> 外部 SDRAM (0xC0000000)
                                                │
                        ┌───────────────────────┼───────────────────────┐
                        ▼                       ▼                       ▼
                  SD 卡 (SDMMC1+FatFs)     LVDS 发送 (SPI1)        诊断变量
                  · 保存 JPEG 照片         · 按自定义协议分包       (Keil Watch 观察)
                  · 录制 MJPEG AVI 视频    · 发送图像数据
```

具体来说，程序上电后会：

1. **识别并配置 OV5640 摄像头** —— 通过 I2C1 读取传感器 ID（应为 `0x5640`），
   然后将其配置为 **JPEG 输出、QVGA（320×240）** 模式（早期版本曾用 RGB565 模式）。
2. **通过 DCMI 接口采集图像** —— DCMI 开启 JPEG 模式，Snapshot 快照方式，
   由 DMA（DMA1_Stream0）把一帧图像搬入 **FMC 外挂 SDRAM 的 0xC0000000** 处。
3. **检测 JPEG 帧结束** —— 由于本板在 H7 的 DCMI JPEG 模式下不产生 Frame 回调，
   代码改为**轮询扫描 SDRAM 中的 JPEG SOI（FF D8）/ EOI（FF D9）标记**来判断一帧结束
   （每 10ms 扫描一次；若某帧传输停滞超过 250ms 则丢弃并重启采集，防止卡死）。
4. **保存到 SD 卡**（SDMMC1 + FatFs）：
   - 在 Keil Watch 窗口把 `camera_jpeg_save_request` 写 1 → 下一帧存为 `0:/img/test.jpg`；
   - 把 `avi_record_request` 写 1 → 自动录制 **30 帧 MJPEG AVI 视频** 到
     `0:/video/AVI00001.AVI`（程序会手工构造标准 AVI 文件头和 idx1 索引，结束时回填长度字段）。
5. **通过 SPI1 + 软件片选按 LVDS 协议发送图像数据** —— 协议与原 F429 工程兼容：
   每包 886 字节（帧头 + 序号 + 类型 + 总包数/当前包号 + 长度 + 862 字节数据 + 校验和 + 帧尾），
   一帧 RGB565 QVGA 图像会被拆成 179 包发出。

> **重要特点：本工程大量使用 `volatile` 全局变量 + Keil Watch 窗口作为调试/控制手段** ——
> 不接上位机、不用串口命令，所有状态（传感器 ID、DMA 剩余字数、JPEG 字节数、错误码等）
> 都暴露成全局变量供实时观察，"写 1 触发动作" 的 request 变量就是控制开关。
> 阅读代码时看到成排的 `volatile` 变量就是这个用途。

## 二、目录结构说明

```
stm32h743_lvds_camera/
├── stm32h743_lvds_camera.ioc   ← CubeMX 工程配置文件（引脚/时钟/外设都在这里定义）
│
├── Core/                        ★★★ 用户业务代码（真正需要阅读的部分）★★★
│   ├── Src/ 与 Inc/             ← CubeMX 生成的外设初始化（main.c、dcmi.c、fmc.c、spi.c…）
│   │                                以及中断服务 stm32h7xx_it.c
│   ├── Src/app/  Inc/app/       ← 【应用层】app_camera.c：总调度，决定先做什么后做什么
│   ├── Src/bsp/  Inc/bsp/       ← 【板级驱动】ov5640.c：摄像头 I2C 驱动（探测 ID、
│   │                                RGB565 VGA/QVGA、JPEG QVGA 三套寄存器配置表）
│   └── Src/services/ Inc/services/ ← 【服务层】四个独立功能模块：
│       ├── camera_capture.c/h   ← DCMI+DMA 采集控制、JPEG SOI/EOI 扫描、诊断轮询
│       ├── lvds_tx.c/h          ← LVDS 协议打包 + SPI1 发送（含软件 CS）
│       ├── storage.c/h          ← SD 卡挂载/格式化/自检、BMP 保存、原始图像保存
│       └── avi_recorder.c/h     ← MJPEG AVI 录像器（写文件头/逐帧追加/回填索引）
│
├── Drivers/                     ← ST 官方芯片支持包（一般不用改）
│   ├── CMSIS/                   ← ARM 内核相关（DSP 库、启动相关模板等）
│   └── STM32H7xx_HAL_Driver/    ← H7 的 HAL 库源码
│
├── FATFS/                       ← FatFs 文件系统的 CubeMX 接入层
│   ├── App/                     ← fatfs.c：FatFs 初始化和驱动链接
│   └── Target/                  ← ffconf.h（FatFs 配置）、user_diskio.c（磁盘 IO 桥接）
│
├── Middlewares/Third_Party/FatFs/ ← FatFs 中间件源码（ChaN 的开源 FAT 文件系统）
│
└── MDK-ARM/                     ← Keil 工程（stm32h743_lvds_camera.uvprojx）
                                   及启动文件 startup_stm32h743xx.s
```

分层架构（自上而下调用）：

| 层 | 目录 | 职责 |
|---|---|---|
| 应用层 | `Core/Src/app/` | 编排流程：初始化顺序、主循环里该做什么 |
| 服务层 | `Core/Src/services/` | 每个独立功能一个模块（采集、发送、存储、录像），互不依赖对方内部实现 |
| BSP 驱动层 | `Core/Src/bsp/` | 直接和硬件芯片打交道的驱动（目前只有 OV5640） |
| CubeMX 生成层 | `Core/Src/*.c` | 外设初始化（MX_xxx_Init），全部在 `/* USER CODE */` 注释块之间插代码 |

## 三、关键文件速查

| 文件 | 内容 |
|---|---|
| [main.c](Core/Src/main.c) | 入口：MPU 配置（SDRAM 区域设为非 Cache 共享）、时钟 240MHz（HSE 25M 经 PLL1）、外设初始化、主循环只调 `Storage_Process()` + `App_CameraProcess()` |
| [app_camera.c](Core/Src/app/app_camera.c) | 初始化流程（探测→配置 JPEG QVGA→LVDS 初始化→启动采集）；帧处理（存 JPEG、录像） |
| [ov5640.c](Core/Src/bsp/ov5640.c) | I2C 读 ID、三种输出模式的寄存器配置表写入 |
| [camera_capture.c](Core/Src/services/camera_capture.c) | DCMI Snapshot 启停、JPEG 标记扫描（10ms 周期）、250ms 停滞恢复、各 DCMI/DMA 回调 |
| [lvds_tx.c](Core/Src/services/lvds_tx.c) | 886 字节协议包封装（见下方格式）+ `HAL_SPI_Transmit` 阻塞发送 |
| [storage.c](Core/Src/services/storage.c) | `f_mount` 挂载、RGB565→24bit BMP 转换保存、分段写原始文件、Watch 触发的格式化/读写自检 |
| [avi_recorder.c](Core/Src/services/avi_recorder.c) | `AVI_RecorderStart/AddJPEG/Stop` 三段式录像；每帧长度先写 `.param` 临时文件，结束时读回生成 idx1 索引 |

### LVDS 协议包格式（共 886 字节，与原 F429 工程兼容）

```
┌────────┬──────┬──────┬──────┬───────┬──────┬───────────────┬───────┬────────┐
│ head   │count │ type │total │current│length│  data (862B)  │checksum│  tail  │
│ FC FC  │ 2B   │ A5A5 │  4B  │  4B   │  2B  │   图像数据     │  2B   │1E 1B   │
│ A1 A1  │      │      │(大端)│(大端) │      │               │       │1E 1B   │
└────────┴──────┴──────┴──────┴───────┴──────┴───────────────┴───────┴────────┘
校验和 = 从 type 字段到 data 末尾（偏移 6~880）的累加和（16bit）
```

## 四、硬件资源配置一览

| 资源 | 用途 | 说明 |
|---|---|---|
| DCMI（含 DMA1_Stream0） | 摄像头并口 | JPEG 模式开启、PCK 上升沿、VS 高有效 |
| I2C1 | OV5640 寄存器配置 | 7 位地址 0x3C |
| FMC | 外挂 SDRAM | 帧缓冲在 0xC0000000，MPU 设为 32MB 非缓存共享区 |
| SDMMC1 + FatFs | SD 卡存储 | FAT32，逻辑盘符 `0:` |
| SPI1（TX-only，/8 分频） | LVDS 数据发送 | 软件片选 CS 引脚 |
| USART1 | 串口（调试用） | 已初始化，业务代码未大量使用 |
| USB_OTG_FS | USB（已初始化） | 预留 |

## 五、如何调试 / 运行

1. 用 Keil 打开 `MDK-ARM/stm32h743_lvds_camera.uvprojx` 编译下载。
2. 进入调试后打开 **Watch 窗口**，添加以下变量观察运行状态：
   - `ov5640_id` / `ov5640_status` —— 摄像头是否识别成功（`0x5640` + `HAL_OK`）
   - `camera_jpeg_soi_found` / `camera_jpeg_eoi_found` / `camera_jpeg_bytes` —— JPEG 帧有效性
   - `camera_jpeg_frame_events` —— 累计收到的帧数
   - `camera_dcmi_error_events` / `camera_dma_error_code` —— 链路错误诊断
   - `storage_mount_status` —— SD 卡挂载是否成功（`FR_OK`=0）
3. 控制动作（在 Watch 中把变量改为 1）：
   - `camera_jpeg_save_request = 1` → 保存下一帧为 `0:/img/test.jpg`
   - `avi_record_request = 1` → 开始录像（自动录 30 帧后停止）；写 3 可提前停止
   - `storage_test_request = 1` → SD 卡读写自检；`storage_format_request = 1` → 格式化（谨慎！）
