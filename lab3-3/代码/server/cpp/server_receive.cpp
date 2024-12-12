#include "udp_packet.h" // ���� UDP_Packet �Ķ���

#pragma comment(lib, "ws2_32.lib") // ���� Windows Sockets ��


atomic<int> Header_Seq(0);
atomic_int Sleep_Time(0);
float Last_Time = 0;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// �ļ�д�뻺����
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
    SOCKET serverSocket;           // ���������׽���
    sockaddr_in serverAddress;     // ��������ַ
    sockaddr_in routerAddress;     // ·������ַ
    socklen_t routerAddrLen;
    uint32_t seq;           // ��ǰ���к�
    uint32_t file_length;
    int Msg_Num;

public:
    UDPServer() : serverSocket(INVALID_SOCKET), seq(0), routerAddrLen(sizeof(routerAddress)), file_length(0), Msg_Num(0) {}

    bool init() {
        // ��ʼ�� Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "[����] WSAStartup ʧ�ܣ��������: " << result << endl;
            return false;
        }

        // ���汾�Ƿ�ƥ��
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            cerr << "[����] ��֧�ֵ� WinSock �汾��" << endl;
            WSACleanup();
            return false;
        }

        // ���� UDP �׽���
        serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            cerr << "[����] �׽��ִ���ʧ�ܣ��������: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[��־] �׽��ִ����ɹ���" << endl;

        // ���÷�����ģʽ
        u_long mode = 1;
        if (ioctlsocket(serverSocket, FIONBIO, &mode) != 0) {
            cerr << "[����] ���÷�����ģʽʧ�ܣ��������: " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        cout << "[��־] �׽�������Ϊ������ģʽ��" << endl;

        // ���÷�������ַ
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(SERVER_PORT);  // ʹ�ù̶��ķ������˿�
        inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr);

        // �󶨵�ַ���׽���
        if (bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
            cerr << "[����] �׽��ְ�ʧ�ܣ��������: " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        cout << "[��־] �������׽��ְ󶨵����ص�ַ: �˿� " << SERVER_PORT << endl;

        // ����Ŀ��·�ɵ�ַ
        memset(&routerAddress, 0, sizeof(routerAddress));
        routerAddress.sin_family = AF_INET;
        routerAddress.sin_port = htons(ROUTER_PORT);
        inet_pton(AF_INET, ROUTER_IP, &routerAddress.sin_addr);

        return true;
    }

    bool connect() {
        UDP_Packet handshakePackets[3]; // ����������Ϣ
        socklen_t routerAddrLen = sizeof(routerAddress);

        memset(handshakePackets, 0, sizeof(handshakePackets)); // ������Ϣ�ṹ��
        auto startTime = chrono::steady_clock::now();

        // ��һ�����֣����� SYN ��Ϣ
        while (true) {
            memset(&handshakePackets[0], 0, sizeof(handshakePackets[0])); // �����һ��������Ϣ
            if (recvfrom(serverSocket, (char*)&handshakePackets[0], sizeof(handshakePackets[0]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (handshakePackets[0].Is_SYN() && handshakePackets[0].CheckValid()) {
                    cout << "[��־] ��һ�����ֳɹ����յ� SYN ��Ϣ�����кţ�" << handshakePackets[0].seq << endl;
                    seq = handshakePackets[0].seq;
                    // ���õڶ������ֵ���Ϣ
                    handshakePackets[1].src_port = SERVER_PORT;   // �������˵Ķ˿�
                    handshakePackets[1].dest_port = ROUTER_PORT; // ·�����Ķ˿�
                    handshakePackets[1].seq = ++seq; // �������к�
                    handshakePackets[1].ack = handshakePackets[0].seq;     // ȷ�Ͽͻ������к�
                    handshakePackets[1].Set_SYN();
                    handshakePackets[1].Set_ACK();
                    if (sendto(serverSocket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                        (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[����] �ڶ���������Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[��־] �ڶ������֣����� SYN+ACK�����кţ�" << handshakePackets[1].seq
                        << "��ȷ�����кţ�" << handshakePackets[1].ack << endl;
                    break;
                }
                else {
                    cerr << "[����] �յ���Ч�� SYN ��Ϣ��������" << endl;
                }
            }
        }

        // �ȴ�������������Ϣ
        startTime = chrono::steady_clock::now();
        while (true) {
            if (recvfrom(serverSocket, (char*)&handshakePackets[2], sizeof(handshakePackets[2]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (handshakePackets[2].Is_ACK() &&
                    handshakePackets[2].ack == handshakePackets[1].seq &&
                    handshakePackets[2].CheckValid()) {
                    seq = handshakePackets[2].seq;
                    cout << "[��־] ���������ֳɹ����յ� ACK��ȷ�����кţ�" << handshakePackets[2].ack << endl;
                    return true; // ���ӽ����ɹ�
                }
                else {
                    cerr << "[����] �յ���Ч�� ACK ��Ϣ��������" << endl;
                }
            }

            // ��ⳬʱ���ش� SYN+ACK
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - startTime).count() > TIMEOUT) {
                cout << "[��־] �ȴ�������������Ϣ��ʱ�����·��� SYN+ACK��" << endl;

                handshakePackets[1].check = handshakePackets[1].Calculate_Checksum();
                if (sendto(serverSocket, (char*)&handshakePackets[1], sizeof(handshakePackets[1]), 0,
                    (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                    cerr << "[����] �ش� SYN+ACK ��Ϣʧ�ܣ�������룺" << WSAGetLastError() << endl;
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
                    cout << "[����] �ļ�ͷ��Ϣ��"
                        << "\n�ļ�����" << file_name
                        << "\n��С��" << formatFileSize(file_length) << endl;
                    SetConsoleTextAttribute(hConsole, 7);

                    // ����ȷ��
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

            // ��ʱ���
            if (chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start_time).count() > TIMEOUT) {
                SetConsoleTextAttribute(hConsole, 12);
                cout << "[��ʱ] �ȴ��ļ�ͷ��ʱ�������ش�" << endl;
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
            cout << "[�ش�] �����ظ�ACK�����кţ�" << seq << endl;
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
            // ����ȷ��
            UDP_Packet ack_packet;
            ack_packet.ack = packet.seq;
            ack_packet.Set_ACK();
            ack_packet.check = ack_packet.Calculate_Checksum();

            if (sendto(serverSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
                (SOCKADDR*)&routerAddress, sizeof(routerAddress)) > 0) {

                // д������
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

        // ��������ʾ
        cout << "\r���ս���: [";
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
        cout << "\n[���] �ļ��������"
            << "\n����λ�ã�" << file_path
            << "\n�ļ���С��" << formatFileSize(total_bytes)
            << "\n�ܺ�ʱ��" << fixed << setprecision(2) << duration << " ��"
            << "\nƽ���ٶȣ�" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    bool ReceiveMessage(const string& outputDir) {
        Header_Seq = seq;
        char file_name[MAX_DATA_SIZE] = {};
        UDP_Packet rec_msg;
        int Waiting_Seq = Header_Seq + 1;
        socklen_t routerAddrLen = sizeof(routerAddress);
        uint64_t total_received_bytes = 0;

        // �����ļ�ͷ
        if (!receiveFileHeader(file_name, rec_msg, Waiting_Seq, routerAddrLen)) {
            return false;
        }

        // ׼���ļ�д��
        string filePath = outputDir + "/" + string(file_name);
        BufferedFileWriter fileWriter(filePath, 1024 * 1024); // 1MB ������

        auto start_time = chrono::steady_clock::now();
        auto last_progress_update = start_time;

        cout << "\n[���տ�ʼ] ��ʼ�����ļ�����...\n" << endl;

        // ������ѭ��
        while (total_received_bytes < file_length) {
            UDP_Packet packet;
            auto receive_result = receivePacketWithTimeout(packet, routerAddrLen, TIMEOUT);

            if (handleReceivedPacket(packet, Waiting_Seq, fileWriter, total_received_bytes)) {
                // ���½�����ʾ
                auto current_time = chrono::steady_clock::now();
                if (chrono::duration_cast<chrono::milliseconds>(current_time - last_progress_update).count() >= 100) {
                    printReceiveProgress(total_received_bytes, file_length, start_time);
                    last_progress_update = current_time;
                }
            }

            // ����Ƿ���Ҫ����ϵ�������Ϣ
            if (total_received_bytes % (5 * 1024 * 1024) == 0) { // ÿ5MB����һ��
                saveCheckpoint(filePath, total_received_bytes);
            }
        }

        // ��ɽ��գ����ͳ����Ϣ
        fileWriter.flush();
        printReceiveStatistics(start_time, total_received_bytes, filePath);

        seq = Waiting_Seq - 1;
        return true;
    }



    bool Disconnect() {
        UDP_Packet finPackets[4];        // �Ĵλ�����Ϣ
        socklen_t routerAddrLen = sizeof(routerAddress);
        auto startTime = chrono::steady_clock::now();

        // ��ʼ��������Ϣ
        memset(finPackets, 0, sizeof(finPackets)); // ������Ϣ�ṹ������

        // ��һ�λ���: ���� FIN ��Ϣ
        while (true) {
            if (recvfrom(serverSocket, (char*)&finPackets[0], sizeof(finPackets[0]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (finPackets[0].Is_FIN() && finPackets[0].CheckValid()) {
                    cout << "[��־] �յ���һ�λ�����Ϣ (FIN)�����кţ�" << finPackets[0].seq << endl;
                    break;
                }
                else {
                    cerr << "[����] �յ���Ч�� FIN ��Ϣ��������" << endl;
                }
            }
        }
        seq = finPackets[0].seq;
        // �ڶ��λ���: ���� ACK ��Ϣ
        memset(&finPackets[2], 0, sizeof(finPackets[1]));
        finPackets[1].src_port = SERVER_PORT;
        finPackets[1].dest_port = ROUTER_PORT;
        finPackets[1].Set_ACK();
        finPackets[1].ack = finPackets[0].seq;
        finPackets[1].seq = ++seq;
        finPackets[1].check = finPackets[1].Calculate_Checksum();

        if (sendto(serverSocket, (char*)&finPackets[1], sizeof(finPackets[1]), 0,
            (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[����] �ڶ��λ�����Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[��־] �ڶ��λ��֣����� ACK��ȷ�����кţ�" << finPackets[1].ack << endl;
        seq = finPackets[1].seq;

        // �����λ���: ���� FIN ��Ϣ
        memset(&finPackets[2], 0, sizeof(finPackets[2]));
        finPackets[2].src_port = SERVER_PORT;
        finPackets[2].dest_port = ROUTER_PORT;
        finPackets[2].seq = ++seq;
        finPackets[2].ack = finPackets[1].seq;
        finPackets[2].Set_FIN();
        finPackets[2].Set_ACK();
        finPackets[2].check = finPackets[2].Calculate_Checksum();
        startTime = chrono::steady_clock::now();
        cout << "[��־] �����λ��֣����� FIN�����кţ�" << finPackets[2].seq << endl;

        if (sendto(serverSocket, (char*)&finPackets[2], sizeof(finPackets[2]), 0,
            (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
            cerr << "[����] �����λ�����Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }

        while (true) {
            // �ȴ����Ĵλ���: ���� ACK ��Ϣ
            if (recvfrom(serverSocket, (char*)&finPackets[3], sizeof(finPackets[3]), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (finPackets[3].Is_ACK() &&
                    finPackets[3].ack == finPackets[2].seq &&
                    finPackets[3].CheckValid()) {
                    cout << "[��־] �յ����Ĵλ�����Ϣ (ACK)��ȷ�����кţ�" << finPackets[3].ack << endl;
                    break;
                }
                else {
                    cerr << "[����] �յ���Ч�� ACK ��Ϣ��������" << endl;
                }
            }

            // ��ʱ�ش������λ�����Ϣ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - startTime).count() > TIMEOUT) {
                cout << "[��־] FIN ��ʱ�����·��͡�" << endl;
                finPackets[2].check = finPackets[2].Calculate_Checksum(); // ����У���
                if (sendto(serverSocket, (char*)&finPackets[2], sizeof(finPackets[2]), 0,
                    (sockaddr*)&routerAddress, sizeof(routerAddress)) == SOCKET_ERROR) {
                    cerr << "[����] �ش�ʧ�ܡ�" << endl;
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
            cout << "[��־] �׽����ѹرգ���Դ���ͷš�" << endl;
        }
    }
};

int main() {
    UDPServer receiver;
    if (!receiver.init()) {
        cerr << "[����] ���ն˳�ʼ��ʧ�ܡ�" << endl;
        return 0;
    }

    cout << "[��־] ��ʼ����ϣ��ȴ����Ͷ�����..." << endl;

    if (!receiver.connect()) {
        cerr << "[����] ��������ʧ�ܣ��޷��������ӡ�" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "��ѡ�������\n1. �����ļ�\n2. �Ͽ�����\n���룺";
        cin >> choice;

        if (choice == 1) {
            string output_dir;
            cout << "����������ļ������Ŀ¼��";
            cin >> output_dir;
            //output_dir = R"(F:\Desktop\Grade3)";

            cout << "[��־] ׼����ϣ��ȴ����Ͷ˴����ļ�..." << endl;
            if (!receiver.ReceiveMessage(output_dir)) {
                cerr << "[����] �ļ�����ʧ�ܡ�" << endl;
            }
        }
        else if (choice == 2) {
            cout << "[��־] ׼����ϣ��ȴ����Ͷ˽��в���..." << endl;
            if (!receiver.Disconnect()) {
                cerr << "[����] �Ͽ�����ʧ�ܡ�" << endl;
            }
            else {
                cout << "[��־] �����ѳɹ��Ͽ���" << endl;
            }
        }
        else {
            cerr << "[����] ��Ч���룬���������롣" << endl;
        }
    } while (choice != 2);

    return 0;
}
