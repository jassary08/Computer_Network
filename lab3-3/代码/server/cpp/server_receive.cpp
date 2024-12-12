#include "udp_packet.h" // 包含 UDP_Packet 的定义

#pragma comment(lib, "ws2_32.lib") // 链接 Windows Sockets 库


atomic<int> Header_Seq(0);
atomic_int Sleep_Time(0);
float Last_Time = 0;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// 文件写入缓冲类
class BufferedFileWriter {
private:
    ofstream file;
    vector<char> buffer;
    size_t current_pos;

public:
    BufferedFileWriter(const string& filename, size_t buffer_size)
        : buffer(buffer_size), current_pos(0) {
        file.open(filename, ios::binary);
    }

    void write(const char* data, size_t length) {
        while (length > 0) {
            size_t space = buffer.size() - current_pos;
            size_t to_write = min(space, length);

            memcpy(&buffer[current_pos], data, to_write);
            current_pos += to_write;
            data += to_write;
            length -= to_write;

            if (current_pos == buffer.size()) {
                flush();
            }
        }
    }

    void flush() {
        if (current_pos > 0) {
            file.write(buffer.data(), current_pos);
            current_pos = 0;
        }
    }

    ~BufferedFileWriter() {
        flush();
        file.close();
    }
};

class UDPServer {
private:
    SOCKET serverSocket;           // 服务器端套接字
    sockaddr_in serverAddress;     // 服务器地址
    sockaddr_in routerAddress;     // 路由器地址
    socklen_t routerAddrLen;
    uint32_t seq;           // 当前序列号
    uint32_t file_length;
    int Msg_Num;

public:
    UDPServer() : serverSocket(INVALID_SOCKET), seq(0), routerAddrLen(sizeof(routerAddress)), file_length(0), Msg_Num(0) {}

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
                    seq = handshakePackets[2].seq;
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

    string formatFileSize(uint64_t bytes) {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_index = 0;
        double size = bytes;

        while (size >= 1024 && unit_index < 4) {
            size /= 1024;
            unit_index++;
        }

        stringstream ss;
        ss << fixed << setprecision(2) << size << " " << units[unit_index];
        return ss.str();
    }

