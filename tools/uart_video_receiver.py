#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
uart_video_receiver.py -- PC 端 STM32H743 视频回传测试工具（pyserial）

用法:
    python uart_video_receiver.py COM3 start-rec     # 发送"开始录像"
    python uart_video_receiver.py COM3 stop-rec      # 发送"结束录像"
    python uart_video_receiver.py COM3 transfer      # 发送"视频传输"并接收 AVI
    python uart_video_receiver.py COM3 --baud 115200 start-rec

说明:
    板端固件将 0:/video/AVI00001.AVI 按旧 F429 视频协议分 886 字节包从
    USART1/RS422 发出。本脚本负责:
      1. 发送 6 字节遥控命令;
      2. 搜索帧头 5A 65 DF 2F, 完整收取 886 字节;
      3. 校验 type(5A 5A) / tail(2E E9 C8 FD) / checksum / count 连续性;
      4. 只把 data[0:length] 追加写入 received_AVI00001.avi;
      5. 统计收包数、有效字节数、校验错误、丢包(跳号)。

注意: legacy F429 video packet 没有 total 字段, 传输结束条件为
      "收到 length < 868 的包" 或 "串口连续 IDLE_TIMEOUT 秒没有新数据"。
      超时仅为本调试程序判断传输结束之用, 不是协议的一部分。
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("需要 pyserial: pip install pyserial")

# ---- 遥控命令（与固件 remote_control.c 保持一致）----
CMD_REC_START = bytes([0xEB, 0x90, 0x02, 0x01, 0x53, 0xD1])
CMD_REC_STOP = bytes([0xEB, 0x90, 0x02, 0x01, 0x54, 0xD2])
CMD_VIDEO_TRANSFER = bytes([0xEB, 0x90, 0x14, 0x01, 0x11, 0xA1])

# ---- 886 字节视频包常量 ----
PACKET_SIZE = 886
PAYLOAD_SIZE = 868
HEAD = bytes([0x5A, 0x65, 0xDF, 0x2F])
TAIL = bytes([0x2E, 0xE9, 0xC8, 0xFD])
TYPE = bytes([0x5A, 0x5A])

IDLE_TIMEOUT = 5.0      # 秒; 连续无新数据视为传输结束（仅调试用）
OUTPUT_FILE = "received_AVI00001.avi"


def open_port(port, baud):
    return serial.Serial(port, baud, bytesize=serial.EIGHTBITS,
                         parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                         timeout=0.05)


def transfer(ser):
    print("发送: 视频传输命令 EB 90 14 01 11 A1 ...")
    ser.reset_input_buffer()
    ser.write(CMD_VIDEO_TRANSFER)

    buf = bytearray()
    packets = 0          # 有效包数
    valid_bytes = 0      # 写入 AVI 的有效字节数
    checksum_errors = 0
    skipped = 0          # count 跳号/丢包计数
    expect_count = 0
    last_rx_time = time.time()
    started = False

    with open(OUTPUT_FILE, "wb") as out:
        while True:
            chunk = ser.read(4096)
            if chunk:
                last_rx_time = time.time()
                buf += chunk

            # 搜索帧头
            while True:
                pos = buf.find(HEAD)
                if pos < 0:
                    # 没找到头, 丢弃缓冲中明显不可能是头前导的数据
                    del buf[:-3]
                    break
                if pos > 0:
                    del buf[:pos]
                if len(buf) < PACKET_SIZE:
                    break  # 等待完整包

                pkt = bytes(buf[:PACKET_SIZE])
                del buf[:PACKET_SIZE]

                # 校验
                if pkt[8:10] != TYPE or pkt[-4:] != TAIL:
                    checksum_errors += 1
                    print(f"[!] 包 {packets}: type/tail 校验失败, 丢弃")
                    continue
                count = int.from_bytes(pkt[4:8], "big")
                length = int.from_bytes(pkt[10:12], "big")
                calc = sum(pkt[12:12 + PAYLOAD_SIZE]) & 0xFFFF
                recv_csum = int.from_bytes(pkt[880:882], "big")
                if calc != recv_csum:
                    checksum_errors += 1
                    print(f"[!] 包 count={count}: checksum 错 "
                          f"(计算 {calc:04X} 收到 {recv_csum:04X})")
                    continue
                if not (0 < length <= PAYLOAD_SIZE):
                    checksum_errors += 1
                    print(f"[!] 包 count={count}: 非法 length={length}")
                    continue

                if count != expect_count:
                    skipped += 1
                    print(f"[!] count 跳号: 期望 {expect_count} 收到 {count}")
                    expect_count = count

                out.write(pkt[12:12 + length])
                packets += 1
                valid_bytes += length
                expect_count += 1
                started = True

                if packets % 50 == 0 or length < PAYLOAD_SIZE:
                    print(f"包 {packets:6d}  count={count:6d}  "
                          f"len={length:4d}  累计 {valid_bytes} 字节")

                if length < PAYLOAD_SIZE:
                    # 短包 = 最后一包（文件大小恰为 868 整数倍时由超时兜底）
                    print("收到短包, 判断为最后一包, 传输结束。")
                    return finish(packets, valid_bytes, checksum_errors, skipped)

            # 传输已经开始后, 连续 IDLE_TIMEOUT 秒无新数据 -> 结束
            # (legacy 协议无 total 字段, 该超时仅用于本调试程序)
            if started and (time.time() - last_rx_time) > IDLE_TIMEOUT:
                print("串口持续无新数据(调试超时), 判断传输结束。")
                return finish(packets, valid_bytes, checksum_errors, skipped)

            # 还没等到第一个包且超时: 报错退出
            if not started and (time.time() - last_rx_time) > IDLE_TIMEOUT:
                print("[!] 超时未收到任何视频包, 请检查板端 video_tx_active"
                      " 与接线。")
                return finish(packets, valid_bytes, checksum_errors, skipped)


def finish(packets, valid_bytes, checksum_errors, skipped):
    print("-" * 60)
    print(f"收到包数量        : {packets}")
    print(f"有效 AVI 字节数   : {valid_bytes}")
    print(f"checksum 错误数   : {checksum_errors}")
    print(f"丢包/跳号数       : {skipped}")
    print(f"输出文件          : {OUTPUT_FILE}")
    if skipped == 0 and checksum_errors == 0 and packets > 0:
        print("结果: 传输完整, 可直接用播放器打开验证。")
    else:
        print("结果: 存在异常, 请核对板端 Watch 变量后重试。")
    return 0 if (skipped == 0 and checksum_errors == 0) else 1


def main():
    ap = argparse.ArgumentParser(description="STM32H743 视频回传测试工具")
    ap.add_argument("port", help="串口名, 如 COM3")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("command",
                    choices=["start-rec", "stop-rec", "transfer"],
                    help="start-rec=开始录像  stop-rec=结束录像  transfer=回传并接收")
    args = ap.parse_args()

    ser = open_port(args.port, args.baud)
    try:
        if args.command == "start-rec":
            ser.write(CMD_REC_START)
            print("已发送: 开始录像 EB 90 02 01 53 D1")
            time.sleep(0.5)
            resp = ser.read(200)
            if resp:
                print("板端应答:", resp.decode(errors="replace").strip())
        elif args.command == "stop-rec":
            ser.write(CMD_REC_STOP)
            print("已发送: 结束录像 EB 90 02 01 54 D2")
            time.sleep(0.5)
            resp = ser.read(200)
            if resp:
                print("板端应答:", resp.decode(errors="replace").strip())
        else:
            sys.exit(transfer(ser))
    finally:
        ser.close()


if __name__ == "__main__":
    main()
