# AVI 录像功能调试记录：两个疑难 Bug 的原因与修复

> 工程：camera_h743_minorspace（STM32H743 + OV5640 + DCMI + SDRAM + FatFs/SD 卡）
> 现象：Watch 窗口写入 `avi_record_request = 1` 后，录像无法正常完成 30 帧录制。
> 结论：两个独立的 bug 叠加，分别导致"启动即失败"和"录制中途随机死机"。

---

## Bug 1：`.param` 扩展名不符合 8.3 短文件名格式

### 现象

```
avi_record_request = 4    （失败标志）
avi_record_active  = 0    （从未进入录像状态）
avi_record_status  = 6    （FR_INVALID_NAME）
avi_record_frames  = 0    （一帧都没录）
```

录像流程在写入第一帧之前就终止了。

### 原因分析

#### 1. FatFs 关闭了长文件名（LFN）支持

`FATFS/Target/ffconf.h:111`：

```c
#define _USE_LFN    0    /* 0 to 3 */
```

`_USE_LFN = 0` 时，FatFs 只接受 **8.3 短文件名**格式：

| 组成部分 | 限制 |
|---|---|
| 主文件名 | ≤ 8 个字符 |
| 扩展名 | ≤ 3 个字符 |

#### 2. 参数文件的扩展名超长

`Core/Src/services/avi_recorder.c` 中原代码：

```c
static const TCHAR avi_parameter_path[] = "0:/video/AVI00001.param";
```

对照工程里所有 SD 卡路径：

| 文件名 | 主名 | 扩展名 | 是否合法 8.3 |
|---|---|---|---|
| `AVI00001.AVI` | 8 字符 | 3 字符 | ✅ |
| `video`（目录） | 5 字符 | 无 | ✅ |
| `AVI00001.param` | 8 字符 | **5 字符** | ❌ |

`AVI_RecorderStart()` 的执行顺序是：

1. `f_mkdir("0:/video")` → 成功
2. `f_open("0:/video/AVI00001.AVI")` → 成功
3. `f_open("0:/video/AVI00001.param")` → **返回 `FR_INVALID_NAME`**

FatFs 的 `FRESULT` 枚举中 `FR_INVALID_NAME` 排在第 6 位，这就是 Watch 里
`avi_record_status = 6` 的直接来源。

#### 3. 后果传导链

`AVI_RecorderStart()` 要求主文件和参数文件**都打开成功**才置位
`avi_record_active = 1`。参数文件打开失败 → 走失败分支关闭主文件并返回错误码 →
`App_CameraProcess()` 中（`Core/Src/app/app_camera.c:132`）把 `avi_record_request`
置 4。整个录像状态机在第一帧之前终止。

> 注：`.param` 的写法沿用了原 F429 工程的习惯，那个工程大概率开启了 LFN，
> 所以同样的路径在那里是合法的——移植到本工程后才暴露。

### 修复及原理

```c
/* FatFs 关闭了 LFN（_USE_LFN=0），路径必须符合 8.3 短文件名格式。 */
static const TCHAR avi_parameter_path[] = "0:/video/AVI00001.PRM";
```

`PRM` 为 3 个字符，满足 8.3 规则，`f_open` 即可正常打开。该文件只是录像过程中
临时记录每帧长度的辅助文件，扩展名叫什么不影响功能。

> 备选方案：在 CubeMX 中开启 FatFs 的 LFN（`_USE_LFN >= 1`）也可以支持长文件名，
> 但需要额外引入 `option/unicode.c` 和约 550 字节的 LFN 工作缓冲区，且 R0.12c 的
> `_USE_LFN=1` 配置非线程安全。对本工程而言，改短文件名是资源开销最小的方案。

---

## Bug 2：SDRAM 被配成 Strongly-Ordered 内存导致非对齐访问 HardFault

### 现象

