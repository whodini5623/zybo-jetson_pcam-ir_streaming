# zybo-jetson_pcam-ir_streaming
zybo z7 10 보드와 jetson thor와의 영상 전송 코드입니다. 

# zybo-jetson-streaming

Zybo 보드에 연결된 **PCAM 5C**(RGB)와 **IR 카메라** 영상을 동시에 캡처하여 YUYV 포맷으로 Jetson에 UDP 스트리밍하는 프로젝트입니다.

---

## 프로젝트 구조

```
zybo-jetson-streaming/
├── boot/
│   └── uEnv.txt          # U-Boot 환경 변수 — CMA 영역 설정 포함
├── jetson/               # Jetson 수신 측 코드 (추후 추가 예정)
└── zybo/
    ├── init-pcam.sh      # PCAM 카메라 초기화 스크립트 (반드시 먼저 실행)
    └── zybo-cam-stream.c # V4L2 캡처 + UDP 청크 송신 메인 코드
```

---

## 시스템 구성

```
[Zybo Z7]
  ├─ /dev/video0  PCAM 5C (OV5640)   640×480  YUYV  multi-planar  → UDP :5000
  └─ /dev/video1  IR Cam (UVC/USB)   640×480  YUYV  single-plane  → UDP :5001
                         |
                   동일 LAN / 직접 이더넷
                         |
               [Jetson]  수신 포트 5000, 5001
```

---

## 하드웨어 요구사항

| 구성 요소 | 사양 |
|-----------|------|
| FPGA 보드 | Digilent Zybo Z7 (Zynq-7000) |
| RGB 카메라 | Digilent PCAM 5C (OV5640, MIPI) |
| IR 카메라 | USB UVC 장치 (AFN_Cap, `/dev/video1`) |
| 수신 장치 | NVIDIA Jetson (Nano / Orin 등) |
| 네트워크 | 동일 LAN 또는 직접 이더넷 연결 |

---

## 빌드

```bash
gcc -O2 -pthread -o zybo-cam-stream zybo/zybo-cam-stream.c
```

외부 라이브러리 의존성 없음 — 표준 V4L2 + POSIX pthread만 사용합니다.

---

## 실행

```bash
# 1. PCAM 초기화 (매 부팅마다 필요, 빠트리면 커널 크래시)
sudo ./init-pcam.sh

# 2. 스트리밍 시작
./zybo-cam-stream <JETSON_IP>

# 예시
./zybo-cam-stream 192.168.3.143
```

`Ctrl+C` 또는 `SIGTERM`으로 양쪽 스레드가 정상 종료됩니다.

---

## 패킷 구조

각 UDP 패킷은 고정 헤더(20 bytes) + 최대 1400 bytes 페이로드로 구성됩니다.

```
 0       4       8      12     14     16     18     20
 +-------+-------+------+------+------+------+------+
 | magic | frame | off  | size | width|height| rsv  |
 | "YUYV"| _id   | set  |      |      |      |      |
 +-------+-------+------+------+------+------+------+
 |          payload (≤ 1400 bytes)                  |
 +--------------------------------------------------+
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `magic` | char[4] | `"YUYV"` 고정 |
| `frame_id` | uint32 BE | 프레임 순번 |
| `offset` | uint32 BE | 프레임 내 바이트 오프셋 |
| `size` | uint16 BE | 이번 청크 크기 |
| `width` | uint16 BE | 프레임 가로 (픽셀) |
| `height` | uint16 BE | 프레임 세로 (픽셀) |
| `reserved` | uint16 | 예약 |

마지막 청크는 1400 bytes 미만 → Jetson 수신측은 이를 기준으로 프레임 완성 판단합니다.

포트 **5000** → PCAM, 포트 **5001** → IR 으로 카메라를 구분합니다.

---

## uEnv.txt 설명

```
uenvcmd=fatload mmc 0 0x00200000 uImage; \
        fatload mmc 0 0x00100000 system.dtb; \
        fdt addr 0x00100000; \
        fdt get value bootargs /chosen bootargs; \
        setenv bootargs ${bootargs} root=/dev/mmcblk0p2 ro rootwait cma=256M@0x10000000; \
        bootm 0x00200000 - 0x00100000
