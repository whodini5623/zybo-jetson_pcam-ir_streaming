#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_BUFFERS     8
#define DEFAULT_PAYLOAD 1400

struct buffer {
    void  *start;
    size_t length;
};

struct packet_header {
    char     magic[4];
    uint32_t frame_id;
    uint32_t offset;
    uint16_t size;
    uint16_t flags;         // bit0=first_chunk, bit1=camera_id(0=PCAM,1=IR)
    uint32_t timestamp_ms;
} __attribute__((packed));

struct cam_config {
    const char *device;
    int         width;
    int         height;
    uint32_t    pixelformat;
    int         multiplanar;
    int         use_userptr;   // 1=USERPTR, 0=MMAP
    const char *receiver_ip;
    int         port;
    const char *name;
};

static volatile sig_atomic_t stop_requested;
static void on_signal(int sig) { (void)sig; stop_requested = 1; }

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}

static double now_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static uint32_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000ULL + tv.tv_usec / 1000);
}

static void *cam_thread(void *arg)
{
    struct cam_config *cfg = (struct cam_config *)arg;

    int vfd = open(cfg->device, O_RDWR | O_NONBLOCK);
    if (vfd < 0) { perror("open"); return NULL; }

    // ── 포맷 설정 ──────────────────────────────────────────
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    if (cfg->multiplanar) {
        fmt.type                                  = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width                      = cfg->width;
        fmt.fmt.pix_mp.height                     = cfg->height;
        fmt.fmt.pix_mp.pixelformat                = cfg->pixelformat;
        fmt.fmt.pix_mp.field                      = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes                 = 1;
        fmt.fmt.pix_mp.plane_fmt[0].sizeimage     = cfg->width * cfg->height * 2;
        fmt.fmt.pix_mp.plane_fmt[0].bytesperline  = cfg->width * 2;
    } else {
        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = cfg->width;
        fmt.fmt.pix.height      = cfg->height;
        fmt.fmt.pix.pixelformat = cfg->pixelformat;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    }

    if (xioctl(vfd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT"); close(vfd); return NULL;
    }

    int width, height;
    size_t frame_size;
    if (cfg->multiplanar) {
        width      = fmt.fmt.pix_mp.width;
        height     = fmt.fmt.pix_mp.height;
        frame_size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    } else {
        width      = fmt.fmt.pix.width;
        height     = fmt.fmt.pix.height;
        frame_size = fmt.fmt.pix.sizeimage;
    }
    if (frame_size == 0)
        frame_size = (size_t)width * (size_t)height * 2;

    // ── 버퍼 요청 ──────────────────────────────────────────
    uint32_t memory = cfg->use_userptr ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = 4; // original is 2
    req.type   = cfg->multiplanar ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                  : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = memory;
    if (xioctl(vfd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS"); close(vfd); return NULL;
    }
    if (req.count < 2) { fprintf(stderr, "[%s] Insufficient buffers\n", cfg->name); return NULL; }
    if (req.count > MAX_BUFFERS) req.count = MAX_BUFFERS;

    // ── 버퍼 할당 ──────────────────────────────────────────
    struct buffer buffers[MAX_BUFFERS];
    memset(buffers, 0, sizeof(buffers));
    unsigned int n_buffers = 0;

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        if (cfg->use_userptr) {
            // USERPTR: malloc으로 일반 메모리 할당 (CMA 미사용)
            buffers[n_buffers].length = frame_size;
            buffers[n_buffers].start  = malloc(frame_size);
            if (!buffers[n_buffers].start) {
                perror("malloc"); return NULL;
            }
        } else {
            // MMAP
            struct v4l2_buffer buf;
            struct v4l2_plane  planes[VIDEO_MAX_PLANES];
            memset(&buf,   0, sizeof(buf));
            memset(planes, 0, sizeof(planes));
            buf.type   = req.type;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = n_buffers;
            if (cfg->multiplanar) {
                buf.length   = VIDEO_MAX_PLANES;
                buf.m.planes = planes;
            }
            if (xioctl(vfd, VIDIOC_QUERYBUF, &buf) < 0) {
                perror("VIDIOC_QUERYBUF"); return NULL;
            }
            if (cfg->multiplanar) {
                buffers[n_buffers].length = planes[0].length;
                buffers[n_buffers].start  = mmap(NULL, planes[0].length,
                                                  PROT_READ | PROT_WRITE, MAP_SHARED,
                                                  vfd, planes[0].m.mem_offset);
            } else {
                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start  = mmap(NULL, buf.length,
                                                  PROT_READ | PROT_WRITE, MAP_SHARED,
                                                  vfd, buf.m.offset);
            }
            if (buffers[n_buffers].start == MAP_FAILED) {
                perror("mmap"); return NULL;
            }
        }
    }

    // ── 버퍼 큐 ────────────────────────────────────────────
    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane  planes[VIDEO_MAX_PLANES];
        memset(&buf,   0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type   = req.type;
        buf.memory = memory;
        buf.index  = i;

        if (cfg->use_userptr) {
            buf.m.userptr = (unsigned long)buffers[i].start;
            buf.length    = buffers[i].length;
        } else if (cfg->multiplanar) {
            buf.length   = VIDEO_MAX_PLANES;
            buf.m.planes = planes;
        }

        if (xioctl(vfd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF"); return NULL;
        }
    }

    // ── UDP 소켓 ────────────────────────────────────────────
    int sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sfd < 0) { perror("socket"); return NULL; }

    int sndbuf = 4 * 1024 * 1024;
    setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons((uint16_t)cfg->port);
    if (inet_pton(AF_INET, cfg->receiver_ip, &dst.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP: %s\n", cfg->receiver_ip);
        return NULL;
    }

    // ── 스트리밍 시작 ───────────────────────────────────────
    enum v4l2_buf_type type = req.type;
    if (xioctl(vfd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON"); return NULL;
    }

    printf("[%s] streaming %dx%d → %s:%d buffers=%u memory=%s\n",
           cfg->name, width, height, cfg->receiver_ip, cfg->port, n_buffers,
           cfg->use_userptr ? "USERPTR" : "MMAP");
    fflush(stdout);

    uint8_t  packet[sizeof(struct packet_header) + DEFAULT_PAYLOAD];
    uint32_t frame_id   = 0;
    uint64_t bytes_sent = 0;
    double   t0 = now_sec(), last_report = t0;

    while (!stop_requested) {
        struct pollfd pfd = { .fd = vfd, .events = POLLIN };
        int pr = poll(&pfd, 1, 2000);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) {
            fprintf(stderr, "[%s] poll timeout\n", cfg->name);
            continue;
        }

        struct v4l2_buffer buf;
        struct v4l2_plane  planes[VIDEO_MAX_PLANES];
        memset(&buf,   0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type   = req.type;
        buf.memory = memory;
        if (cfg->multiplanar) {
            buf.length   = VIDEO_MAX_PLANES;
            buf.m.planes = planes;
        }

        if (xioctl(vfd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            perror("VIDIOC_DQBUF"); break;
        }

        uint8_t *src;
        size_t used;

        if (cfg->use_userptr) {
            src  = (uint8_t *)buf.m.userptr;
            used = buf.bytesused ? buf.bytesused : frame_size;
        } else if (cfg->multiplanar) {
            src  = (uint8_t *)buffers[buf.index].start;
            used = planes[0].bytesused ? planes[0].bytesused : frame_size;
        } else {
            src  = (uint8_t *)buffers[buf.index].start;
            used = buf.bytesused ? buf.bytesused : frame_size;
        }

        if (used > frame_size) used = frame_size;

        // ── 청크 분할 송신 ──────────────────────────────────
        for (size_t off = 0; off < used; off += DEFAULT_PAYLOAD) {
            size_t chunk = used - off;
            if (chunk > DEFAULT_PAYLOAD) chunk = DEFAULT_PAYLOAD;

            struct packet_header *hdr = (struct packet_header *)packet;
            memcpy(hdr->magic, "YUYV", 4);
            hdr->frame_id = htonl(frame_id);
            hdr->offset   = htonl((uint32_t)off);
            hdr->size     = htons((uint16_t)chunk);
            uint16_t flags = (off == 0 ? 0x0001 : 0x0000) |
                             (cfg->port == 5001 ? 0x0002 : 0x0000);
            hdr->flags        = htons(flags);
            hdr->timestamp_ms = htonl(now_ms());
            if (off == 0) {
                memcpy(packet + sizeof(*hdr), src + off, chunk);
                uint8_t *meta = packet + sizeof(*hdr);
                *(uint16_t *)(meta + 0) = htons((uint16_t)width);
                *(uint16_t *)(meta + 2) = htons((uint16_t)height);
            } else {
                memcpy(packet + sizeof(*hdr), src + off, chunk);
            }

            ssize_t sent = sendto(sfd, packet, sizeof(*hdr) + chunk, 0,
                                  (struct sockaddr *)&dst, sizeof(dst));
            if (sent < 0 && errno != ENOBUFS && errno != EAGAIN)
                perror("sendto");
            else if (sent > 0)
                bytes_sent += (uint64_t)sent;
        }

        // ── 버퍼 재큐 ───────────────────────────────────────
        if (cfg->use_userptr) {
            buf.m.userptr = (unsigned long)buffers[buf.index].start;
            buf.length    = buffers[buf.index].length;
        }

        if (xioctl(vfd, VIDIOC_QBUF, &buf) < 0)
            perror("VIDIOC_QBUF requeue");

        frame_id++;

        double t = now_sec();
        if (t - last_report >= 2.0) {
            double elapsed = t - t0;
            printf("[%s] frames=%u fps=%.2f tx=%.1f Mbps\n",
                   cfg->name, frame_id,
                   elapsed > 0.0 ? (double)frame_id / elapsed : 0.0,
                   elapsed > 0.0 ? (double)bytes_sent * 8.0 / elapsed / 1e6 : 0.0);
            fflush(stdout);
            last_report = t;
        }
    }

    xioctl(vfd, VIDIOC_STREAMOFF, &type);
    close(sfd);
    if (cfg->use_userptr) {
        for (unsigned int i = 0; i < n_buffers; ++i)
            free(buffers[i].start);
    } else {
        for (unsigned int i = 0; i < n_buffers; ++i)
            munmap(buffers[i].start, buffers[i].length);
    }
    close(vfd);

    printf("[%s] stopped after %u frames\n", cfg->name, frame_id);
    return NULL;
}

int main(int argc, char **argv)
{
    fprintf(stderr, "NOTE: run sudo ./init-pcam.sh before this\n");
    
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <receiver_ip>\n"
                "Example: %s 192.168.3.143\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    const char *receiver_ip = argv[1];

    struct cam_config ir_cfg = {
        .device      = "/dev/video1",
        .width       = 640,
        .height      = 480,
        .pixelformat = V4L2_PIX_FMT_YUYV,
        .multiplanar = 0,
        .use_userptr = 0,          // MMAP (cma=256M@0x10000000으로 해결)
        .receiver_ip = receiver_ip,
        .port        = 5001,
        .name        = "IR",
    };

    struct cam_config pcam_cfg = {
        .device      = "/dev/video0",
        .width       = 640, // 1280
        .height      = 480, // 720
        .pixelformat = V4L2_PIX_FMT_YUYV,
        .multiplanar = 1,
        .use_userptr = 0,          // MMAP 유지
        .receiver_ip = receiver_ip,
        .port        = 5000,
        .name        = "PCAM",
    };

    pthread_t t_ir, t_pcam;
    pthread_create(&t_pcam, NULL, cam_thread, &pcam_cfg);
    usleep(500000);
    pthread_create(&t_ir,   NULL, cam_thread, &ir_cfg);

    pthread_join(t_ir,   NULL);
    pthread_join(t_pcam, NULL);

    printf("Done.\n");
    return 0;
}