修复 Bug 1 后，录像可以启动并成功写入 2~3 帧，随后**整个系统冻结**：
Watch 中所有变量（包括每次主循环都会刷新的 `camera_dma_remaining_words`）全部停止变化。

通过在 `HardFault_Handler` 中增加诊断代码（记录 fault 寄存器和出错 PC）抓到现场：

```
fault_cfsr       = 0x01000000   → CFSR bit24 = UFSR.UNALIGNED（非对齐访问错误）
fault_hfsr       = 0x40000000   → FORCED（UsageFault 升级为 HardFault）
fault_stacked_pc = 0x0810466E    → HAL_SD_WriteBlocks() 的 FIFO 填充循环

出错指令：LDR r0,[r6,#0x04]     → 从 SDRAM 读 JPEG 数据准备写入 SDMMC FIFO
```

### 原因分析（三个因素叠加）

#### 因素 1：MPU 把 SDRAM 配置成了 Strongly-Ordered 内存

`Core/Src/main.c` 的 `MPU_Config()` 原配置：

```c
MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;   // TEX = 0
MPU_InitStruct.IsCacheable   = MPU_ACCESS_NOT_CACHEABLE;  // C = 0
MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE; // B = 0
```

ARM 内存类型由 TEX/C/B 三位决定：

| TEX | C | B | 内存类型 | 允许非对齐访问？ |
|---|---|---|---|---|
| 1 | 0 | 0 | Normal（非缓存） | ✅ 允许 |
| 0 | 0 | 1 | Device | ❌ 禁止 |
| 0 | 0 | 0 | **Strongly-Ordered（原配置）** | ❌ 禁止 |

关键规则：**Cortex-M7 对 Normal 内存的非对齐访问由硬件自动拆分处理，完全合法；
但对 Device / Strongly-Ordered 内存做非对齐访问，一律触发 UNALIGNED UsageFault。**

原配置 TEX=0 + C=0 + B=0 恰好落在 Strongly-Ordered 上。

#### 因素 2：FatFs 的"直通写"优化会传递非对齐指针

`Middlewares/Third_Party/FatFs/src/ff.c:3677`（`f_write` 内部）：

```c
if (disk_write(fs->drv, wbuff, sect, cc) != RES_OK)  /* wbuff = 用户缓冲区指针! */
```

当**文件写位置**对齐到 512 字节扇区边界时，FatFs 跳过自己的缓冲区，把用户缓冲区
指针**原样直通**给 `disk_write`，省一次拷贝。它只保证文件位置对齐了扇区，
**不保证指针本身的字节对齐**。

而 AVI 文件中每帧 JPEG 前有 8 字节 chunk 头（`"00dc"` + 4 字节长度），JPEG 又是变长的：

```
| 8字节头 | JPEG数据(L1) | 8字节头 | JPEG数据(L2) | ...
```

- 若某帧长度 L ≡ 0 (mod 4)：后续数据指针仍 4 字节对齐 → 安全
- 若某帧长度 L ≡ 2 (mod 4)：**下一帧数据起点变为 2 字节对齐**（如 0xC0000000+2）→ 危险

#### 因素 3：编译器把逐字节读取合并成字读取

