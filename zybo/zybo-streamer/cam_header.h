#pragma once
#include <cstdint>
#include <cstring>
#include <chrono>

// 매직 넘버
constexpr uint8_t CAM_MAGIC[4] = { 0xCA, 0xFE, 0xBE, 0xEF };

// 카메라 ID
enum class CameraID : uint8_t {
    PCAM = 0x01,
    IR   = 0x02,
};

// 패킷 헤더 (26 bytes, packed)
#pragma pack(push, 1)
struct CamHeader {
    uint8_t  magic[4];      // 0xCAFEBEEF
    uint8_t  camera_id;     // CameraID
    uint8_t  reserved;      // 0x00
    uint8_t  timestamp[6];  // Unix time ms (48-bit little endian)
    uint32_t frame_id;      // 프레임 번호
    uint32_t offset;        // 청크 오프셋 (bytes)
    uint16_t size;          // 이 청크의 데이터 크기
    uint16_t width;         // 프레임 가로
    uint16_t height;        // 프레임 세로
};
#pragma pack(pop)

static_assert(sizeof(CamHeader) == 26, "CamHeader must be 26 bytes");

// 헤더 초기화 헬퍼
inline CamHeader make_header(CameraID cam_id,
                              uint32_t frame_id,
                              uint32_t offset,
                              uint16_t chunk_size,
                              uint16_t width,
                              uint16_t height)
{
    CamHeader h{};
    std::memcpy(h.magic, CAM_MAGIC, 4);
    h.camera_id = static_cast<uint8_t>(cam_id);
    h.reserved  = 0x00;

    // 현재 시각 ms (48-bit LE)
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (int i = 0; i < 6; ++i)
        h.timestamp[i] = (now_ms >> (8 * i)) & 0xFF;

    h.frame_id = frame_id;
    h.offset   = offset;
    h.size     = chunk_size;
    h.width    = width;
    h.height   = height;
    return h;
}
