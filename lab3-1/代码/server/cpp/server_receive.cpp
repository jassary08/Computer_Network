#include "udp_packet.h" // 包含 UDP_Packet 的定义

#pragma comment(lib, "ws2_32.lib") // 链接 Windows Sockets 库

class UDPServer {
private:
    SOCKET serverSocket;           // 服务器端套接字
    sockaddr_in serverAddress;     // 服务器地址
    sockaddr_in routerAddress;     // 路由器地址
    uint32_t seq;           // 当前序列号

public:
    UDPServer() : serverSocket(INVALID_SOCKET), seq(0) {}

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
        serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            cerr << "[错误] 套接字创建失败，错误代码: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[日志] 套接字创建成功。" << endl;

        // 设置非阻塞模式
        u_long mode = 1;
        if (ioctlsocket(serverSocket, FIONBIO, &mode) != 0) {
            cerr << "[错误] 设置非阻塞模式失败，错误代码: " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        cout << "[日志] 套接字设置为非阻塞模式。" << endl;

        // 配置服务器地址
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(SERVER_PORT);  // 使用固定的服务器端口
        inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr);

        // 绑定地址到套接字
        if (bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
            cerr << "[错误] 套接字绑定失败，错误代码: " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        cout << "[日志] 服务器套接字绑定到本地地址: 端口 " << SERVER_PORT << endl;

        // 配置目标路由地址
        memset(&routerAddress, 0, sizeof(routerAddress));
        routerAddress.sin_family = AF_INET;
        routerAddress.sin_port = htons(ROUTER_PORT);
        inet_pton(AF_INET, ROUTER_IP, &routerAddress.sin_addr);

        return true;
    }

