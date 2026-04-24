/*
#include "v4l2_capture.h"

#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <linux/videodev2.h>

// ioctl 재시도 래퍼
static int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

V4L2Capture::V4L2Capture(const std::string& device,
                          uint32_t width,
                          uint32_t height,
                          FrameCallback cb)
    : device_(device), width_(width), height_(height), cb_(std::move(cb))
{
    open_device();
    init_device();
    start_capture();
}

V4L2Capture::~V4L2Capture() {
    stop_capture();
    uninit_device();
    close_device();
}

void V4L2Capture::open_device() {
    fd_ = open(device_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
        throw std::runtime_error("Failed to open device: " + device_);
}

void V4L2Capture::init_device() {
    // 포맷 설정 (MJPEG)
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width_;
    fmt.fmt.pix.height      = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
        throw std::runtime_error("VIDIOC_S_FMT failed: " + device_);

    // 실제 설정된 해상도 반영
    width_  = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;

    // mmap 버퍼 요청
    v4l2_requestbuffers req{};
    req.count  = V4L2_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0)
        throw std::runtime_error("VIDIOC_REQBUFS failed: " + device_);

    // 버퍼 mmap
    for (int i = 0; i < V4L2_BUFFER_COUNT; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
            throw std::runtime_error("VIDIOC_QUERYBUF failed");

        buffers_[i].length = buf.length;
        buffers_[i].start  = mmap(nullptr, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED)
            throw std::runtime_error("mmap failed");
    }
}

void V4L2Capture::start_capture() {
    // 모든 버퍼 큐에 넣기
    for (int i = 0; i < V4L2_BUFFER_COUNT; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            throw std::runtime_error("VIDIOC_QBUF failed");
    }

    // 스트리밍 시작
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
        throw std::runtime_error("VIDIOC_STREAMON failed");

    running_ = true;
}

void V4L2Capture::stop_capture() {
    running_ = false;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
}

void V4L2Capture::uninit_device() {
    for (auto& buf : buffers_) {
        if (buf.start && buf.start != MAP_FAILED)
            munmap(buf.start, buf.length);
    }
}

void V4L2Capture::close_device() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void V4L2Capture::capture_loop() {
    pollfd pfd{ fd_, POLLIN, 0 };

    while (running_) {
        int r = poll(&pfd, 1, 2000);  // 2초 타임아웃
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll error: " << strerror(errno) << "\n";
            break;
        }
        if (r == 0) {
            std::cerr << "poll timeout: " << device_ << "\n";
            continue;
        }

        // 프레임 dequeue
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            std::cerr << "VIDIOC_DQBUF failed: " << strerror(errno) << "\n";
            continue;
        }

        // 콜백으로 프레임 전달
        cb_(static_cast<const uint8_t*>(buffers_[buf.index].start),
            buf.bytesused);

        // 버퍼 반환
        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            std::cerr << "VIDIOC_QBUF failed: " << strerror(errno) << "\n";
    }
}
*/

#include "v4l2_capture.h"

#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <iostream>

#include <jpeglib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <linux/videodev2.h>

static int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

// ═════════════════════════════════════════════════════════════
//  V4L2Capture — Single-plane (기존 코드 그대로)
// ═════════════════════════════════════════════════════════════

V4L2Capture::V4L2Capture(const std::string& device,
                          uint32_t width,
                          uint32_t height,
                          FrameCallback cb)
    : device_(device), width_(width), height_(height), cb_(std::move(cb))
{
    open_device();
    init_device();
    start_capture();
}

V4L2Capture::~V4L2Capture() {
    stop_capture();
    uninit_device();
    close_device();
}

void V4L2Capture::open_device() {
    fd_ = open(device_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
        throw std::runtime_error("Failed to open device: " + device_);
}

void V4L2Capture::init_device() {
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width_;
    fmt.fmt.pix.height      = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
        throw std::runtime_error("VIDIOC_S_FMT failed: " + device_);

    width_  = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;

    v4l2_requestbuffers req{};
    req.count  = V4L2_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0)
        throw std::runtime_error("VIDIOC_REQBUFS failed: " + device_);

    for (int i = 0; i < V4L2_BUFFER_COUNT; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
            throw std::runtime_error("VIDIOC_QUERYBUF failed");

        buffers_[i].length = buf.length;
        buffers_[i].start  = mmap(nullptr, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd_, buf.m.offset);

        if (buffers_[i].start == MAP_FAILED)
            throw std::runtime_error("mmap failed");
    }
}

