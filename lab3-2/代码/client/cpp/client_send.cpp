#include "udp_packet.h" // ���� UDP_Packet �Ķ���

#pragma comment(lib, "ws2_32.lib") // ���� Windows Sockets ��

//���̱߳�������
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
    SOCKET clientSocket;           // �ͻ����׽���
    sockaddr_in clientAddr;        // �ͻ��˵�ַ
    sockaddr_in routerAddr;        // Ŀ��·�ɵ�ַ
    uint32_t seq;                  // �ͻ��˵�ǰ���к�
    int Msg_Num;                   //������Ϣ����
    uint32_t file_length;
    socklen_t addr_len = sizeof(routerAddr);

public:
    UDPClient() : clientSocket(INVALID_SOCKET), seq(0), Msg_Num(0), addr_len(sizeof(routerAddr)) {}

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
        clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "[����] �׽��ִ���ʧ�ܣ��������: " << WSAGetLastError() << endl;
            WSACleanup();
            return false;
        }

        cout << "[��־] �׽��ִ����ɹ���" << endl;

        // ���÷�����ģʽ
        u_long mode = 1;
        if (ioctlsocket(clientSocket, FIONBIO, &mode) != 0) {
            cerr << "[����] ���÷�����ģʽʧ�ܣ��������: " << WSAGetLastError() << endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }

        cout << "[��־] �׽�������Ϊ������ģʽ��" << endl;

        // ���ÿͻ��˵�ַ
        memset(&clientAddr, 0, sizeof(clientAddr));
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(CLIENT_PORT);
        inet_pton(AF_INET, CLIENT_IP, &clientAddr.sin_addr);

        // �󶨿ͻ��˵�ַ���׽���
        if (bind(clientSocket, (sockaddr*)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
            cerr << "[����] �׽��ְ�ʧ�ܣ��������: " << WSAGetLastError() << endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }

        cout << "[��־] �׽��ְ󶨵����ص�ַ: �˿� " << CLIENT_PORT << endl;

        // ����Ŀ��·�ɵ�ַ
        memset(&routerAddr, 0, sizeof(routerAddr));
        routerAddr.sin_family = AF_INET;
        routerAddr.sin_port = htons(ROUTER_PORT);
        inet_pton(AF_INET, ROUTER_IP, &routerAddr.sin_addr);

        return true;
    }

    bool connect() {
        UDP_Packet con_msg[3]; // ����������Ϣ

        // ��һ������
        con_msg[0] = {}; // ��սṹ��
        con_msg[0].src_port = CLIENT_PORT;
        con_msg[0].dest_port = ROUTER_PORT;
        con_msg[0].Set_SYN();                  // ���� SYN ��־λ
        con_msg[0].seq = ++seq;                // �������к�
        con_msg[0].check = con_msg[0].Calculate_Checksum(); // ����У���
        auto msg1_Send_Time = chrono::steady_clock::now(); // ��¼����ʱ��

        cout << "[��־] ��һ�����֣����� SYN..." << endl;
        if (sendto(clientSocket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[����] ��һ��������Ϣ����ʧ�ܡ�" << endl;
            return false;
        }

        // �ڶ�������
        socklen_t addr_len = sizeof(routerAddr);
        while (true) {
            // ���� SYN+ACK ��Ϣ
            if (recvfrom(clientSocket, (char*)&con_msg[1], sizeof(con_msg[1]), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                if (con_msg[1].Is_ACK() && con_msg[1].Is_SYN() && con_msg[1].CheckValid() &&
                    con_msg[1].ack == con_msg[0].seq) {
                    cout << "[��־] �ڶ������ֳɹ����յ� SYN+ACK��" << endl;
                    break;
                }
                else {
                    cerr << "[����] �ڶ���������Ϣ��֤ʧ�ܡ�" << endl;
                }
            }

            // ��ʱ�ش���һ��������Ϣ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - msg1_Send_Time).count() > TIMEOUT) {
                cout << "[��־] ��ʱ���ش���һ��������Ϣ��" << endl;
                con_msg[0].check = con_msg[0].Calculate_Checksum(); // ���¼���У���
                if (sendto(clientSocket, (char*)&con_msg[0], sizeof(con_msg[0]), 0,
                    (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                    cerr << "[����] �ش�ʧ�ܡ�" << endl;
                    return false;
                }
                msg1_Send_Time = now; // ���·���ʱ��
            }
        }
        seq = con_msg[1].seq;
        // ����������
        con_msg[2] = {}; // ��սṹ��
        con_msg[2].src_port = CLIENT_PORT;
        con_msg[2].dest_port = ROUTER_PORT;
        con_msg[2].seq = ++seq;           // �������к�
        con_msg[2].ack = con_msg[1].seq;  // ����ȷ�Ϻ�
        con_msg[2].Set_ACK();             // ���� ACK ��־λ
        con_msg[2].check = con_msg[2].Calculate_Checksum(); // ����У���
        cout << "[��־] ���������֣����� ACK..." << endl;
        if (sendto(clientSocket, (char*)&con_msg[2], sizeof(con_msg[2]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[����] ������������Ϣ����ʧ�ܡ�" << endl;
            return false;
        }
        cout << "[��־] ����������ɣ����ӽ����ɹ���" << endl;
        return true;
    }

    void Reset() {
        Next_Seq = 1;
        Base_Seq = 1;
        Over = false;
        Resend = false;
        Header_Seq = 0;
        Msg_Num = 0;
    }

    void Thread_Ack() {
        int Err_ack_Num = 0;
        int resend_threshold = 3;  // �趨�ظ�ȷ�ϵ��ط���ֵ
        int resend_counter = 0;  // ����ͳ����������ͬ ACK

        while (true) {
            UDP_Packet ack_msg;

            // ����ACK��Ϣ
            if (recvfrom(clientSocket, (char*)&ack_msg, sizeof(ack_msg), 0, (SOCKADDR*)&routerAddr, &addr_len)) {
                // ȷ�����յ��İ�����Ч�� ACK
                if (ack_msg.Is_ACK() && ack_msg.CheckValid()) {
                    lock_guard<mutex> lock(mtx);  // ���������ٽ���

                    cout << "[��־] ���յ�ȷ����Ϣ��ACK ���кţ� " << ack_msg.ack << endl;

                    // ���� ACK ���кŸ���
                    if (ack_msg.ack >= Base_Seq + Header_Seq) {
                        Base_Seq = ack_msg.ack - Header_Seq + 1;
                    }

                    // ����Ƿ����������Ϣ����
                    if (ack_msg.ack - Header_Seq == Msg_Num + 1) {
                        Over = true;
                        return;  // ��ɴ��䣬�˳��߳�
                    }

                    // ���� ACK �ط�����
                    if (Err_ack_Num != ack_msg.ack) {
                        Err_ack_Num = ack_msg.ack;
                        resend_counter = 0;  // ���������
                    }
                    else {
                        resend_counter++;
                        if (resend_counter >= resend_threshold) {
                            Resend = true;  // �ﵽ�ط���ֵ�������ط���־
                            resend_counter = 0;  // ���������
                        }
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

        SetConsoleTextAttribute(hConsole, 11); // ǳ��ɫ
        cout << "[����] �ļ�ͷ��Ϣ����" << file_name
            << " (���к�: " << data_msg[0].seq << ")" << endl;
        SetConsoleTextAttribute(hConsole, 7);

        if (sendto(clientSocket, (char*)&data_msg[0], sizeof(data_msg[0]), 0,
            (SOCKADDR*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            SetConsoleTextAttribute(hConsole, 12); // ��ɫ
            cerr << "[����] �ļ�ͷ����ʧ�ܣ������룺" << WSAGetLastError() << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return false;
        }
        return true;
    }

    bool sendFileData(UDP_Packet* data_msg, ifstream& file, int next_seq, int last_length) {
        // ��ȡ�ļ�����
        if (next_seq == Msg_Num && last_length) {
            file.read(data_msg[next_seq - 1].data, last_length);
            data_msg[next_seq - 1].length = last_length;
        }
        else {
            file.read(data_msg[next_seq - 1].data, MAX_DATA_SIZE);
            data_msg[next_seq - 1].length = MAX_DATA_SIZE;
        }

        // �������ݰ�����
        data_msg[next_seq - 1].seq = ++seq;
        data_msg[next_seq - 1].src_port = CLIENT_PORT;
        data_msg[next_seq - 1].dest_port = ROUTER_PORT;
        data_msg[next_seq - 1].check = data_msg[next_seq - 1].Calculate_Checksum();

        if (sendto(clientSocket, (char*)&data_msg[next_seq - 1], sizeof(data_msg[next_seq - 1]), 0,
            (SOCKADDR*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            SetConsoleTextAttribute(hConsole, 12);
            cerr << "[����] ���ݰ�����ʧ�ܣ����кţ�" << data_msg[next_seq - 1].seq
                << "�������룺" << WSAGetLastError() << endl;
            SetConsoleTextAttribute(hConsole, 7);
            return false;
        }
        return true;
    }

    void handleResend(UDP_Packet* data_msg) {
        SetConsoleTextAttribute(hConsole, 14);
        cout << "\n[�ش�] ��ʼ�ش�δȷ�ϵ����ݰ�..." << endl;
        SetConsoleTextAttribute(hConsole, 7);

        for (int i = 0; i < Next_Seq - Base_Seq; i++) {
            lock_guard<mutex> lock(mtx);
            int resend_seq = Base_Seq + i - 1;
            data_msg[resend_seq].check = data_msg[resend_seq].Calculate_Checksum();

            if (sendto(clientSocket, (char*)&data_msg[resend_seq], sizeof(data_msg[resend_seq]), 0,
                (SOCKADDR*)&routerAddr, sizeof(routerAddr)) != SOCKET_ERROR) {
                SetConsoleTextAttribute(hConsole, 14);
                cout << "[�ش�] ���ݰ��ش��ɹ������кţ�" << resend_seq + Header_Seq + 1 << endl;
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
            cout << "[����] Base: " << Base_Seq
                << " Next: " << Next_Seq
                << " δȷ��: " << Next_Seq - Base_Seq
                << " ���ڿռ�: " << Windows_Size - (Next_Seq - Base_Seq)
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

        // ���������
        const int bar_width = 50;
        int filled = bar_width * percentage / 100;

        cout << "\r[����] [";
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

        SetConsoleTextAttribute(hConsole, 10); // ��ɫ
        cout << "\n[���] �ļ��������"
            << "\n�ܴ�С��" << formatFileSize(total_bytes)
            << "\n��ʱ��" << fixed << setprecision(2) << duration << " ��"
            << "\nƽ���ٶȣ�" << speed << " KB/s" << endl;
        SetConsoleTextAttribute(hConsole, 7);
    }

    bool Send_Message(string file_path) {
        // ���ļ���������
        ifstream file(file_path, ios::binary);
        if (!file.is_open()) {
            cerr << "[����] �޷����ļ���" << file_path << "\n����ԭ��" << endl;
            return false;
        }

        // ��ȡ�ļ���Ϣ
        size_t pos = file_path.find_last_of("/\\");
        string file_name = (pos != string::npos) ? file_path.substr(pos + 1) : file_path;
        file.seekg(0, ios::end);
        file_length = file.tellg();
        cout << file_length;
        file.seekg(0, ios::beg);

        SetConsoleTextAttribute(hConsole, 14); // ��ɫ
        cout << "\n[�ļ���Ϣ] ׼�������ļ���" << file_name
            << "\n�ļ���С��" << formatFileSize(file_length) << endl;
        SetConsoleTextAttribute(hConsole, 7);  // �ָ�Ĭ��ɫ

        // �����ļ��ֿ���Ϣ
        int complete_num = file_length / MAX_DATA_SIZE;
        int last_length = file_length % MAX_DATA_SIZE;
        Header_Seq = seq;
        Msg_Num = complete_num + (last_length != 0);

        // ��������ȷ���߳�
        thread ackThread([this]() {
            this->Thread_Ack();
            });

        unique_ptr<UDP_Packet[]> data_msg(new UDP_Packet[Msg_Num + 1]);
        auto start_time = chrono::steady_clock::now();
        uint64_t total_sent_bytes = 0;

        cout << "\n[���俪ʼ] ��ʼ����ɣ���ʼ����...\n" << endl;

        // ������ѭ��
        while (!Over) {
            // �ش�����
            if (Resend) {
                handleResend(data_msg.get());
                continue;
            }

            // �������ʹ���
            if (Next_Seq < Base_Seq + Windows_Size && Next_Seq <= Msg_Num + 1) {
                lock_guard<mutex> lock(mtx);

                if (Next_Seq == 1) {
                    // �����ļ�ͷ
                    if (!sendFileHeader(data_msg.get(), file_name)) {
                        return false;
                    }
                }
                else {
                    // �����ļ�����
                    if (!sendFileData(data_msg.get(), file, Next_Seq, last_length)) {
                        return false;
                    }
                }
                printWindowStatus();
                // ���½��Ⱥ�����
                total_sent_bytes += data_msg[Next_Seq - 1].length;
                printTransferProgress(total_sent_bytes, file_length, start_time);

                Next_Seq++;

            }

            // �����ƣ������ڽӽ���ʱ�ʵ��ӳ�
            if (Next_Seq - Base_Seq > Windows_Size * 0.8) {
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        }

        // �ȴ�ȷ���߳̽���
        ackThread.join();

        // ��ӡ����ͳ����Ϣ
        printTransferStatistics(start_time, file_length);

        // ����״̬
        Reset();
        file.close();

        return true;
    }

    bool Disconnect() {
        UDP_Packet wavehand_packets[4]; // �����Ĵλ�����Ϣ����
        socklen_t addr_len = sizeof(routerAddr);
        auto start_time = chrono::steady_clock::now();

        // ��ʼ��������Ϣ����
        memset(wavehand_packets, 0, sizeof(wavehand_packets)); // ������Ϣ�ṹ������

        // ��һ�λ���: ���� FIN ��Ϣ
        wavehand_packets[0].src_port = CLIENT_PORT;
        wavehand_packets[0].dest_port = ROUTER_PORT;
        wavehand_packets[0].Set_FIN();
        wavehand_packets[0].seq = ++seq;
        wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum();
        cout << "[��־] ��һ�λ��֣����� FIN ��Ϣ�����кţ�" << wavehand_packets[0].seq << endl;
        if (sendto(clientSocket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[����] FIN ��Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }
        while (true) {
            // �ڶ��λ���: �ȴ� ACK ��Ϣ
            if (recvfrom(clientSocket, (char*)&wavehand_packets[1], sizeof(wavehand_packets[1]), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                if (wavehand_packets[1].Is_ACK() &&
                    wavehand_packets[1].ack == wavehand_packets[0].seq &&
                    wavehand_packets[1].CheckValid()) {
                    cout << "[��־] �յ��ڶ��λ�����Ϣ (ACK)��ȷ�����кţ�" << wavehand_packets[1].ack << endl;
                    break;
                }
                else {
                    cerr << "[����] �յ���Ч�� ACK ��Ϣ��������" << endl;
                }
            }

            // ��ʱ�ش���һ�λ�����Ϣ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cout << "[��־] FIN ��Ϣ��ʱ�����·��͡�" << endl;
                wavehand_packets[0].check = wavehand_packets[0].Calculate_Checksum(); // ����У���
                if (sendto(clientSocket, (char*)&wavehand_packets[0], sizeof(wavehand_packets[0]), 0,
                    (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                    cerr << "[����] �ش�ʧ�ܡ�" << endl;
                    return false;
                }
                start_time = now; // ���¼�ʱ
            }
        }

        // �����λ���: ���� FIN ��Ϣ
        start_time = chrono::steady_clock::now();
        while (true) {
            if (recvfrom(clientSocket, (char*)&wavehand_packets[2], sizeof(wavehand_packets[2]), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                cout << wavehand_packets[2].Is_FIN() << wavehand_packets[2].CheckValid();
                if (wavehand_packets[2].Is_FIN() && wavehand_packets[2].CheckValid()) {
                    cout << "[��־] �յ������λ�����Ϣ (FIN)�����кţ�" << wavehand_packets[2].seq << endl;
                    break;
                }
                else {
                    cerr << "[����] �յ���Ч�� FIN ��Ϣ��������" << endl;
                }
            }

            // ��ʱ����
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cerr << "[��־] �ȴ� FIN ��ʱ���Ͽ�����ʧ�ܡ�" << endl;
                return false;
            }
        }
        seq = wavehand_packets[2].seq;
        // ���Ĵλ���: ���� ACK ��Ϣ
        wavehand_packets[3].src_port = CLIENT_PORT;
        wavehand_packets[3].dest_port = ROUTER_PORT;
        wavehand_packets[3].Set_ACK();
        wavehand_packets[3].ack = wavehand_packets[2].seq;
        wavehand_packets[3].seq = ++seq;
        wavehand_packets[3].check = wavehand_packets[3].Calculate_Checksum();
        if (sendto(clientSocket, (char*)&wavehand_packets[3], sizeof(wavehand_packets[3]), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[����] ���Ĵλ�����Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[��־] ���Ĵλ��֣����� ACK ��Ϣ��ȷ�����кţ�" << wavehand_packets[3].ack << endl;

        // �ȴ� 2 * TIMEOUT ʱ����ȷ����Ϣ���
        cout << "[��־] �ȴ� 2 * TIMEOUT ȷ�����ӶϿ�..." << endl;
        this_thread::sleep_for(chrono::milliseconds(2 * TIMEOUT));
        return true;
    }



    ~UDPClient() {
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
            WSACleanup();
            cout << "[��־] �׽����ѹرգ���Դ���ͷš�" << endl;
        }
    }
};

int main() {
    UDPClient sender;
    if (!sender.init()) {
        cerr << "[����] ���Ͷ˳�ʼ��ʧ�ܡ�" << endl;
        return 0;
    }

    if (!sender.connect()) {
        cerr << "[����] ��������ʧ�ܣ��޷��������ӡ�" << endl;
        return 0;
    }

    int choice;
    do {
        cout << "��ѡ�������\n1. �����ļ�\n2. �Ͽ�����\n���룺";
        cin >> choice;


        if (choice == 1) {
            string file_path;
            cout << "������Ҫ���͵��ļ�·����";
            //file_path = R"(F:\Desktop\lab3����\lab3����\�����ļ�\helloworld.txt)";
            cin >> file_path;

            if (!sender.Send_Message(file_path)) {
                cerr << "[����] �ļ�����ʧ�ܡ�" << endl;
            }
        }
        else if (choice == 2) {
            if (!sender.Disconnect()) {
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