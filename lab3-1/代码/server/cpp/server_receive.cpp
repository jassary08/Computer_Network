#include "udp_packet.h" // ���� UDP_Packet �Ķ���

#pragma comment(lib, "ws2_32.lib") // ���� Windows Sockets ��

class UDPServer {
private:
    SOCKET serverSocket;           // ���������׽���
    sockaddr_in serverAddress;     // ��������ַ
    sockaddr_in routerAddress;     // ·������ַ
    uint32_t seq;           // ��ǰ���к�

public:
    UDPServer() : serverSocket(INVALID_SOCKET), seq(0) {}

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
                    handshakePackets[1].check = handshakePackets[1].Calculate_Checksum();

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



    bool ReceiveMessage(const string& outputDir) {
        UDP_Packet headerPacket{};         // �ļ�ͷ����
        socklen_t routerAddrLen = sizeof(routerAddress);

        // �����ļ�ͷ����Ϣ
        while (true) {
            if (recvfrom(serverSocket, (char*)&headerPacket, sizeof(headerPacket), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                if (headerPacket.Is_CFH() && headerPacket.CheckValid()) {
                    cout << "[��־] �յ��ļ�ͷ����Ϣ��" << endl;
                    cout << "[��־] �ļ�����" << headerPacket.data
                        << "���ļ���С��" << headerPacket.length << " �ֽڡ�" << endl;
                    // ����ȷ�ϱ���
                    UDP_Packet ackPacket{};
                    ackPacket.src_port = SERVER_PORT;   // �������˵Ķ˿�
                    ackPacket.dest_port = ROUTER_PORT; // ·�����Ķ˿�
                    ackPacket.Set_ACK();
                    ackPacket.seq = ++seq;
                    ackPacket.ack = headerPacket.seq;
                    ackPacket.check = ackPacket.Calculate_Checksum();
                    if (sendto(serverSocket, (char*)&ackPacket, sizeof(ackPacket), 0,
                        (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[����] ACK ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[��־] �ѷ����ļ�ͷ��ȷ�ϱ��� (ACK)��" << endl;
                    break;
                }
                else {
                    cerr << "[����] �յ����ļ�ͷ����Ϣ��Ч��������" << endl;
                }
            }
        }

        // ���ļ�׼��д��
        string filePath = outputDir + "/" + string(headerPacket.data);
        ofstream file(filePath, ios::binary);
        if (!file.is_open()) {
            cerr << "[����] �޷����ļ�����д�룺" << filePath << endl;
            return false;
        }
        cout << "[��־] �ļ��Ѵ򿪣�׼����������д�룺" << filePath << endl;

        size_t totalReceived = 0;
        size_t totalSize = headerPacket.length;       // �ܴ�С
        uint32_t expectedSeq = headerPacket.seq + 1; // ��һ�����������к�

        // ѭ�������ļ�����
        while (totalReceived < totalSize) {
            UDP_Packet dataPacket{};
            if (recvfrom(serverSocket, (char*)&dataPacket, sizeof(dataPacket), 0,
                (sockaddr*)&routerAddress, &routerAddrLen) > 0) {
                // У�����ݰ�
                if (dataPacket.CheckValid() && dataPacket.seq == expectedSeq) {
                    file.write(dataPacket.data, dataPacket.length); // д���ļ�
                    totalReceived += dataPacket.length;
                    expectedSeq++;

                    cout << "[��־] ���ݶν��ճɹ������кţ�" << dataPacket.seq
                        << "����С��" << dataPacket.length << " �ֽڡ�" << endl;

                    // ���� ACK ȷ��
                    seq = dataPacket.seq;
                    UDP_Packet ackPacket{};
                    memset(&ackPacket, 0, sizeof(ackPacket)); // ���� ACK ��Ϣ
                    ackPacket.src_port = SERVER_PORT;   // ���ö˿�
                    ackPacket.dest_port = ROUTER_PORT;
                    ackPacket.Set_ACK();
                    ackPacket.seq = ++seq;
                    ackPacket.ack = dataPacket.seq;
                    ackPacket.check = ackPacket.Calculate_Checksum();

                    if (sendto(serverSocket, (char*)&ackPacket, sizeof(ackPacket), 0,
                        (sockaddr*)&routerAddress, routerAddrLen) == SOCKET_ERROR) {
                        cerr << "[����] ACK ����ʧ�ܣ�������룺" << WSAGetLastError() << endl;
                        return false;
                    }
                    cout << "[��־] �ѷ������ݶ�ȷ�ϱ��� (ACK)��ȷ�����кţ�" << ackPacket.ack << endl;
                }
                else {
                    cerr << "[����] �յ���Ч���ݶΣ�������" << endl;
                }
            }
        }

        file.close();
        cout << "[��־] �ļ�������ɣ��ܽ����ֽ�����" << totalReceived
            << "���ļ��ѱ�������" << filePath << endl;
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
