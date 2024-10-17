#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include<map>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

// ȫ�ֱ����볣������
#define PORT 8000
#define MAX_CLIENTS 10
#define BUF_SIZE 1024

vector<SOCKET> client_sockets;  // �洢�ͻ����׽���
mutex client_mutex;  // ���� client_sockets �Ļ�����
map<SOCKET, string> client_usernames;  // ���ڴ洢�׽������û�����ӳ��
int user_count = 0;

// ��ȡ��ǰʱ����ĺ���
string get_current_time() {
    time_t now = time(0);
    struct tm time_info;
    localtime_s(&time_info, &now);  // ʹ�� localtime_s �滻 localtime
    char time_buffer[50];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &time_info);
    return string(time_buffer);
}

// �㲥��Ϣ�����пͻ��˵ĺ���
void broadcast_message(const string& message, SOCKET sender_socket = INVALID_SOCKET) {
    lock_guard<mutex> lock(client_mutex);  // �Զ����������
    for (const auto& pair : client_usernames) {  // �����ͻ����׽��ֺ��û�����ӳ��
        SOCKET sock = pair.first;  // ��ȡ�ͻ��˵��׽���
        const string& username = pair.second;  // ��ȡ�û���

        if (!username.empty()) {  // ֻ���������û����Ŀͻ��˲Ź㲥��Ϣ
            if (send(sock, message.c_str(), message.size(), 0) == SOCKET_ERROR) {
                cout << "------------------------------------------" << endl;
                cerr << "Failed to send message to client: " << WSAGetLastError() << endl;
                cout << "------------------------------------------" << endl;
            }
        }
    }
}


// �̴߳�����������ͻ�����Ϣ���û�������
void handle_client(SOCKET client_socket) {
    char buffer[BUF_SIZE];
    string message;
    int bytes_received;

    // �ӿͻ��˽����û���
    char name_buffer[BUF_SIZE] = { 0 };
    int name_len = recv(client_socket, name_buffer, BUF_SIZE, 0);
    if (name_len > 0) {
        user_count++;
        string username(name_buffer, name_len);
        cout << "------------------------------------------" << endl;
        cout << "�ͻ����Ѽ����б�,�û�����: " << username << endl;
        cout << "��ǰ����������" << user_count <<endl;
        cout << "------------------------------------------" << endl;

        // ���û����洢�� map ��
        {
            lock_guard<mutex> lock(client_mutex);
            client_usernames[client_socket] = username;  // ���û�������ӳ����
        }
        // �㲥��ӭ��Ϣ
        string timestamp = get_current_time();
        string welcome_message = "[" + timestamp + "] ϵͳ��Ϣ: " + username + " �ѽ��������ң���ӭ����ǰ����������" + std::to_string(user_count);
        broadcast_message(welcome_message, client_socket);

        // ���պʹ�����Ϣ��ѭ��
        while (true) {
            memset(buffer, 0, BUF_SIZE);
            bytes_received = recv(client_socket, buffer, BUF_SIZE, 0);

            if (bytes_received > 0) {
                string received_message(buffer, bytes_received);

                message = received_message;

                cout << "------------------------------------------" << endl;
                cout << "Received message: " << message << endl;
                cout << "------------------------------------------" << endl;

                // �㲥��Ϣ�������ͻ���
                broadcast_message(message, client_socket);
            }
            else {
                // �ͻ��˶Ͽ�����
                cout << "------------------------------------------" << endl;
                cout << "�û� " << username << " �Ͽ�����: " << client_socket << endl;
                cout << "------------------------------------------" << endl;
                // �㲥�ͻ����뿪��Ϣ
                string timestamp = get_current_time();
                string leave_message = "[" + timestamp + "] " + "ϵͳ��Ϣ: " + username + " ���뿪�����ҡ�";
                broadcast_message(leave_message, client_socket);
                user_count--;
                break;
            }
        }

        // �Ƴ��ͻ��˲��ر�����
        closesocket(client_socket);

        lock_guard<mutex> lock(client_mutex);
        client_sockets.erase(remove(client_sockets.begin(), client_sockets.end(), client_socket), client_sockets.end());
    }
    else {
        cerr << "Failed to receive username." << endl;
        closesocket(client_socket);
    }
}

int main() {
    WSADATA wsaData;

    // ��ʼ�� WinSock ��
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "Socket DLL ��ʼ���ɹ�" << endl;
    cout << "------------------------------------------" << endl;

    // �����������׽���
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "Socket �����ɹ�" << endl;
    cout << "------------------------------------------" << endl;

    // ���÷�������ַ
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // �󶨵���������IP
    server_addr.sin_port = htons(PORT);  // �󶨵��˿�8000

    // ���׽��ֵ�ָ����IP�Ͷ˿�
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "�˿ں��Ѿ��󶨳ɹ�,�˿ں�Ϊ8000" << endl;
    cout << "------------------------------------------" << endl;

    // �����ͻ�������
    if (listen(server_socket, MAX_CLIENTS) == SOCKET_ERROR) {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "��ʼ�������ȴ��ͻ���������..." << endl;
    cout << "------------------------------------------" << endl;

    // ��ѭ�������ܿͻ�������
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    while (true) {
        SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];  // ���ڴ洢�ͻ���IP��ַ
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);  // ʹ�� inet_ntop ��� inet_ntoa

        // ��ȡ��ǰʱ���
        string timestamp = get_current_time();

        cout << "------------------------------------------" << endl;
        cout << "����������: " << client_ip << ":" << ntohs(client_addr.sin_port) << " at " << timestamp << endl;
        cout << "------------------------------------------" << endl;

        // �������ӵĿͻ��˼����б�
        {
            lock_guard<mutex> lock(client_mutex);
            if (client_sockets.size() < MAX_CLIENTS) {
                client_sockets.push_back(client_socket);
            }
            else {
                cout << "------------------------------------------" << endl;
                cout << "�ﵽ���ͻ����������ܾ������ӡ�" << endl;
                cout << "------------------------------------------" << endl;
                closesocket(client_socket);
                continue;
            }
        }

        // �����̴߳���ͻ���
        thread client_thread(handle_client, client_socket);
        client_thread.detach();  // �����߳��Ա������Զ�������
    }

    // �رշ������׽���
    closesocket(server_socket);
    WSACleanup();
    cout << "------------------------------------------" << endl;
    cout << "�������ѹر�" << endl;
    cout << "------------------------------------------" << endl;
    return 0;
}
