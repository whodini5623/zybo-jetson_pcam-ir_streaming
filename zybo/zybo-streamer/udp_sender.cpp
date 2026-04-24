#include "udp_sender.h"
#include <iostream>
#include <cstring>

using boost::asio::ip::udp;

UDPSender::UDPSender(boost::asio::io_context& io_ctx,
                     const std::string& host,
                     uint16_t port)
    : socket_(io_ctx, udp::v4())
{
    udp::resolver resolver(io_ctx);
    auto endpoints = resolver.resolve(udp::v4(), host, std::to_string(port));
    endpoint_ = *endpoints.begin();

    // 송신 버퍼 크기 확대 (4MB)
    boost::asio::socket_base::send_buffer_size opt(4 * 1024 * 1024);
    socket_.set_option(opt);
}

void UDPSender::send_frame(CameraID cam_id,
                            uint32_t frame_id,
                            uint16_t width,
                            uint16_t height,
                            const uint8_t* data,
                            size_t len)
{
    size_t offset = 0;

    while (offset < len) {
        // 이번 청크 크기 계산
        size_t chunk = std::min(UDP_CHUNK_SIZE, len - offset);

        // 헤더 생성
        CamHeader hdr = make_header(cam_id,
                                     frame_id,
                                     static_cast<uint32_t>(offset),
                                     static_cast<uint16_t>(chunk),
                                     width,
                                     height);

        // 헤더 + payload 하나의 버퍼로 조립
        uint8_t packet[sizeof(CamHeader) + UDP_CHUNK_SIZE];
        std::memcpy(packet, &hdr, sizeof(CamHeader));
        std::memcpy(packet + sizeof(CamHeader), data + offset, chunk);

        boost::system::error_code ec;
        socket_.send_to(
            boost::asio::buffer(packet, sizeof(CamHeader) + chunk),
            endpoint_,
            0,
            ec);

        if (ec)
            std::cerr << "send_to error: " << ec.message() << "\n";

        offset += chunk;
    }
}
