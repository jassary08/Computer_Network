#define UDP_PACKET_H

#include <iostream>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <winsock2.h>
#include <unordered_map>
#include <fstream>
#include <ws2tcpip.h>
#include <chrono>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <sstream> 
#include <thread>
#include <deque>

using namespace std;

#define MAX_DATA_SIZE 10240  // 数据部分的最大大小

#define TIMEOUT 1000 // 超时时间（毫秒）

#define SERVER_PORT 23456          // 服务器端端口
#define ROUTER_PORT 12345         // 目标路由端口
#define SERVER_IP "127.0.0.1"      // 服务器 IP 地址
#define ROUTER_IP "127.0.0.1"      // 目标路由 IP 地址

// UDP 数据报结构
struct UDP_Packet {
    uint32_t src_port;       // 源端口
    uint32_t dest_port;      // 目标端口
    uint32_t seq;            // 序列号
    uint32_t ack;            // 确认号
    uint32_t length;         // 数据长度（包括头部和数据）
    uint16_t flag;     // 标志位
    uint16_t check;          // 校验和
    char data[MAX_DATA_SIZE]; // 数据部分


    // 标志位掩码
    static constexpr uint16_t FLAG_FIN = 0x8000;  // FIN 位
    static constexpr uint16_t FLAG_CFH = 0x4000;  // CFH 位
    static constexpr uint16_t FLAG_ACK = 0x2000;  // ACK 位
    static constexpr uint16_t FLAG_SYN = 0x1000;  // SYN 位

    UDP_Packet() : src_port(0), dest_port(0), seq(0), ack(0), length(0), flag(0), check(0) {
        memset(data, 0, MAX_DATA_SIZE); // 将数据部分初始化为 0
    }

    // 设置标志位
    void Set_CFH();
    bool Is_CFH() const;

    void Set_ACK();
    bool Is_ACK() const;

    void Set_SYN();
    bool Is_SYN() const;

    void Set_FIN();
    bool Is_FIN() const;

    // 校验和计算
    uint16_t Calculate_Checksum() const;

    // 校验和验证
    bool CheckValid() const;

    // 打印消息
    void Print_Message() const;
};