    bool receiveFileHeader(char* file_name, UDP_Packet& rec_msg, int& Waiting_Seq, socklen_t routerAddrLen) {
        auto start_time = chrono::steady_clock::now();

        while (true) {
            if (recvfrom(serverSocket, (char*)&rec_msg, sizeof(rec_msg), 0,
                (SOCKADDR*)&routerAddress, &routerAddrLen) > 0) {

                if (rec_msg.Is_CFH() && rec_msg.CheckValid() && rec_msg.seq == Waiting_Seq) {
                    file_length = rec_msg.length;
                    strcpy_s(file_name, MAX_DATA_SIZE, rec_msg.data);

                    SetConsoleTextAttribute(hConsole, 11);
                    cout << "[接收] 文件头信息："
                        << "\n文件名：" << file_name
                        << "\n大小：" << formatFileSize(file_length) << endl;
                    SetConsoleTextAttribute(hConsole, 7);

                    // 发送确认
                    UDP_Packet ack_packet;
                    ack_packet.ack = rec_msg.seq;
                    ack_packet.Set_ACK();
                    ack_packet.check = ack_packet.Calculate_Checksum();

                    if (sendto(serverSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
                        (SOCKADDR*)&routerAddress, routerAddrLen) > 0) {
                        Waiting_Seq++;
                        return true;
                    }
                }
                else if (rec_msg.Is_CFH() && rec_msg.CheckValid()) {
                    sendDuplicateAck(Waiting_Seq - 1);
                }
            }

            // 超时检查
            if (chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start_time).count() > TIMEOUT) {
                SetConsoleTextAttribute(hConsole, 12);
                cout << "[超时] 等待文件头超时，请求重传" << endl;
                SetConsoleTextAttribute(hConsole, 7);
                sendDuplicateAck(Waiting_Seq - 1);
                start_time = chrono::steady_clock::now();
            }
        }
    }

    void sendDuplicateAck(int seq) {
        UDP_Packet ack_packet;
        ack_packet.ack = seq;
        ack_packet.Set_ACK();
        ack_packet.check = ack_packet.Calculate_Checksum();

        if (sendto(serverSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
            (SOCKADDR*)&routerAddress, sizeof(routerAddress)) > 0) {
            SetConsoleTextAttribute(hConsole, 14);
            cout << "[重传] 发送重复ACK，序列号：" << seq << endl;
            SetConsoleTextAttribute(hConsole, 7);
        }
    }

    enum class ReceiveResult {
        Success,
        Timeout,
        Error
    };

    ReceiveResult receivePacketWithTimeout(UDP_Packet& packet, socklen_t routerAddrLen, int timeout_ms) {
        auto start_time = chrono::steady_clock::now();

        while (true) {
            if (recvfrom(serverSocket, (char*)&packet, sizeof(packet), 0,
                (SOCKADDR*)&routerAddress, &routerAddrLen) > 0) {
                return ReceiveResult::Success;
            }

            if (chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start_time).count() > timeout_ms) {
                return ReceiveResult::Timeout;
            }
        }
    }

    bool handleReceivedPacket(const UDP_Packet& packet, int& Waiting_Seq,
        BufferedFileWriter& writer, uint64_t& total_received) {

        if (packet.CheckValid() && packet.seq == Waiting_Seq) {
            // 发送确认
            UDP_Packet ack_packet;
            ack_packet.ack = packet.seq;
            ack_packet.Set_ACK();
            ack_packet.check = ack_packet.Calculate_Checksum();

            if (sendto(serverSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
                (SOCKADDR*)&routerAddress, sizeof(routerAddress)) > 0) {

                // 写入数据
                writer.write(packet.data, packet.length);
                total_received += packet.length;
                Waiting_Seq++;
                return true;
            }
        }
        else if (packet.CheckValid()) {
            sendDuplicateAck(Waiting_Seq - 1);
        }
        return false;
    }

    void saveCheckpoint(const string& file_path, uint64_t bytes_received) {
        string checkpoint_path = file_path + ".checkpoint";
        ofstream checkpoint(checkpoint_path);
        if (checkpoint.is_open()) {
            checkpoint << bytes_received;
            checkpoint.close();
        }
    }

    void printReceiveProgress(uint64_t received, uint64_t total,
        chrono::steady_clock::time_point start_time) {

        auto now = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(now - start_time).count();
        double speed = received / elapsed / 1024; // KB/s
        int percentage = static_cast<int>(received * 100.0 / total);

        // 进度条显示
        cout << "\r接收进度: [";
        for (int i = 0; i < 50; i++) {
            if (i < (percentage / 2)) cout << "=";
            else if (i == (percentage / 2)) cout << ">";
            else cout << " ";
        }
        cout << "] " << percentage << "% "
            << formatFileSize(received) << "/" << formatFileSize(total)
            << " (" << fixed << setprecision(2) << speed << " KB/s)    " << flush;
        cout << endl;
    }

    void printReceiveStatistics(chrono::steady_clock::time_point start_time,
        uint64_t total_bytes, const string& file_path) {

        auto end_time = chrono::steady_clock::now();
        double duration = chrono::duration<double>(end_time - start_time).count();
        double speed = (total_bytes / 1024.0) / duration; // KB/s

        SetConsoleTextAttribute(hConsole, 10);
        cout << "\n[完成] 文件接收完成"
            << "\n保存位置：" << file_path
            << "\n文件大小：" << formatFileSize(total_bytes)
            << "\n总耗时：" << fixed << setprecision(2) << duration << " 秒"
            << "\n平均速度：" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    bool ReceiveMessage(const string& outputDir) {
        Header_Seq = seq;
        char file_name[MAX_DATA_SIZE] = {};
        UDP_Packet rec_msg;
        int Waiting_Seq = Header_Seq + 1;
        socklen_t routerAddrLen = sizeof(routerAddress);
        uint64_t total_received_bytes = 0;

        // 接收文件头
        if (!receiveFileHeader(file_name, rec_msg, Waiting_Seq, routerAddrLen)) {
            return false;
        }

        // 准备文件写入
        string filePath = outputDir + "/" + string(file_name);
        BufferedFileWriter fileWriter(filePath, 1024 * 1024); // 1MB 缓冲区

        auto start_time = chrono::steady_clock::now();
        auto last_progress_update = start_time;

        cout << "\n[接收开始] 开始接收文件数据...\n" << endl;

        // 主接收循环
        while (total_received_bytes < file_length) {
            UDP_Packet packet;
            auto receive_result = receivePacketWithTimeout(packet, routerAddrLen, TIMEOUT);

            if (handleReceivedPacket(packet, Waiting_Seq, fileWriter, total_received_bytes)) {
                // 更新进度显示
                auto current_time = chrono::steady_clock::now();
                if (chrono::duration_cast<chrono::milliseconds>(current_time - last_progress_update).count() >= 100) {
                    printReceiveProgress(total_received_bytes, file_length, start_time);
                    last_progress_update = current_time;
                }
            }

            // 检查是否需要保存断点续传信息
            if (total_received_bytes % (5 * 1024 * 1024) == 0) { // 每5MB保存一次
                saveCheckpoint(filePath, total_received_bytes);
            }
        }

        // 完成接收，输出统计信息
        fileWriter.flush();
        printReceiveStatistics(start_time, total_received_bytes, filePath);

        seq = Waiting_Seq - 1;
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
            //output_dir = R"(F:\Desktop\Grade3)";

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