void V4L2Capture::start_capture() {
    for (int i = 0; i < V4L2_BUFFER_COUNT; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            throw std::runtime_error("VIDIOC_QBUF failed");
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
        throw std::runtime_error("VIDIOC_STREAMON failed");

    running_ = true;
}

void V4L2Capture::stop_capture() {
    running_ = false;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
}

void V4L2Capture::uninit_device() {
    for (auto& buf : buffers_) {
        if (buf.start && buf.start != MAP_FAILED)
            munmap(buf.start, buf.length);
    }
}

void V4L2Capture::close_device() {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}

void V4L2Capture::capture_loop() {
    pollfd pfd{ fd_, POLLIN, 0 };

    while (running_) {
        int r = poll(&pfd, 1, 2000);
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[v4l2] poll error: " << strerror(errno) << "\n";
            break;
        }
        if (r == 0) {
            std::cerr << "[v4l2] poll timeout: " << device_ << "\n";
            continue;
        }

        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            std::cerr << "[v4l2] VIDIOC_DQBUF failed: " << strerror(errno) << "\n";
            continue;
        }

        cb_(static_cast<const uint8_t*>(buffers_[buf.index].start),
            buf.bytesused);

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            std::cerr << "[v4l2] VIDIOC_QBUF failed: " << strerror(errno) << "\n";
    }
}

// ═════════════════════════════════════════════════════════════
//  V4L2CaptureMP — Multiplanar (PCAM, YUYV)
// ═════════════════════════════════════════════════════════════

V4L2CaptureMP::V4L2CaptureMP(const std::string& device,
                               uint32_t width,
                               uint32_t height,
                               FrameCallback cb)
    : device_(device), width_(width), height_(height), cb_(std::move(cb))
{
    open_device();
    init_device();
    //start_capture();
}

V4L2CaptureMP::~V4L2CaptureMP() {
    stop_capture();
    uninit_device();
    close_device();
}

void V4L2CaptureMP::open_device() {
    fd_ = open(device_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
        throw std::runtime_error("Failed to open device: " + device_);
}

void V4L2CaptureMP::init_device() {
    // VIDIOC_S_FMT - xilinx-vipp DMA 포맷 설정용 (필수)
    v4l2_format fmt{};
    fmt.type                                  = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width                      = 1280;
    fmt.fmt.pix_mp.height                     = 720;
    fmt.fmt.pix_mp.pixelformat                = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix_mp.field                      = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes                 = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage     = 1280 * 720 * 2;
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline  = 1280 * 2;

    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0)
        throw std::runtime_error("VIDIOC_S_FMT (MP) failed: " + device_);

    width_  = fmt.fmt.pix_mp.width;
    height_ = fmt.fmt.pix_mp.height;
    std::cout << "[v4l2mp] format set: " << width_ << "x" << height_ << " YUYV\n";

    // VIDIOC_REQBUFS
    v4l2_requestbuffers req{};
    req.count  = V4L2_BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0)
        throw std::runtime_error("VIDIOC_REQBUFS (MP) failed: " + device_);

    std::cout << "[v4l2mp] REQBUFS granted: " << req.count << "\n";

    // VIDIOC_QUERYBUF + mmap
    for (int i = 0; i < V4L2_BUFFER_COUNT; ++i) {
        v4l2_buffer buf{};
        v4l2_plane  planes[2]{};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 1;

        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0)
            throw std::runtime_error("VIDIOC_QUERYBUF (MP) failed");

        std::cout << "[v4l2mp] buf[" << i << "] length=" << planes[0].length
                  << " offset=" << planes[0].m.mem_offset << "\n";

        buffers_[i].planes[0].length = planes[0].length;
        buffers_[i].planes[0].start  = mmap(nullptr, planes[0].length,
                                             PROT_READ | PROT_WRITE,
                                             MAP_SHARED, fd_,
                                             planes[0].m.mem_offset);
        if (buffers_[i].planes[0].start == MAP_FAILED)
            throw std::runtime_error("mmap (MP) failed");
    }
}

