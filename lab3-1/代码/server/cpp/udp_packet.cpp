#include "udp_packet.h"

// ���ñ�־λ
void UDP_Packet::Set_CFH() {
    flag |= FLAG_CFH;
}

bool UDP_Packet::Is_CFH() const {
    return (flag & FLAG_CFH) != 0;
}

void UDP_Packet::Set_ACK() {
    flag |= FLAG_ACK;
}

bool UDP_Packet::Is_ACK() const {
    return (flag & FLAG_ACK) != 0;
}

void UDP_Packet::Set_SYN() {
    flag |= FLAG_SYN;
}

bool UDP_Packet::Is_SYN() const {
    return (flag & FLAG_SYN) != 0;
}

void UDP_Packet::Set_FIN() {
    flag |= FLAG_FIN;
}

bool UDP_Packet::Is_FIN() const {
    return (flag & FLAG_FIN) != 0;
}

// У��ͼ���
uint16_t UDP_Packet::Calculate_Checksum() const {
    // ��֤ this �� data ����Ч��
    if (this == nullptr) {
        std::cerr << "[����] this ָ��Ϊ�գ��޷�����У��͡�" << std::endl;
        return 0;
    }
    if (data == nullptr) {
        std::cerr << "[����] ����ָ����Ч���޷�����У��͡�" << std::endl;
        return 0;
    }

    uint32_t sum = 0;

    // �ۼ� UDP ͷ��
    sum += src_port;
    sum += dest_port;
    sum += (seq >> 16) & 0xFFFF;
    sum += seq & 0xFFFF;
    sum += (ack >> 16) & 0xFFFF;
    sum += ack & 0xFFFF;
    sum += length;

    // �ۼ����ݲ��֣�ȷ����Χ�Ϸ�
    for (size_t i = 0; i < MAX_DATA_SIZE - 1 && i + 1 < length; i += 2) {
        uint16_t word = (data[i] << 8) | (data[i + 1] & 0xFF);
        sum += word;
    }

    // ����λ�ӻص� 16 λ
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum & 0xFFFF;
}


// У�����֤
bool UDP_Packet::CheckValid() const {
    return (check & 0xFFFF) == Calculate_Checksum();
}

// ��ӡ��Ϣ
void UDP_Packet::Print_Message() const {
    std::cout << "UDP Packet:" << std::endl;
    std::cout << "  SrcPort: " << src_port << std::endl;
    std::cout << "  DestPort: " << dest_port << std::endl;
    std::cout << "  Seq: " << seq << std::endl;
    std::cout << "  Ack: " << ack << std::endl;
    std::cout << "  Length: " << length << std::endl;

    std::cout << "  Flag Details:" << std::endl;
    std::cout << "    CFH: " << (Is_CFH() ? "Set" : "Unset") << std::endl;
    std::cout << "    ACK: " << (Is_ACK() ? "Set" : "Unset") << std::endl;
    std::cout << "    SYN: " << (Is_SYN() ? "Set" : "Unset") << std::endl;
    std::cout << "    FIN: " << (Is_FIN() ? "Set" : "Unset") << std::endl;

    std::cout << "  Checksum: " << std::bitset<16>(flag & 0xFFFF) << std::endl;
}

