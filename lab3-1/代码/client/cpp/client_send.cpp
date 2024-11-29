#include "udp_packet.h" // 包含 UDP_Packet 的定义

#pragma comment(lib, "ws2_32.lib") // 链接 Windows Sockets 库

class UDPClient {
private:
    SOCKET clientSocket;           // 客户端套接字
    sockaddr_in clientAddr;        // 客户端地址
    sockaddr_in routerAddr;        // 目标路由地址
    uint32_t seq;                  // 客户端当前序列号

public:
    UDPClient() : clientSocket(INVALID_SOCKET), seq(0) {}

    bool init() {
        // 初始化 Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "[错误] WSAStartup 失败，错误代码: " << result << endl;
            return false;
        }

        // 检查版本是否匹配
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            cerr << "[错误] 不支持的 WinSock 版本。" << endl;
            WSACleanup();
            return false;
        }


        // 创建 UDP 套接字
        clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "[错误] 套接字创建失败，错误代码: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[日志] 套接字创建成功。" << endl;

        // 设置非阻塞模式
        u_long mode = 1;
        if (ioctlsocket(clientSocket, FIONBIO, &mode) != 0) {
            cerr << "[错误] 设置非阻塞模式失败，错误代码: " << WSAGetLastError() << endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }

        cout << "[日志] 套接字设置为非阻塞模式。" << endl;

        // 配置客户端地址
        memset(&clientAddr, 0, sizeof(clientAddr));
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(CLIENT_PORT);
        inet_pton(AF_INET, CLIENT_IP, &clientAddr.sin_addr);

        // 绑定客户端地址到套接字
        if (bind(clientSocket, (sockaddr*)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
            cerr << "[错误] 套接字绑定失败，错误代码: " << WSAGetLastError() << endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }

        cout << "[日志] 套接字绑定到本地地址: 端口 " << CLIENT_PORT << endl;

        // 配置目标路由地址
        memset(&routerAddr, 0, sizeof(routerAddr));
        routerAddr.sin_family = AF_INET;
        routerAddr.sin_port = htons(ROUTER_PORT);
        inet_pton(AF_INET, ROUTER_IP, &routerAddr.sin_addr);

        return true;
    }

    bool connect() {
        UDP_Packet con_msg[3]; // 三次握手消息

        // 第一次握手
        con_msg[0] = {}; // 清空结构体
        con_msg[0].src_port = CLIENT_PORT;
        con_msg[0].dest_port = ROUTER_PORT;
        con_msg[0].Set_SYN();                  // 设置 SYN 标志位
        con_msg[0].seq = ++seq;                // 设置序列号
        con_msg[0].check = con_msg[0].Calculate_Checksum(); // 计算校验和
        auto msg1_Send_Time = chrono::steady_clock::now(); // 记录发送时间

        cout << "[日志] 第一次握手：发送 SYN..." << endl;
        if (sendto(clientSocket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[错误] 第一次握手消息发送失败。" << endl;
            return false;
        }

        // 第二次握手
        socklen_t addr_len = sizeof(routerAddr);
        while (true) {
            // 接收 SYN+ACK 消息
            if (recvfrom(clientSocket, (char*)&con_msg[1], sizeof(con_msg[1]), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                if (con_msg[1].Is_ACK() && con_msg[1].Is_SYN() && con_msg[1].CheckValid() &&
                    con_msg[1].ack == con_msg[0].seq) {
                    cout << "[日志] 第二次握手成功：收到 SYN+ACK。" << endl;
                    break;
                }
                else {
                    cerr << "[错误] 第二次握手消息验证失败。" << endl;
                }
            }

            // 超时重传第一次握手消息
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - msg1_Send_Time).count() > TIMEOUT) {
                cout << "[日志] 超时，重传第一次握手消息。" << endl;
                con_msg[0].check = con_msg[0].Calculate_Checksum(); // 重新计算校验和
                if (sendto(clientSocket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
                    (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                    cerr << "[错误] 重传失败。" << endl;
                    return false;
                }
                msg1_Send_Time = now; // 更新发送时间
            }
        }
        seq = con_msg[1].seq;
        // 第三次握手
        con_msg[2] = {}; // 清空结构体
        con_msg[2].src_port = CLIENT_PORT;
        con_msg[2].dest_port = ROUTER_PORT;
        con_msg[2].seq =  seq + 1;           // 设置序列号
        con_msg[2].ack = con_msg[1].seq;  // 设置确认号
        con_msg[2].Set_ACK();             // 设置 ACK 标志位
        con_msg[2].check = con_msg[2].Calculate_Checksum(); // 计算校验和
        cout << "[日志] 第三次握手：发送 ACK..." << endl;
        if (sendto(clientSocket, (char*)&con_msg[2], sizeof(con_msg[2]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[错误] 第三次握手消息发送失败。" << endl;
            return false;
        }

        cout << "[日志] 三次握手完成，连接建立成功。" << endl;
        return true;
    }

    // 文件发送主函数
    bool Send_Message(const string& file_path) {
        // 打开文件
        ifstream file(file_path, ios::binary);
        if (!file.is_open()) {
            cerr << "[错误] 无法打开文件：" << file_path << endl;
            return false;
        }

        // 获取文件名和文件大小
        size_t pos = file_path.find_last_of("/\\");
        string file_name = (pos != string::npos) ? file_path.substr(pos + 1) : file_path;
        file.seekg(0, ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, ios::beg);

        cout << "[日志] 准备发送文件：" << file_name << "，大小：" << file_size << " 字节。" << endl;

        // 构造文件头信息
        UDP_Packet header_packet{};
        header_packet.src_port = CLIENT_PORT;
        header_packet.dest_port = ROUTER_PORT;
        header_packet.Set_CFH(); // 设置 CFH 标志位
        header_packet.seq = ++seq; // 设置序列号
        strncpy_s(header_packet.data, file_name.c_str(), MAX_DATA_SIZE - 1); // 写入文件名
        header_packet.length = file_size; // 写入文件大小
        header_packet.check = header_packet.Calculate_Checksum(); // 设置校验和

        // 发送文件头消息
        if (sendto(clientSocket, (char*)&header_packet, sizeof(header_packet), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[错误] 文件头信息发送失败，错误代码：" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[日志] 文件头信息已发送。" << endl;
        // 发送文件头信息，启用超时重传机制
        auto start_time = chrono::steady_clock::now();
        while (true) {

            // 接收 ACK 确认
            UDP_Packet ack_packet{};
            socklen_t addr_len = sizeof(routerAddr);
            if (recvfrom(clientSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                if (ack_packet.Is_ACK() && ack_packet.ack == header_packet.seq && ack_packet.CheckValid()) {
                    cout << "[日志] 收到文件头信息的 ACK 确认。" << endl;
                    break; // 成功接收确认，退出重传循环
                }
                else {
                    cerr << "[错误] 收到的 ACK 无效或不匹配。" << endl;
                }
            }

            // 检测超时
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cout << "[日志] 超时，重新发送文件头信息。" << endl;
                header_packet.check = header_packet.Calculate_Checksum(); // 重新计算校验和
                if (sendto(clientSocket, (char*)&header_packet, sizeof(header_packet), 0,
                    (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                    cerr << "[错误] 重传失败。" << endl;
                    return false;
                }
                start_time = now; // 更新发送时间
            }
        }

        // 文件头信息发送成功
        cout << "[日志] 文件头信息发送完成，准备发送文件数据。" << endl;

        // 开始发送文件内容
        size_t total_segments = file_size / MAX_DATA_SIZE;       // 完整数据段数量
        size_t last_segment_size = file_size % MAX_DATA_SIZE;    // 最后一个数据段的大小
        size_t total_sent = 0;                                   // 已发送字节总数
        size_t current_segment = 0;                             // 当前发送段编号

        auto file_start_time = chrono::steady_clock::now(); // 记录文件传输的开始时间

        double estimated_rtt = 100.0;  // 初始估计的 RTT
        double timeout_interval = 200.0;  // 初始超时时间
        double alpha = 0.125;  // 平滑因子
        double beta = 0.25;  // 平滑因子
        double dev_rtt = 0.0;  // RTT 偏差
  
        while (total_sent < file_size) {
            UDP_Packet data_packet{};
            data_packet.src_port = CLIENT_PORT;
            data_packet.dest_port = ROUTER_PORT;
            size_t segment_size = (current_segment < total_segments) ? MAX_DATA_SIZE : last_segment_size;

            file.read(data_packet.data, segment_size);
            data_packet.seq = ++seq;
            data_packet.length = segment_size;
            data_packet.check = data_packet.Calculate_Checksum();

            auto segment_send_time = chrono::steady_clock::now();
            bool ack_received = false;

            if (sendto(clientSocket, (char*)&data_packet, sizeof(data_packet), 0,
                (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                cerr << "[错误] 数据段发送失败，错误代码：" << WSAGetLastError() << endl;
                return false;
            }

            cout << "[日志] 数据段 " << current_segment + 1
                << " 已发送，大小：" << segment_size << " 字节。" << endl;

            while (!ack_received) {
                UDP_Packet ack_packet{};
                socklen_t addr_len = sizeof(routerAddr);
                if (recvfrom(clientSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
                    (sockaddr*)&routerAddr, &addr_len) > 0) {
                    if (ack_packet.Is_ACK() && ack_packet.ack == data_packet.seq && ack_packet.CheckValid()) {
                        ack_received = true;

                        // 动态调整 RTT 和超时时间
                        auto now = chrono::steady_clock::now();
                        double sample_rtt = chrono::duration<double, milli>(now - segment_send_time).count();
                        estimated_rtt = (1 - alpha) * estimated_rtt + alpha * sample_rtt;
                        dev_rtt = (1 - beta) * dev_rtt + beta * abs(sample_rtt - estimated_rtt);
                        timeout_interval = estimated_rtt + 4 * dev_rtt;

                        cout << "[日志] 收到 ACK，确认序列号：" << ack_packet.ack
                            << "，RTT: " << sample_rtt << " ms。" << endl;
                        cout << "[调试] 新的超时时间: " << timeout_interval << " ms，估计的 RTT: "
                            << estimated_rtt << " ms，RTT 偏差: " << dev_rtt << " ms。" << endl;

                        break;
                    }
                    else {
                        cerr << "[警告] 收到无效 ACK 或序列号不匹配。" << endl;
                    }
                }

                auto now = chrono::steady_clock::now();
                if (chrono::duration_cast<chrono::milliseconds>(now - segment_send_time).count() > timeout_interval) {
                    cout << "[日志] 数据段 " << current_segment + 1
                        << " 超时，重新发送。" << endl;
                    data_packet.check = data_packet.Calculate_Checksum();
                    if (sendto(clientSocket, (char*)&data_packet, sizeof(data_packet), 0,
                        (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                        cerr << "[错误] 重传失败。" << endl;
                        return false;
                    }
                    segment_send_time = now;
                }
            }

            total_sent += segment_size;
            current_segment++;
        }

        // 文件传输完成
        auto file_end_time = chrono::steady_clock::now();
        double total_time = chrono::duration<double>(file_end_time - file_start_time).count();
        double throughput = (total_sent / 1024.0) / total_time; // KB/s
        cout << "[日志] 文件传输完成，总耗时：" << total_time
            << " 秒，吞吐率：" << throughput << " KB/s。" << endl;

        file.close();
        return true;
    }


    bool Disconnect() {
        UDP_Packet wavehand_packets[4]; // 定义四次挥手消息数组
        socklen_t addr_len = sizeof(routerAddr);
        auto start_time = chrono::steady_clock::now();

        // 初始化挥手消息数组
        memset(wavehand_packets, 0, sizeof(wavehand_packets)); // 清零消息结构体数组

        // 第一次挥手: 发送 FIN 消息
        wavehand_packets[0].src_port = CLIENT_PORT;
        wavehand_packets[0].dest_port = ROUTER_PORT;
        wavehand_packets[0].Set_FIN();
        wavehand_packets[0].seq = ++seq;
        wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum();
        cout << "[日志] 第一次挥手：发送 FIN 消息，序列号：" << wavehand_packets[0].seq << endl;
        if (sendto(clientSocket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[错误] FIN 消息发送失败，错误代码：" << WSAGetLastError() << endl;
            return false;
        }
        while (true) {
            // 第二次挥手: 等待 ACK 消息
            if (recvfrom(clientSocket, (char*)&wavehand_packets[1], sizeof(wavehand_packets[1]), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                if (wavehand_packets[1].Is_ACK() &&
                    wavehand_packets[1].ack == wavehand_packets[0].seq &&
                    wavehand_packets[1].CheckValid()) {
                    cout << "[日志] 收到第二次挥手消息 (ACK)，确认序列号：" << wavehand_packets[1].ack << endl;
                    break;
                }
                else {
                    cerr << "[警告] 收到无效的 ACK 消息，丢弃。" << endl;
                }
            }

            // 超时重传第一次挥手消息
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cout << "[日志] FIN 消息超时，重新发送。" << endl;
                wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum(); // 重算校验和
                if (sendto(clientSocket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
                    (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                    cerr << "[错误] 重传失败。" << endl;
                    return false;
                }
                start_time = now; // 更新计时
            }
        }

        // 第三次挥手: 接收 FIN 消息
        start_time = chrono::steady_clock::now();
        while (true) {
            if (recvfrom(clientSocket, (char*)&wavehand_packets[2], sizeof(wavehand_packets[2]), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                cout << wavehand_packets[2].Is_FIN() << wavehand_packets[2].CheckValid();
                if (wavehand_packets[2].Is_FIN() && wavehand_packets[2].CheckValid()) {
                    cout << "[日志] 收到第三次挥手消息 (FIN)，序列号：" << wavehand_packets[2].seq << endl;
                    break;
                }
                else {
                    wavehand_packets[2].Print_Message();
                    cerr << "[警告] 收到无效的 FIN 消息，丢弃。" << endl;
                }
            }

            // 超时处理
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cerr << "[日志] 等待 FIN 超时，断开连接失败。" << endl;
                return false;
            }
        }
        seq = wavehand_packets[2].seq;
        // 第四次挥手: 发送 ACK 消息
        wavehand_packets[3].src_port = CLIENT_PORT;
        wavehand_packets[3].dest_port = ROUTER_PORT;
        wavehand_packets[3].Set_ACK();
        wavehand_packets[3].ack = wavehand_packets[2].seq;
        wavehand_packets[3].seq = ++seq;
        wavehand_packets[3].check = wavehand_packets[3].Calculate_Checksum();
        if (sendto(clientSocket, (char*)&wavehand_packets[3], sizeof(wavehand_packets[3]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[错误] 第四次挥手消息发送失败，错误代码：" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[日志] 第四次挥手：发送 ACK 消息，确认序列号：" << wavehand_packets[3].ack << endl;

        // 等待 2 * TIMEOUT 时间以确保消息完成
        cout << "[日志] 等待 2 * TIMEOUT 确保连接断开..." << endl;
        this_thread::sleep_for(chrono::milliseconds(2 * TIMEOUT));
        return true;
    }



    ~UDPClient() {
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
            WSACleanup();
            cout << "[日志] 套接字已关闭，资源已释放。" << endl;
        }
    }
};

int main() {
    UDPClient sender;
    if (!sender.init()) {
        cerr << "[错误] 发送端初始化失败。" << endl;
        return 0;
    }

    if (!sender.connect()) {
        cerr << "[错误] 三次握手失败，无法建立连接。" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "请选择操作：\n1. 发送文件\n2. 断开连接\n输入：";
        cin >> choice;

        if (choice == 1) {
            string file_path;
            cout << "请输入要发送的文件路径：";
            cin >> file_path;

            if (!sender.Send_Message(file_path)) {
                cerr << "[错误] 文件传输失败。" << endl;
            }
        }
        else if (choice == 2) {
            if (!sender.Disconnect()) {
                cerr << "[错误] 断开连接失败。" << endl;
            }
            else {
                cout << "[日志] 连接已成功断开。" << endl;
            }
        }
        else {
            cerr << "[警告] 无效输入，请重新输入。" << endl;
        }
    } while (choice != 2);
    return 0;
}