```

| 항목 | 값 | 설명 |
|------|----|------|
| uImage 로드 주소 | `0x00200000` | 커널 이미지 |
| DTB 로드 주소 | `0x00100000` | 디바이스 트리 |
| rootfs | `/dev/mmcblk0p2` | SD 카드 2번 파티션 |
| **CMA** | `256M@0x10000000` | V4L2 MMAP 버퍼용 연속 메모리 |

---

## ⚠️ CMA / uEnv.txt 트러블슈팅 (삽질 기록)

이 섹션은 실제 디버깅 과정에서 겪은 문제들을 순서대로 정리한 것입니다.

---

### 문제 1: IR 카메라 `VIDIOC_STREAMON: Cannot allocate memory`

**증상**

```
[PCAM] streaming 1280x720 → ... buffers=2 memory=MMAP
chipidea-usb2 e0002000.usb: Rejecting highmem page from CMA.
VIDIOC_STREAMON: Cannot allocate memory
```

**원인**

PCAM (xilinx-frmbuf) 드라이버가 CMA를 먼저 선점하고, IR 카메라의 USB 컨트롤러(chipidea-usb2)가 DMA 버퍼를 할당할 CMA가 부족해서 실패합니다. IR 카메라는 드라이버가 `uvcvideo` (USB 웹캠)이고, UVC는 내부적으로 USB DMA를 사용하므로 USERPTR 방식으로 우회할 수 없습니다.

**확인**

```bash
cat /proc/cmdline           # cma= 파라미터 있는지 확인
grep CmaTotal /proc/meminfo # 기본값 128MB(131072 kB)면 부족
```

---

### 문제 2: CMA 256MB로 늘렸는데도 여전히 실패

**증상**

`CmaFree: 55144 kB` 여유가 있는데도 `Rejecting highmem page from CMA` 반복.

**원인**

CMA가 `cma=256M`만 지정하면 커널이 **높은 주소**(예: 0x30000000 = 768MB 위)에 배치합니다. chipidea-usb2 DMA는 lowmem 범위(32-bit 주소)만 접근 가능한데, CMA가 highmem에 잡혀 DMA 가능한 페이지가 없는 것입니다.

```
dmesg 확인:
cma: Reserved 256 MiB at 0x30000000  ← 이러면 안 됨
cma: Reserved 256 MiB at 0x10000000  ← 이래야 함
```

**해결**

`cma=256M@0x10000000` 으로 주소를 명시적으로 고정합니다.

```bash
dmesg | grep cma
# cma: Reserved 256 MiB at 0x10000000  ← 정상
grep CmaTotal /proc/meminfo
# CmaTotal: 262144 kB  ← 정상
```

---

### 문제 3: uEnv.txt를 수정해도 부팅에 반영이 안 됨

**원인**

이 보드의 `boot.scr`은 아래 순서로 동작합니다.

1. `uEnv.txt` 읽어서 `env import`
2. `uenvcmd` 변수가 있으면 `run uenvcmd` 실행 후 나머지 스킵
3. 없으면 DTB의 `/chosen/bootargs`를 읽어서 `bootargs` **덮어씀**

따라서 `uEnv.txt`에 `bootargs=...`만 써두면 3번 단계에서 DTB가 덮어버려 무시됩니다.

**해결**

`uenvcmd`를 정의해서 boot.scr의 나머지 흐름을 건너뛰고 직접 부팅합니다. DTB에서 bootargs를 읽은 뒤 `cma=...`와 `root=`를 직접 이어붙입니다.

```
uenvcmd=fatload mmc 0 0x00200000 uImage; fatload mmc 0 0x00100000 system.dtb; fdt addr 0x00100000; fdt get value bootargs /chosen bootargs; setenv bootargs ${bootargs} root=/dev/mmcblk0p2 ro rootwait cma=256M@0x10000000; bootm 0x00200000 - 0x00100000
```

> `root=/dev/mmcblk0p2`를 빠트리면 이 보드의 DTB `bootargs`에 root= 가 없어서 kernel panic이 납니다.

---

### 문제 4: uEnv.txt 파일 쓰기가 제대로 안 됨

`nano`에서 저장 시 줄이 붙거나 CRLF가 섞이는 경우가 있었습니다. 아래 방법으로 확실하게 덮어씁니다.

```bash
sudo tee /boot/uEnv.txt << 'EOF'
uenvcmd=fatload mmc 0 0x00200000 uImage; fatload mmc 0 0x00100000 system.dtb; fdt addr 0x00100000; fdt get value bootargs /chosen bootargs; setenv bootargs ${bootargs} root=/dev/mmcblk0p2 ro rootwait cma=256M@0x10000000; bootm 0x00200000 - 0x00100000
EOF

# 반드시 확인
cat /boot/uEnv.txt
```

---

### 문제 5: 부팅 불가 상태에서 복구 (U-Boot 프롬프트)

uEnv.txt가 잘못되어 kernel panic으로 부팅이 안 되는 경우, UART 콘솔(115200 baud)에서 재부팅 직후 아무 키를 연타해 U-Boot 프롬프트에 진입합니다.

```
Zynq>
```

프롬프트에서 직접 부팅 명령을 실행합니다.

```
fatload mmc 0 0x00200000 uImage; fatload mmc 0 0x00100000 system.dtb; fdt addr 0x00100000; fdt get value bootargs /chosen bootargs; setenv bootargs ${bootargs} root=/dev/mmcblk0p2 ro rootwait cma=256M@0x10000000; bootm 0x00200000 - 0x00100000
```

부팅 성공 후 `/boot/uEnv.txt`를 수정하고 재부팅합니다.

---

### 최종 확인

```bash
# CMA 위치 및 크기
dmesg | grep cma
# 기대값: cma: Reserved 256 MiB at 0x10000000

# 부팅 파라미터
cat /proc/cmdline
# 기대값: ... cma=256M@0x10000000

# CMA 잔여 메모리
grep CmaTotal /proc/meminfo
# 기대값: CmaTotal: 262144 kB
```

---

## 참고

- [Digilent PCAM 5C 공식 문서](https://digilent.com/reference/add-ons/pcam-5c/start)
- [Zybo Z7 리소스 센터](https://digilent.com/reference/programmable-logic/zybo-z7/start)
- [U-Boot 환경 변수 공식 문서](https://u-boot.readthedocs.io/en/latest/usage/environment.html)
- [Linux V4L2 API](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)