#include "rtcm_parser.h"
#include <string.h>

uint8_t rtcm_buffer[UART_RX_BUFFER_SIZE];
uint16_t rtcm_len = 0;

uint16_t rtcm_head = 0; // Vị trí bắt đầu đọc
uint16_t rtcm_tail = 0; // Vị trí kết thúc dữ liệu hiện tại

void RTCM_Process(const uint8_t *data, uint16_t length) {
    // Kiểm tra tràn bộ đệm
    if (rtcm_tail + length > UART_RX_BUFFER_SIZE) {
        // Nếu sắp tràn, dồn dữ liệu còn lại về đầu mảng
        uint16_t remaining = rtcm_tail - rtcm_head;
        memmove(rtcm_buffer, &rtcm_buffer[rtcm_head], remaining);
        rtcm_head = 0;
        rtcm_tail = remaining;
    }

    // Nếu vẫn không đủ chỗ sau khi dồn, bỏ qua để tránh crash
    if (rtcm_tail + length <= UART_RX_BUFFER_SIZE) {
        memcpy(&rtcm_buffer[rtcm_tail], data, length);
        rtcm_tail += length;
    }
}

bool RTCM_GetPacket(uint8_t *out, uint16_t *out_len) {
    // Một gói RTCM tối thiểu phải có 6 byte (3 byte header + 3 byte CRC)
    while ((rtcm_tail - rtcm_head) >= 6) {
        // 1. Tìm byte đồng bộ 0xD3
        if (rtcm_buffer[rtcm_head] != 0xD3) {
            rtcm_head++;
            continue;
        }

        // 2. Lấy chiều dài Payload (nằm ở byte 1 và 2)
        // RTCM format: 0xD3 | 00 | length (10 bits)
        uint16_t payload_len = ((rtcm_buffer[rtcm_head + 1] & 0x03) << 8) | rtcm_buffer[rtcm_head + 2];
        uint16_t total_packet_len = payload_len + 6; // 3 byte đầu + payload + 3 byte CRC

        // 3. Kiểm tra xem đã nhận đủ cả gói chưa
        if ((rtcm_tail - rtcm_head) < total_packet_len) {
            return false; // Chưa đủ dữ liệu, đợi đợt IDLE tiếp theo
        }

        // 4. Copy gói tin ra ngoài
        memcpy(out, &rtcm_buffer[rtcm_head], total_packet_len);
        *out_len = total_packet_len;

        // 5. Cập nhật vị trí head (Nhảy qua gói vừa đọc)
        rtcm_head += total_packet_len;

        // Reset buffer nếu đã đọc hết để tránh mảng phình to về phía sau
        if (rtcm_head == rtcm_tail) {
            rtcm_head = 0;
            rtcm_tail = 0;
        }
        
        return true; 
    }

    return false;
}