    bool connect() {
        UDP_Packet handshakePackets[3]; // 三次握手消息
        socklen_t routerAddrLen = sizeof(routerAddress);

        memset(handshakePackets, 0, sizeof(handshakePackets)); // 清零消息结构体
        auto startTime = chrono::steady_clock::now();

        // 第一次握手：接收 SYN 消息
        while (true) {
            memset(&handshakePackets[0], 0, sizeof(handshakePackets[0])); // 清零第一次握手消息
            if (recvfrom(serverSocket, (char*)&handshakePackets[0], sizeof(handshakePackets[0]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (handshakePackets[0].Is_SYN() && handshakePackets[0].CheckValid()) {
                    cout << "[日志] 第一次握手成功：收到 SYN 消息，序列号：" << handshakePackets[0].seq << endl;
                    seq = handshakePackets[0].seq;
                    // 设置第二次握手的消息
                    handshakePackets[1].src_port = SERVER_PORT;   // 服务器端的端口
                    handshakePackets[1].dest_port = ROUTER_PORT; // 路由器的端口
                    handshakePackets[1].seq = ++seq; // 增加序列号
                    handshakePackets[1].ack = handshakePackets[0].seq;     // 确认客户端序列号
                    handshakePackets[1].Set_SYN();
                    handshakePackets[1].Set_ACK();
                    handshakePackets[1].check = handshakePackets[1].Calculate_Checksum();

                    if (sendto(serverSocket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                        (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[错误] 第二次握手消息发送失败，错误代码：" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[日志] 第二次握手：发送 SYN+ACK，序列号：" << handshakePackets[1].seq
                        << "，确认序列号：" << handshakePackets[1].ack << endl;
                    break;
                }
                else {
                    cerr << "[警告] 收到无效的 SYN 消息，丢弃。" << endl;
                }
            }
        }

        // 等待第三次握手消息
        startTime = chrono::steady_clock::now();
        while (true) {
            if (recvfrom(serverSocket, (char*)&handshakePackets[2], sizeof(handshakePackets[2]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (handshakePackets[2].Is_ACK() &&
                    handshakePackets[2].ack == handshakePackets[1].seq &&
                    handshakePackets[2].CheckValid()) {
                    cout << "[日志] 第三次握手成功：收到 ACK，确认序列号：" << handshakePackets[2].ack << endl;
                    return true; // 连接建立成功
                }
                else {
                    cerr << "[警告] 收到无效的 ACK 消息，丢弃。" << endl;
                }
            }

            // 检测超时并重传 SYN+ACK
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - startTime).count() > TIMEOUT) {
                cout << "[日志] 等待第三次握手消息超时，重新发送 SYN+ACK。" << endl;

                handshakePackets[1].check = handshakePackets[1].Calculate_Checksum();
                if (sendto(serverSocket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                    (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                    cerr << "[错误] 重传 SYN+ACK 消息失败，错误代码：" << WSAGetLastError() << endl;
                    return false;
                }
                startTime = now;
            }
        }
    }



    bool ReceiveMessage(const string& outputDir) {
        UDP_Packet headerPacket{};         // 文件头部包
        socklen_t routerAddrLen = sizeof(routerAddress);

        // 接收文件头部信息
        while (true) {
            if (recvfrom(serverSocket, (char*)&headerPacket, sizeof(headerPacket), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (headerPacket.Is_CFH() && headerPacket.CheckValid()) {
                    cout << "[日志] 收到文件头部信息。" << endl;
                    cout << "[日志] 文件名：" << headerPacket.data
                        << "，文件大小：" << headerPacket.length << " 字节。" << endl;
                    // 发送确认报文
                    UDP_Packet ackPacket{};
                    ackPacket.src_port = SERVER_PORT;   // 服务器端的端口
                    ackPacket.dest_port = ROUTER_PORT; // 路由器的端口
                    ackPacket.Set_ACK();
                    ackPacket.seq = ++seq;
                    ackPacket.ack = headerPacket.seq;
                    ackPacket.check = ackPacket.Calculate_Checksum();
                    if (sendto(serverSocket, (char*)&ackPacket, sizeof(ackPacket), 0,
                        (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[错误] ACK 发送失败，错误代码：" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[日志] 已发送文件头部确认报文 (ACK)。" << endl;
                    break;
                }
                else {
                    cerr << "[警告] 收到的文件头部信息无效，丢弃。" << endl;
                }
            }
        }

        // 打开文件准备写入
        string filePath = outputDir + "/" + string(headerPacket.data);
        ofstream file(filePath, ios::binary);
        if (!file.is_open()) {
            cerr << "[错误] 无法打开文件进行写入：" << filePath << endl;
            return false;
        }
        cout << "[日志] 文件已打开，准备接收数据写入：" << filePath << endl;

        size_t totalReceived = 0;
        size_t totalSize = headerPacket.length;       // 总大小
        uint32_t expectedSeq = headerPacket.seq + 1; // 下一个期望的序列号

        // 循环接收文件数据
        while (totalReceived < totalSize) {
            UDP_Packet dataPacket{};
            if (recvfrom(serverSocket, (char*)&dataPacket, sizeof(dataPacket), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                // 校验数据包
                if (dataPacket.CheckValid() && dataPacket.seq == expectedSeq) {
                    file.write(dataPacket.data, dataPacket.length); // 写入文件
                    totalReceived += dataPacket.length;
                    expectedSeq++;

                    cout << "[日志] 数据段接收成功，序列号：" << dataPacket.seq
                        << "，大小：" << dataPacket.length << " 字节。" << endl;

                    // 发送 ACK 确认
                    seq = dataPacket.seq;
                    UDP_Packet ackPacket{};
                    memset(&ackPacket, 0, sizeof(ackPacket)); // 清零 ACK 消息
                    ackPacket.src_port = SERVER_PORT;   // 设置端口
                    ackPacket.dest_port = ROUTER_PORT;
                    ackPacket.Set_ACK();
                    ackPacket.seq = ++seq;
                    ackPacket.ack = dataPacket.seq;
                    ackPacket.check = ackPacket.Calculate_Checksum();

                    if (sendto(serverSocket, (char*)&ackPacket, sizeof(ackPacket), 0,
                        (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[错误] ACK 发送失败，错误代码：" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[日志] 已发送数据段确认报文 (ACK)，确认序列号：" << ackPacket.ack << endl;
                }
                else {
                    cerr << "[警告] 收到无效数据段，丢弃。" << endl;
                }
            }
        }

        file.close();
        cout << "[日志] 文件接收完成，总接收字节数：" << totalReceived
            << "，文件已保存至：" << filePath << endl;
        return true;
    }

    bool Disconnect() {
        UDP_Packet finPackets[4];        // 四次挥手消息
        socklen_t routerAddrLen = sizeof(routerAddress);
        auto startTime = chrono::steady_clock::now();

        // 初始化挥手消息
        memset(finPackets, 0, sizeof(finPackets)); // 清零消息结构体数组

        // 第一次挥手: 接收 FIN 消息
        while (true) {
            if (recvfrom(serverSocket, (char*)&finPackets[0], sizeof(finPackets[0]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (finPackets[0].Is_FIN() && finPackets[0].CheckValid()) {
                    cout << "[日志] 收到第一次挥手消息 (FIN)，序列号：" << finPackets[0].seq << endl;
                    break;
                }
                else {
                    cerr << "[警告] 收到无效的 FIN 消息，丢弃。" << endl;
                }
            }
        }
        seq = finPackets[0].seq;
        // 第二次挥手: 发送 ACK 消息
        memset(&finPackets[2], 0, sizeof(finPackets[1]));
        finPackets[1].src_port = SERVER_PORT;
        finPackets[1].dest_port = ROUTER_PORT;
        finPackets[1].Set_ACK();
        finPackets[1].ack = finPackets[0].seq;
        finPackets[1].seq = ++seq;
        finPackets[1].check = finPackets[1].Calculate_Checksum();

        if (sendto(serverSocket, (char*)&finPackets[1], sizeof(finPackets[1]), 0,
            (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[错误] 第二次挥手消息发送失败，错误代码：" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[日志] 第二次挥手：发送 ACK，确认序列号：" << finPackets[1].ack << endl;
        seq = finPackets[1].seq;

        // 第三次挥手: 发送 FIN 消息
        memset(&finPackets[2], 0, sizeof(finPackets[2]));
        finPackets[2].src_port = SERVER_PORT;
        finPackets[2].dest_port = ROUTER_PORT;
        finPackets[2].seq = ++seq;
        finPackets[2].ack = finPackets[1].seq;
        finPackets[2].Set_FIN();
        finPackets[2].Set_ACK();
        finPackets[2].check = finPackets[2].Calculate_Checksum();
        startTime = chrono::steady_clock::now();
        cout << "[日志] 第三次挥手：发送 FIN，序列号：" << finPackets[2].seq << endl;

        if (sendto(serverSocket, (char*)&finPackets[2], sizeof(finPackets[2]), 0,
            (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[错误] 第三次挥手消息发送失败，错误代码：" << WSAGetLastError() << endl;
            return false;
        }

        while (true) {
            // 等待第四次挥手: 接收 ACK 消息
            if (recvfrom(serverSocket, (char*)&finPackets[3], sizeof(finPackets[3]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (finPackets[3].Is_ACK() &&
                    finPackets[3].ack == finPackets[2].seq &&
                    finPackets[3].CheckValid()) {
                    cout << "[日志] 收到第四次挥手消息 (ACK)，确认序列号：" << finPackets[3].ack << endl;
                    break;
                }
                else {
                    cerr << "[警告] 收到无效的 ACK 消息，丢弃。" << endl;
                }
            }

            // 超时重传第三次挥手消息
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - startTime).count() > TIMEOUT) {
                cout << "[日志] FIN 超时，重新发送。" << endl;
                finPackets[2].check = finPackets[2].Calculate_Checksum(); // 重算校验和
                if (sendto(serverSocket, (char*)&finPackets[2], sizeof(finPackets[2]), 0,
                    (sockaddr*)&routerAddress, sizeof(routerAddress)) == SOCKET_ERROR) {
                    cerr << "[错误] 重传失败。" << endl;
                    return false;
                }
                startTime = now;
            }
        }

        return true;
    }


    ~UDPServer() {
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
            WSACleanup();
            cout << "[日志] 套接字已关闭，资源已释放。" << endl;
        }
    }
};

int main() {
    UDPServer receiver;
    if (!receiver.init()) {
        cerr << "[错误] 接收端初始化失败。" << endl;
        return 0;
    }

    cout << "[日志] 初始化完毕，等待发送端连接..." << endl;

    if (!receiver.connect()) {
        cerr << "[错误] 三次握手失败，无法建立连接。" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "请选择操作：\n1. 接收文件\n2. 断开连接\n输入：";
        cin >> choice;

        if (choice == 1) {
            string output_dir;
            cout << "请输入接收文件保存的目录：";
            cin >> output_dir;

            cout << "[日志] 准备完毕，等待发送端传输文件..." << endl;
            if (!receiver.ReceiveMessage(output_dir)) {
                cerr << "[错误] 文件接收失败。" << endl;
            }
        }
        else if (choice == 2) {
            cout << "[日志] 准备完毕，等待发送端进行操作..." << endl;
            if (!receiver.Disconnect()) {
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
