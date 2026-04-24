#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <boost/asio.hpp>
#include "cam_header.h"
#include "v4l2_capture.h"
#include "udp_sender.h"

static std::atomic<bool> g_running{true};
void signal_handler(int) { g_running = false; }

int main() {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    const std::string JETSON_IP = "192.168.10.1"; // pc : 192.168.10.1 jetson: 192.168.3.143
    boost::asio::io_context io_ctx;
    auto work = boost::asio::make_work_guard(io_ctx);

    // --- IR (video1, Single-plane, MJPEG, 640x480) ---
    UDPSender sender_ir(io_ctx, JETSON_IP, 5001);
    std::atomic<uint32_t> frame_id_ir{0};
    V4L2Capture ir(
        "/dev/video1", 640, 480,
        [&](const uint8_t* data, size_t len) {
            sender_ir.send_frame(CameraID::IR,
                                 frame_id_ir++,
                                 640, 480,
                                 data, len);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // ← 추가    

    // --- PCAM (video0, Multiplanar, YUYV→MJPEG, 1920x1080) ---
    UDPSender sender_pcam(io_ctx, JETSON_IP, 5000);
    std::atomic<uint32_t> frame_id_pcam{0};
    V4L2CaptureMP pcam(
        "/dev/video0", 1280, 720,
        [&](const uint8_t* data, size_t len) {
            sender_pcam.send_frame(CameraID::PCAM,
                                   frame_id_pcam++,
                                   1920, 1080,
                                   data, len);
        });

    std::thread t_ir([&]  { ir.capture_loop();   });
    std::thread t_pcam([&]{ pcam.capture_loop(); });

    std::cout << "Streaming started. IR→5001, PCAM→5000. Ctrl+C to stop.\n";

    while (g_running)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Stopping...\n";
    ir.stop();
    pcam.stop();
    work.reset();
    io_ctx.stop();
    t_ir.join();
    t_pcam.join();
    std::cout << "Done.\n";
    return 0;
}
