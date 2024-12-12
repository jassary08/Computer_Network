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
        cerr << "[����] this ָ��Ϊ�գ��޷�����У��͡�" << endl;
        return 0;
    }
    if (data == nullptr) {
        cerr << "[����] ����ָ����Ч���޷�����У��͡�" << endl;
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
    cout << "UDP Packet:" << endl;
    cout << "  SrcPort: " << src_port << endl;
    cout << "  DestPort: " << dest_port << endl;
    cout << "  Seq: " << seq << endl;
    cout << "  Ack: " << ack << endl;
    cout << "  Length: " << length << endl;
    std::cout << "  check: " << check << std::endl;

    cout << "  Flag Details:" << endl;
    cout << "    CFH: " << (Is_CFH() ? "Set" : "Unset") << endl;
    cout << "    ACK: " << (Is_ACK() ? "Set" : "Unset") << endl;
    cout << "    SYN: " << (Is_SYN() ? "Set" : "Unset") << endl;
    cout << "    FIN: " << (Is_FIN() ? "Set" : "Unset") << endl;

    cout << "  Checksum: " << bitset<16>(flag & 0xFFFF) << endl;
}

