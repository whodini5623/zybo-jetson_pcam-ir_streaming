#!/bin/sh
width=640
height=480
rate=30

echo "[init-pcam] Setting up PCAM pipeline..."
media-ctl -d /dev/media0 -V "\"ov5640 2-003c\":0 [fmt:UYVY/${width}x${height}@1/${rate} field:none]"
media-ctl -d /dev/media0 -V "\"43c60000.mipi_csi2_rx_subsystem\":0 [fmt:UYVY/${width}x${height} field:none]"
v4l2-ctl -d /dev/video0 --set-fmt-video=width=${width},height=${height},pixelformat=YUYV
echo "[init-pcam] Done."
