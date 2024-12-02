#define UDP_PACKET_H

#include <iostream>
#include <vector>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <winsock2.h>
#include <fstream>
#include <thread>
#include <ws2tcpip.h>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <sstream> 
#include <mutex>
#include <unordered_map>
#include <thread>

using namespace std;

#define MAX_DATA_SIZE 10240  // ���ݲ��ֵ�����С

#define TIMEOUT 1000 // ��ʱʱ�䣨���룩

#define CLIENT_PORT 54321          // �ͻ��˶˿�
#define ROUTER_PORT 12345        // Ŀ��·�ɶ˿�
#define CLIENT_IP "127.0.0.1"      // Ŀ��·�� IP ��ַ
#define ROUTER_IP "127.0.0.1"      // Ŀ��·�� IP ��ַ
#define Windows_Size 5

// UDP ���ݱ��ṹ
struct UDP_Packet {
    uint32_t src_port;       // Դ�˿�
    uint32_t dest_port;      // Ŀ��˿�
    uint32_t seq;            // ���к�
    uint32_t ack;            // ȷ�Ϻ�
    uint32_t length;         // ���ݳ��ȣ�����ͷ�������ݣ�
    uint16_t flag;     // ��־λ
    uint16_t check;          // У���
    char data[MAX_DATA_SIZE]; // ���ݲ���


    // ��־λ����
    static constexpr uint16_t FLAG_FIN = 0x8000;  // FIN λ
    static constexpr uint16_t FLAG_CFH = 0x4000;  // CFH λ
    static constexpr uint16_t FLAG_ACK = 0x2000;  // ACK λ
    static constexpr uint16_t FLAG_SYN = 0x1000;  // SYN λ

    UDP_Packet() : src_port(0), dest_port(0), seq(0), ack(0), length(0), flag(0), check(0) {
        memset(data, 0, MAX_DATA_SIZE); // �����ݲ��ֳ�ʼ��Ϊ 0
    }

    // ���ñ�־λ
    void Set_CFH();
    bool Is_CFH() const;

    void Set_ACK();
    bool Is_ACK() const;

    void Set_SYN();
    bool Is_SYN() const;

    void Set_FIN();
    bool Is_FIN() const;

    // У��ͼ���
    uint16_t Calculate_Checksum() const;

    // У�����֤
    bool CheckValid() const;

    // ��ӡ��Ϣ
    void Print_Message() const;
};