`HAL_SD_WriteBlocks()` 向 SDMMC FIFO 填数据的源码是每次读 1 字节
（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c:946-953`）：

```c
data  = (uint32_t)(*tempbuff);          tempbuff++;
data |= ((uint32_t)(*tempbuff) << 8U);  tempbuff++;
...
```

armclang 优化时将 4 次 1 字节读取合并为**一条 32 位 LDR 指令**（即抓到的
`LDR r0,[r6,#0x04]`）。这个合并对 Normal 内存完全合法（编译器默认假设数据位于
Normal 内存），但配合因素 1 的 Strongly-Ordered 属性即成致命组合。

### 三个因素的合谋过程

```
某帧 JPEG 长度 ≡ 2 (mod 4)            ← 摄像头输出决定，随画面内容随机
        ↓
下一帧数据指针 = 0xC0000000 + 2        ← 非对齐地址
        ↓
FatFs 直通优化：指针原样传给 SD 驱动    ← ff.c:3677
        ↓
编译器优化：4 次字节读 → 1 次 LDR 字读  ← HAL_SD_WriteBlocks FIFO 循环
        ↓
LDR 非对齐访问 Strongly-Ordered 内存
        ↓
UNALIGNED UsageFault → HardFault
        ↓
HardFault_Handler: while(1){}          ← 主循环永远死在这里
        ↓
所有 Watch 变量冻结（"卡死"的真相）
```

### 为什么崩溃点是"随机"的

触发条件是**某帧 JPEG 长度 mod 4 == 2**，而 OV5640 输出的 JPEG 大小随画面内容
变化：前几帧碰巧都是 4 的倍数时一直正常；一旦出现一帧"长度 ≡ 2"就立刻崩溃。
这解释了：

- 为什么一次运行录 3 帧崩、另一次录 2 帧崩（崩溃帧数不固定）
- 为什么 SD 卡状态一直正常（`sd_disk_card_state = 4` = TRANSFER）
- 为什么 DCMI / DMA 错误计数全部为 0（硬件全部无辜）

### 修复及原理

```c
/* TEX=1 + C=0 + B=0：Normal 非缓存内存。TEX=0+C=0+B=0 是 Strongly-Ordered，
 * 其非对齐访问会触发 UNALIGNED HardFault（FatFs 直通写 SD 时指针可能 2 字节对齐）。 */
MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
```

为什么改成 Normal 非缓存就能彻底解决：

1. **非对齐访问合法化**：Normal 内存上 Cortex-M7 硬件自动处理非对齐 LDR/STR，
   FatFs 直通的非对齐指针、编译器合并出的字读取都不再触发异常。
2. **DMA 一致性不受影响**：非缓存（C=0）意味着 CPU 读写不经过 D-Cache，
   DCMI DMA 写入 SDRAM 后 CPU 读到的永远是最新数据，无需任何 cache 维护操作。
   这是 DMA 共享内存的标准推荐属性。
3. **附带收益**：单张 JPEG 拍照保存（`Storage_SaveRawImage`）走完全相同的
   "SDRAM → f_write 直通 → disk_write" 路径，同样暴露在此风险下，一并修复。

---

## 附：HardFault 诊断手段（建议保留）

`Core/Src/stm32h7xx_it.c` 的 `HardFault_Handler` 中增加了 fault 现场捕获，将以下
变量加入 Keil Watch 即可在一分钟内定位 HardFault：

| 变量 | 含义 |
|---|---|
| `fault_cfsr` | MMFSR+BFSR+UFSR（0xE000ED28），bit24=UNALIGNED 等 |
| `fault_hfsr` | bit30=FORCED 表示由可配置 fault 升级而来 |
| `fault_bfar` | BusFault 目标地址（bit15 BFARVALID 有效时才有意义） |
| `fault_mmfar` | MemManage 目标地址 |
| `fault_stacked_pc` | **出错指令的地址**（可用反汇编窗口定位） |
| `fault_stacked_lr` | 出错时的返回地址 |

排查思路：`fault_cfsr` 确定 fault 类型 → `fault_stacked_pc` 在反汇编窗口定位
出错指令 → 结合寄存器判断访问了什么地址。

---

## 修复后验证结果

`avi_record_request` 置 1 后：

- `avi_record_frames` 从 0 递增至 30（0x1E），约 125KB 帧数据
- 录满 30 帧自动停止：`avi_record_request = 4`、`avi_record_active = 0`、
  `avi_record_status = 0 (FR_OK)`
- SD 卡生成 `0:/video/AVI00001.AVI`，可在电脑播放器正常播放
  （320×240 @ 15fps MJPEG，约 2 秒）
