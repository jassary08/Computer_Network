#include "udp_packet.h" // ���� UDP_Packet �Ķ���

#pragma comment(lib, "ws2_32.lib") // ���� Windows Sockets ��

class UDPClient {
private:
    SOCKET clientSocket;           // �ͻ����׽���
    sockaddr_in clientAddr;        // �ͻ��˵�ַ
    sockaddr_in routerAddr;        // Ŀ��·�ɵ�ַ
    uint32_t seq;                  // �ͻ��˵�ǰ���к�

public:
    UDPClient() : clientSocket(INVALID_SOCKET), seq(0) {}

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
        con_msg[2].seq =  seq + 1;           // �������к�
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

    // �ļ�����������
    bool Send_Message(const string& file_path) {
        // ���ļ�
        ifstream file(file_path, ios::binary);
        if (!file.is_open()) {
            cerr << "[����] �޷����ļ���" << file_path << endl;
            return false;
        }

        // ��ȡ�ļ������ļ���С
        size_t pos = file_path.find_last_of("/\\");
        string file_name = (pos != string::npos) ? file_path.substr(pos + 1) : file_path;
        file.seekg(0, ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, ios::beg);

        cout << "[��־] ׼�������ļ���" << file_name << "����С��" << file_size << " �ֽڡ�" << endl;

        // �����ļ�ͷ��Ϣ
        UDP_Packet header_packet{};
        header_packet.src_port = CLIENT_PORT;
        header_packet.dest_port = ROUTER_PORT;
        header_packet.Set_CFH(); // ���� CFH ��־λ
        header_packet.seq = ++seq; // �������к�
        strncpy_s(header_packet.data, file_name.c_str(), MAX_DATA_SIZE - 1); // д���ļ���
        header_packet.length = file_size; // д���ļ���С
        header_packet.check = header_packet.Calculate_Checksum(); // ����У���

        // �����ļ�ͷ��Ϣ
        if (sendto(clientSocket, (char*)&header_packet, sizeof(header_packet), 0,
            (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
            cerr << "[����] �ļ�ͷ��Ϣ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
            return false;
        }
        cout << "[��־] �ļ�ͷ��Ϣ�ѷ��͡�" << endl;
        // �����ļ�ͷ��Ϣ�����ó�ʱ�ش�����
        auto start_time = chrono::steady_clock::now();
        while (true) {

            // ���� ACK ȷ��
            UDP_Packet ack_packet{};
            socklen_t addr_len = sizeof(routerAddr);
            if (recvfrom(clientSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
                (sockaddr*)&routerAddr, &addr_len) > 0) {
                if (ack_packet.Is_ACK() && ack_packet.ack == header_packet.seq && ack_packet.CheckValid()) {
                    cout << "[��־] �յ��ļ�ͷ��Ϣ�� ACK ȷ�ϡ�" << endl;
                    break; // �ɹ�����ȷ�ϣ��˳��ش�ѭ��
                }
                else {
                    cerr << "[����] �յ��� ACK ��Ч��ƥ�䡣" << endl;
                }
            }

            // ��ⳬʱ
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > TIMEOUT) {
                cout << "[��־] ��ʱ�����·����ļ�ͷ��Ϣ��" << endl;
                header_packet.check = header_packet.Calculate_Checksum(); // ���¼���У���
                if (sendto(clientSocket, (char*)&header_packet, sizeof(header_packet), 0,
                    (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                    cerr << "[����] �ش�ʧ�ܡ�" << endl;
                    return false;
                }
                start_time = now; // ���·���ʱ��
            }
        }

        // �ļ�ͷ��Ϣ���ͳɹ�
        cout << "[��־] �ļ�ͷ��Ϣ������ɣ�׼�������ļ����ݡ�" << endl;

        // ��ʼ�����ļ�����
        size_t total_segments = file_size / MAX_DATA_SIZE;       // �������ݶ�����
        size_t last_segment_size = file_size % MAX_DATA_SIZE;    // ���һ�����ݶεĴ�С
        size_t total_sent = 0;                                   // �ѷ����ֽ�����
        size_t current_segment = 0;                             // ��ǰ���Ͷα��

        auto file_start_time = chrono::steady_clock::now(); // ��¼�ļ�����Ŀ�ʼʱ��

        double estimated_rtt = 100.0;  // ��ʼ���Ƶ� RTT
        double timeout_interval = 200.0;  // ��ʼ��ʱʱ��
        double alpha = 0.125;  // ƽ������
        double beta = 0.25;  // ƽ������
        double dev_rtt = 0.0;  // RTT ƫ��
  
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
                cerr << "[����] ���ݶη���ʧ�ܣ�������룺" << WSAGetLastError() << endl;
                return false;
            }

            cout << "[��־] ���ݶ� " << current_segment + 1
                << " �ѷ��ͣ���С��" << segment_size << " �ֽڡ�" << endl;

            while (!ack_received) {
                UDP_Packet ack_packet{};
                socklen_t addr_len = sizeof(routerAddr);
                if (recvfrom(clientSocket, (char*)&ack_packet, sizeof(ack_packet), 0,
                    (sockaddr*)&routerAddr, &addr_len) > 0) {
                    if (ack_packet.Is_ACK() && ack_packet.ack == data_packet.seq && ack_packet.CheckValid()) {
                        ack_received = true;

                        // ��̬���� RTT �ͳ�ʱʱ��
                        auto now = chrono::steady_clock::now();
                        double sample_rtt = chrono::duration<double, milli>(now - segment_send_time).count();
                        estimated_rtt = (1 - alpha) * estimated_rtt + alpha * sample_rtt;
                        dev_rtt = (1 - beta) * dev_rtt + beta * abs(sample_rtt - estimated_rtt);
                        timeout_interval = estimated_rtt + 4 * dev_rtt;

                        cout << "[��־] �յ� ACK��ȷ�����кţ�" << ack_packet.ack
                            << "��RTT: " << sample_rtt << " ms��" << endl;
                        cout << "[����] �µĳ�ʱʱ��: " << timeout_interval << " ms�����Ƶ� RTT: "
                            << estimated_rtt << " ms��RTT ƫ��: " << dev_rtt << " ms��" << endl;

                        break;
                    }
                    else {
                        cerr << "[����] �յ���Ч ACK �����кŲ�ƥ�䡣" << endl;
                    }
                }

                auto now = chrono::steady_clock::now();
                if (chrono::duration_cast<chrono::milliseconds>(now - segment_send_time).count() > timeout_interval) {
                    cout << "[��־] ���ݶ� " << current_segment + 1
                        << " ��ʱ�����·��͡�" << endl;
                    data_packet.check = data_packet.Calculate_Checksum();
                    if (sendto(clientSocket, (char*)&data_packet, sizeof(data_packet), 0,
                        (sockaddr*)&routerAddr, sizeof(routerAddr)) == SOCKET_ERROR) {
                        cerr << "[����] �ش�ʧ�ܡ�" << endl;
                        return false;
                    }
                    segment_send_time = now;
                }
            }

            total_sent += segment_size;
            current_segment++;
        }

        // �ļ��������
        auto file_end_time = chrono::steady_clock::now();
        double total_time = chrono::duration<double>(file_end_time - file_start_time).count();
        double throughput = (total_sent / 1024.0) / total_time; // KB/s
        cout << "[��־] �ļ�������ɣ��ܺ�ʱ��" << total_time
            << " �룬�����ʣ�" << throughput << " KB/s��" << endl;

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
                    wavehand_packets[2].Print_Message();
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
