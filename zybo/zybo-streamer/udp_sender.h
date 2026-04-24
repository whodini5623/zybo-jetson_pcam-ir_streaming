#pragma once
#include <boost/asio.hpp>
#include <string>
#include <cstdint>
#include "cam_header.h"

// UDP 청크 크기 (헤더 제외 payload)
constexpr size_t UDP_CHUNK_SIZE = 1400;

class UDPSender {
public:
    UDPSender(boost::asio::io_context& io_ctx,
              const std::string& host,
              uint16_t port);

    // 프레임 전체를 청크로 분할해서 송신
    void send_frame(CameraID cam_id,
                    uint32_t frame_id,
                    uint16_t width,
                    uint16_t height,
                    const uint8_t* data,
                    size_t len);

private:
    boost::asio::ip::udp::socket   socket_;
    boost::asio::ip::udp::endpoint endpoint_;
};
