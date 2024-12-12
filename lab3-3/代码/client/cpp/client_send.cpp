#include "udp_packet.h" 
#pragma comment(lib, "ws2_32.lib")

// 拥塞控制相关常量
const int INIT_CWND = 1;        // 初始拥塞窗口大小(MSS)
const int INIT_SSTHRESH = 16;   // 初始慢启动阈值
const int MAX_CWND = 32;        // 最大拥塞窗口

// 拥塞控制状态
enum CongestionState {
    SLOW_START,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY
};

//多线程变量定义
atomic_int Base_Seq(1);
atomic_int Next_Seq(1);
atomic_int Header_Seq(0);
atomic_int Count(0);
atomic_bool Resend(false);
atomic_bool Over(false);
mutex mtx;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

class UDPClient {
private:
    SOCKET clientSocket;
    sockaddr_in clientAddr;
    sockaddr_in routerAddr;
    uint32_t seq;
    int Msg_Num;
    uint32_t file_length;
    socklen_t addr_len = sizeof(routerAddr);

    // 拥塞控制相关变量
    double cwnd;                    // 拥塞窗口大小
    int ssthresh;                   // 慢启动阈值
    CongestionState congestion_state; // 当前拥塞状态
    int duplicate_ack_count;        // 重复ACK计数
    int last_ack;                   // 上一个收到的ACK

public:
    UDPClient() :
        clientSocket(INVALID_SOCKET),
        seq(0),
        Msg_Num(0),
        addr_len(sizeof(routerAddr)),
        cwnd(INIT_CWND),
        ssthresh(INIT_SSTHRESH),
        congestion_state(SLOW_START),
        duplicate_ack_count(0),
        last_ack(0) {}