void V4L2CaptureMP::start_capture() {
    for (int i = 0; i < V4L2_BUFFER_COUNT; ++i) {
        v4l2_buffer buf{};
        v4l2_plane  planes[2]{};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 1;

        // 명시적으로 plane 크기 설정
        planes[0].length    = buffers_[i].planes[0].length;
        planes[0].bytesused = 0;  // 캡처 전이므로 0

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[v4l2mp] VIDIOC_QBUF failed: " << strerror(errno) << "\n";
            throw std::runtime_error("VIDIOC_QBUF (MP) failed");
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
        throw std::runtime_error("VIDIOC_STREAMON (MP) failed");

    running_ = true;
    std::cout << "[v4l2mp] streaming started: " << device_ << "\n";
}

void V4L2CaptureMP::stop_capture() {
    running_ = false;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
}

void V4L2CaptureMP::uninit_device() {
    for (auto& buf : buffers_)
        for (auto& pl : buf.planes)
            if (pl.start && pl.start != MAP_FAILED)
                munmap(pl.start, pl.length);
}

void V4L2CaptureMP::close_device() {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}
/*
void V4L2CaptureMP::capture_loop() {
    pollfd pfd{ fd_, POLLIN, 0 };

    while (running_) {
        int r = poll(&pfd, 1, 2000);
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[v4l2mp] poll error: " << strerror(errno) << "\n";
            break;
        }
        if (r == 0) {
            std::cerr << "[v4l2mp] poll timeout: " << device_ << "\n";
            continue;
        }

        v4l2_buffer buf{};
        v4l2_plane  planes[2]{};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.length   = 1;

        if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            std::cerr << "[v4l2mp] VIDIOC_DQBUF failed: " << strerror(errno) << "\n";
            continue;
        }

        // plane 0 데이터를 콜백으로 전달
        cb_(static_cast<const uint8_t*>(buffers_[buf.index].planes[0].start),
            planes[0].bytesused);

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            std::cerr << "[v4l2mp] VIDIOC_QBUF failed: " << strerror(errno) << "\n";
    }
}
*/

// YUYV → MJPEG 압축 
static std::vector<uint8_t> yuyv_to_jpeg(const uint8_t* yuyv,
                                          uint32_t width, uint32_t height,
                                          int quality = 80)
{
    struct jpeg_compress_struct cinfo{};
    struct jpeg_error_mgr       jerr{};
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned char* outbuf = nullptr;
    unsigned long  outlen = 0;
    jpeg_mem_dest(&cinfo, &outbuf, &outlen);

    cinfo.image_width      = width;
    cinfo.image_height     = height;
    cinfo.input_components = 3;
    cinfo.in_color_space   = JCS_YCbCr;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    // YUYV(YUY2): Y0 U0 Y1 V0 → 2픽셀씩 처리
    std::vector<uint8_t> row(width * 3);
    while (cinfo.next_scanline < height) {
        const uint8_t* src = yuyv + cinfo.next_scanline * width * 2;
        uint8_t* dst = row.data();
        for (uint32_t x = 0; x < width; x += 2) {
            uint8_t y0 = src[0], u = src[1], y1 = src[2], v = src[3];
            dst[0] = y0; dst[1] = u; dst[2] = v;
            dst[3] = y1; dst[4] = u; dst[5] = v;
            src += 4; dst += 6;
        }
        JSAMPROW row_ptr = row.data();
        jpeg_write_scanlines(&cinfo, &row_ptr, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    std::vector<uint8_t> result(outbuf, outbuf + outlen);
    free(outbuf);
    return result;
}

void V4L2CaptureMP::capture_loop() {

    start_capture(); // 추가
    pollfd pfd{ fd_, POLLIN, 0 };

    while (running_) {
        int r = poll(&pfd, 1, 2000);
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[v4l2mp] poll error: " << strerror(errno) << "\n";
            break;
        }
        if (r == 0) {
            std::cerr << "[v4l2mp] poll timeout: " << device_ << "\n";
            continue;
        }

        v4l2_buffer buf{};
        v4l2_plane  planes[2]{};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.length   = 1;

        if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            std::cerr << "[v4l2mp] VIDIOC_DQBUF failed: " << strerror(errno) << "\n";
            continue;
        }

        // YUYV → MJPEG 압축 후 콜백
        auto jpeg = yuyv_to_jpeg(
            static_cast<const uint8_t*>(buffers_[buf.index].planes[0].start),
            width_, height_, 80);

        cb_(jpeg.data(), jpeg.size());

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0)
            std::cerr << "[v4l2mp] VIDIOC_QBUF failed: " << strerror(errno) << "\n";
    }
}