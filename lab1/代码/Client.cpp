#include <iostream>
#include <string>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

// ȫ�ֱ����볣������
#define PORT 8000
#define BUF_SIZE 1024

bool running = true;  // ���ڿ��ƿͻ����Ƿ��������

// ��ȡ��ǰʱ����ĺ���
string get_current_time() {
    time_t now = time(0);
    struct tm time_info;
    localtime_s(&time_info, &now);  // ʹ�� localtime_s �滻 localtime
    char time_buffer[50];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &time_info);
    return string(time_buffer);
}

// ������Ϣ�̺߳���
void receive_messages(SOCKET client_socket) {
    char buffer[BUF_SIZE];

    while (running) {
        memset(buffer, 0, BUF_SIZE);
        int bytes_received = recv(client_socket, buffer, BUF_SIZE, 0);
        if (bytes_received > 0) {
            cout << buffer << endl;  // ����������㲥����Ϣ
        }
        else if (bytes_received == 0) {
            cout << "Disconnected from server." << endl;
            break;
        }
        else {
            cerr << "Error receiving message: " << WSAGetLastError() << endl;
            break;
        }
    }

    running = false;  // ���������Ͽ����ӣ�ֹͣ�ͻ���
    closesocket(client_socket);
}

int main() {
    WSADATA wsaData;

    // ��ʼ�� WinSock ��
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
        return 1;
    }

    // �����ͻ����׽���
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    // ���÷�������ַ
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);  // ���ӵ����ط�����
    server_addr.sin_port = htons(PORT);

    // ���ӵ�������
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Connect failed: " << WSAGetLastError() << endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    cout << "Connected to server." << endl;

    // ��ȡ�û�������
    string username;
    cout << "����������û���: ";
    getline(cin, username);

    // �����û�����������
    if (send(client_socket, username.c_str(), username.size(), 0) == SOCKET_ERROR) {
        cerr << "Failed to send username: " << WSAGetLastError() << endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    cout << "******************************************" << endl;
    cout << "*                                        *" << endl;
    cout << "*           �Ͽ���ѧ������               *" << endl;
    cout << "*                                        *" << endl;
    cout << "******************************************" << endl;

    // ����������Ϣ���߳�
    thread receive_thread(receive_messages, client_socket);
    receive_thread.detach();

    // ��ѭ���������û����벢������Ϣ
    string message;
    while (running) {
        string timestamp = get_current_time();
        getline(cin, message);

        if (message == "exit") {
            running = false;
            break;
        }

        // ����û�������Ϣ��
        string full_message = "[" + timestamp + "] " + username + ": " + message;

        // ������Ϣ��������
        if (send(client_socket, full_message.c_str(), full_message.size(), 0) == SOCKET_ERROR) {
            cerr << "Failed to send message: " << WSAGetLastError() << endl;
            break;
        }

        // ����ո��������
        // \033[2K ������У�\033[A ������Ƶ���һ��
        cout << "\033[A\033[2K";  // �����л�����ղ��������
    }


    // �رտͻ����׽���
    closesocket(client_socket);
    WSACleanup();
    cout << "Client disconnected." << endl;
    return 0;
}
