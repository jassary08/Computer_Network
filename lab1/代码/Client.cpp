#include <iostream>
#include <string>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

// 全局变量与常量定义
#define PORT 8000
#define BUF_SIZE 1024

bool running = true;  // 用于控制客户端是否继续运行

// 获取当前时间戳的函数
string get_current_time() {
    time_t now = time(0);
    struct tm time_info;
    localtime_s(&time_info, &now);  // 使用 localtime_s 替换 localtime
    char time_buffer[50];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &time_info);
    return string(time_buffer);
}

// 接收消息线程函数
void receive_messages(SOCKET client_socket) {
    char buffer[BUF_SIZE];

    while (running) {
        memset(buffer, 0, BUF_SIZE);
        int bytes_received = recv(client_socket, buffer, BUF_SIZE, 0);
        if (bytes_received > 0) {
            cout << buffer << endl;  // 输出服务器广播的消息
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

    running = false;  // 当服务器断开连接，停止客户端
    closesocket(client_socket);
}

int main() {
    WSADATA wsaData;

    // 初始化 WinSock 库
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
        return 1;
    }

    // 创建客户端套接字
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    // 配置服务器地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);  // 连接到本地服务器
    server_addr.sin_port = htons(PORT);

    // 连接到服务器
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Connect failed: " << WSAGetLastError() << endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    cout << "Connected to server." << endl;

    // 获取用户的名字
    string username;
    cout << "请输入你的用户名: ";
    getline(cin, username);

    // 发送用户名到服务器
    if (send(client_socket, username.c_str(), username.size(), 0) == SOCKET_ERROR) {
        cerr << "Failed to send username: " << WSAGetLastError() << endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    cout << "******************************************" << endl;
    cout << "*                                        *" << endl;
    cout << "*           南开大学聊天室               *" << endl;
    cout << "*                                        *" << endl;
    cout << "******************************************" << endl;

    // 创建接收消息的线程
    thread receive_thread(receive_messages, client_socket);
    receive_thread.detach();

    // 主循环：处理用户输入并发送消息
    string message;
    while (running) {
        string timestamp = get_current_time();
        getline(cin, message);

        if (message == "exit") {
            running = false;
            break;
        }

        // 添加用户名到消息中
        string full_message = "[" + timestamp + "] " + username + ": " + message;

        // 发送消息到服务器
        if (send(client_socket, full_message.c_str(), full_message.size(), 0) == SOCKET_ERROR) {
            cerr << "Failed to send message: " << WSAGetLastError() << endl;
            break;
        }

        // 清除刚刚输入的行
        // \033[2K 清除整行，\033[A 将光标移到上一行
        cout << "\033[A\033[2K";  // 这两行会清除刚才输入的行
    }


    // 关闭客户端套接字
    closesocket(client_socket);
    WSACleanup();
    cout << "Client disconnected." << endl;
    return 0;
}