    // 拥塞控制状态管理方法
    void enterSlowStart() {
        cwnd = INIT_CWND;
        ssthresh = INIT_SSTHRESH;
        congestion_state = SLOW_START;
        SetConsoleTextAttribute(hConsole, 14);
        cout << "[拥塞控制] 进入慢启动阶段" << endl;
        cout << "[拥塞控制] cwnd = " << cwnd << ", ssthresh = " << ssthresh << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    void enterCongestionAvoidance() {
        congestion_state = CONGESTION_AVOIDANCE;
        SetConsoleTextAttribute(hConsole, 14);
        cout << "[拥塞控制] 进入拥塞避免阶段" << endl;
        cout << "[拥塞控制] cwnd = " << cwnd << ", ssthresh = " << ssthresh << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    void enterFastRecovery() {
        ssthresh = max(cwnd / 2, (double)INIT_CWND);
        cwnd = ssthresh + 3;
        congestion_state = FAST_RECOVERY;
        SetConsoleTextAttribute(hConsole, 14);
        cout << "[拥塞控制] 进入快速恢复阶段" << endl;
        cout << "[拥塞控制] cwnd = " << cwnd << ", ssthresh = " << ssthresh << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    void handleTimeout() {
        ssthresh = max(cwnd / 2, (double)INIT_CWND);
        cwnd = INIT_CWND;
        congestion_state = SLOW_START;
        SetConsoleTextAttribute(hConsole, 12);
        cout << "[拥塞控制] 超时发生！重置拥塞窗口" << endl;
        cout << "[拥塞控制] cwnd = " << cwnd << ", ssthresh = " << ssthresh << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    void handleNewAck() {
        switch (congestion_state) {
        case SLOW_START:
            cwnd = 2 * cwnd;
            if (cwnd >= ssthresh) {
                enterCongestionAvoidance();
            }
            break;
        case CONGESTION_AVOIDANCE:
            cwnd += 1.0;
            break;
        case FAST_RECOVERY:
            cwnd += 1.0;
            congestion_state = CONGESTION_AVOIDANCE;
            break;
        }
        cwnd = min(cwnd, (double)MAX_CWND);
    }

    void handleDuplicateAck() {
        duplicate_ack_count++;
        if (duplicate_ack_count == 3) {
            enterFastRecovery();
            Resend = true;
        }
    }

    bool init() {
        // 初始化Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "[错误] WSAStartup 失败，错误代码: " << result << endl;
            return false;
        }

        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            cerr << "[错误] 不支持的 WinSock 版本。" << endl;
            WSACleanup();
            return false;
        }

        clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "[错误] 套接字创建失败，错误代码: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        // 设置非阻塞模式
        u_long mode = 1;
        if (ioctlsocket(clientSocket, FIONBIO, &mode) != 0) {
            cerr << "[错误] 设置非阻塞模式失败" << endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }

        // 配置客户端地址
        memset(&clientAddr, 0, sizeof(clientAddr));
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(CLIENT_PORT);
        inet_pton(AF_INET, CLIENT_IP, &clientAddr.sin_addr);

        if (bind(clientSocket, (sockaddr*)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
            cerr << "[错误] 套接字绑定失败" << endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }

        // 配置路由地址
        memset(&routerAddr, 0, sizeof(routerAddr));
        routerAddr.sin_family = AF_INET;
        routerAddr.sin_port = htons(ROUTER_PORT);
        inet_pton(AF_INET, ROUTER_IP, &routerAddr.sin_addr);

        // 初始化拥塞控制
        enterSlowStart();

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
        con_msg[2].seq = ++seq;           // 设置序列号
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

    void Reset() {
        Next_Seq = 1;
        Base_Seq = 1;
        Over = false;
        Resend = false;
        Header_Seq = 0;
        Msg_Num = 0;
        enterSlowStart();
        duplicate_ack_count = 0;
        last_ack = 0;
    }

    void Thread_Ack() {
        while (true) {
            UDP_Packet ack_msg;
            if (recvfrom(clientSocket, (char*)&ack_msg, sizeof(ack_msg), 0,
                (SOCKADDR*)&routerAddr, &addr_len)) {

                if (ack_msg.Is_ACK() && ack_msg.CheckValid()) {
                    lock_guard<mutex> lock(mtx);

                    cout << "[日志] 接收到确认消息，ACK=" << ack_msg.ack << endl;
                    if (ack_msg.ack % 15 == 0) {
                        continue;
                    }
                    // 拥塞控制处理
                    if (ack_msg.ack == last_ack) {
                        handleDuplicateAck();
                    }
                    else {
                        duplicate_ack_count = 0;
                        handleNewAck();
                        last_ack = ack_msg.ack;
                    }

                    // 更新发送窗口
                    if (ack_msg.ack >= Base_Seq + Header_Seq) {
                        int advance = ack_msg.ack - (Base_Seq + Header_Seq);
                        Base_Seq += advance + 1;
                    }

                    printWindowStatus();

                    if (ack_msg.ack - Header_Seq == Msg_Num + 1) {
                        Over = true;
                        return;
                    }
                }
            }
        }
    }

    bool sendFileHeader(UDP_Packet* data_msg, const string& file_name) {
        strcpy_s(data_msg[0].data, file_name.c_str());
        data_msg[0].data[strlen(data_msg[0].data)] = '\0';
        data_msg[0].length = file_length;
        data_msg[0].seq = ++seq;
        data_msg[0].Set_CFH();
        data_msg[0].src_port = CLIENT_PORT;
        data_msg[0].dest_port = ROUTER_PORT;
        data_msg[0].check = data_msg[0].Calculate_Checksum();

        SetConsoleTextAttribute(hConsole, 11); // 浅蓝色
        cout << "[发送] 文件头信息包：" << file_name
            << " (序列号: " << data_msg[0].seq << ")" << endl;
        SetConsoleTextAttribute(hConsole, 7);

        if (sendto(clientSocket, (char*)&data_msg[0], sizeof(data_msg[0]), 0,
            (SOCKADDR*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            SetConsoleTextAttribute(hConsole, 12); // 红色
            cerr << "[错误] 文件头发送失败，错误码：" << WSAGetLastError() << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return false;
        }
        return true;
    }

    bool sendFileData(UDP_Packet* data_msg, ifstream& file, int next_seq, int last_length) {
        // 读取文件数据
        if (next_seq == Msg_Num && last_length) {
            file.read(data_msg[next_seq - 1].data, last_length);
            data_msg[next_seq - 1].length = last_length;
        }
        else {
            file.read(data_msg[next_seq - 1].data, MAX_DATA_SIZE);
            data_msg[next_seq - 1].length = MAX_DATA_SIZE;
        }

        // 设置数据包属性
        data_msg[next_seq - 1].seq = ++seq;
        data_msg[next_seq - 1].src_port = CLIENT_PORT;
        data_msg[next_seq - 1].dest_port = ROUTER_PORT;
        data_msg[next_seq - 1].check = data_msg[next_seq - 1].Calculate_Checksum();
        if (seq % 20 != 0) {
            if (sendto(clientSocket, (char*)&data_msg[next_seq - 1], sizeof(data_msg[next_seq - 1]), 0,
                (SOCKADDR*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                SetConsoleTextAttribute(hConsole, 12);
                cerr << "[错误] 数据包发送失败，序列号：" << data_msg[next_seq - 1].seq
                    << "，错误码：" << WSAGetLastError() << endl;
                SetConsoleTextAttribute(hConsole, 7);
                return false;
            }
        }
        cout << "[日志] 成功发送数据包，SEQ 序列号： " << data_msg[next_seq - 1].seq << endl;
        return true;
    }

    void handleResend(UDP_Packet* data_msg) {
        SetConsoleTextAttribute(hConsole, 14);
        cout << "\n[重传] 开始重传未确认的数据包..." << endl;
        SetConsoleTextAttribute(hConsole, 7);

        for (int i = 0; i < Next_Seq - Base_Seq; i++) {
            lock_guard<mutex> lock(mtx);
            int resend_seq = Base_Seq + i - 1;
            data_msg[resend_seq].check = data_msg[resend_seq].Calculate_Checksum();

            if (sendto(clientSocket, (char*)&data_msg[resend_seq], sizeof(data_msg[resend_seq]), 0,
                (SOCKADDR*)&routerAddr, sizeof(routerAddr)) != SOCKET_ERROR) {
                SetConsoleTextAttribute(hConsole, 14);
                cout << "[重传] 数据包重传成功，序列号：" << resend_seq + Header_Seq + 1 << endl;
                SetConsoleTextAttribute(hConsole, 7);
            }
        }
        Resend = false;
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

    void printWindowStatus() {
        static int last_base = -1;
        static int last_next = -1;
        if (last_base != Base_Seq || last_next != Next_Seq) {
            SetConsoleTextAttribute(hConsole, 11);
            cout << "[窗口] Base: " << Base_Seq
                << " Next: " << Next_Seq
                << " 未确认: " << Next_Seq - Base_Seq
                << " 拥塞窗口大小: " << cwnd
                << " 窗口剩余空间: " << cwnd - (Next_Seq - Base_Seq)
                << endl;
            SetConsoleTextAttribute(hConsole, 7);
            last_base = Base_Seq;
            last_next = Next_Seq;
        }
    }

    void printTransferProgress(uint64_t transferred, uint64_t total, chrono::steady_clock::time_point start_time) {
        auto now = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(now - start_time).count();
        double speed = (transferred - total) / elapsed / 1024; // KB/s
        int percentage = (int)((transferred - total) * 100.0 / total);

        // 进度条宽度
        const int bar_width = 50;
        int filled = bar_width * percentage / 100;

        cout << "\r[进度] [";
        for (int i = 0; i < bar_width; ++i) {
            if (i < filled) cout << "=";
            else if (i == filled) cout << ">";
            else cout << " ";
        }
        cout << "] " << percentage << "% "
            << formatFileSize(transferred - total) << "/" << formatFileSize(total)
            << " (" << fixed << setprecision(2) << speed << " KB/s)    " << flush;
        cout << endl;
    }

    void printTransferStatistics(chrono::steady_clock::time_point start_time, uint32_t total_bytes) {
        auto end_time = chrono::steady_clock::now();
        double duration = chrono::duration<double>(end_time - start_time).count();
        double speed = (total_bytes / 1024.0) / duration; // KB/s

        SetConsoleTextAttribute(hConsole, 10); // 绿色
        cout << "\n[完成] 文件传输完成"
            << "\n总大小：" << formatFileSize(total_bytes)
            << "\n耗时：" << fixed << setprecision(2) << duration << " 秒"
            << "\n平均速度：" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    bool Send_Message(string file_path) {
        ifstream file(file_path, ios::binary);
        if (!file.is_open()) {
            cerr << "[错误] 无法打开文件：" << file_path << endl;
            return false;
        }

        // 获取文件信息
        size_t pos = file_path.find_last_of("/\\");
        string file_name = (pos != string::npos) ? file_path.substr(pos + 1) : file_path;
        file.seekg(0, ios::end);
        file_length = file.tellg();
        file.seekg(0, ios::beg);

        int complete_num = file_length / MAX_DATA_SIZE;
        int last_length = file_length % MAX_DATA_SIZE;
        Header_Seq = seq;
        Msg_Num = complete_num + (last_length != 0);

        thread ackThread([this]() { this->Thread_Ack(); });
        unique_ptr<UDP_Packet[]> data_msg(new UDP_Packet[Msg_Num + 1]);
        auto start_time = chrono::steady_clock::now();
        uint64_t total_sent_bytes = 0;

        while (!Over) {
            if (Resend) {
                handleResend(data_msg.get());
                continue;
            }

            // 考虑拥塞窗口大小
            int effective_window = min((int)cwnd, MAX_CWND);
            if (Next_Seq < Base_Seq + effective_window && Next_Seq <= Msg_Num + 1) {
                lock_guard<mutex> lock(mtx);
                if (Next_Seq == 1) {
                    if (!sendFileHeader(data_msg.get(), file_name)) {
                        return false;
                    }
                }
                else {
                    if (!sendFileData(data_msg.get(), file, Next_Seq, last_length)) {
                        return false;
                    }
                }

                total_sent_bytes += data_msg[Next_Seq - 1].length;
                printTransferProgress(total_sent_bytes, file_length, start_time);
                Next_Seq++;
            }

            // 流控制和拥塞控制
            if (Next_Seq - Base_Seq > effective_window * 0.8) {
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        }

        ackThread.join();
        printTransferStatistics(start_time, file_length);
        Reset();
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
        cout << "\n请选择操作：\n1. 发送文件\n2. 断开连接\n输入：";
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