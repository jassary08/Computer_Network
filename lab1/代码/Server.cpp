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

// 全局变量与常量定义
#define PORT 8000
#define MAX_CLIENTS 10
#define BUF_SIZE 1024

vector<SOCKET> client_sockets;  // 存储客户端套接字
mutex client_mutex;  // 保护 client_sockets 的互斥锁
map<SOCKET, string> client_usernames;  // 用于存储套接字与用户名的映射
int user_count = 0;

// 获取当前时间戳的函数
string get_current_time() {
    time_t now = time(0);
    struct tm time_info;
    localtime_s(&time_info, &now);  // 使用 localtime_s 替换 localtime
    char time_buffer[50];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &time_info);
    return string(time_buffer);
}

// 广播消息给所有客户端的函数
void broadcast_message(const string& message, SOCKET sender_socket = INVALID_SOCKET) {
    lock_guard<mutex> lock(client_mutex);  // 自动加锁与解锁
    for (const auto& pair : client_usernames) {  // 遍历客户端套接字和用户名的映射
        SOCKET sock = pair.first;  // 获取客户端的套接字
        const string& username = pair.second;  // 获取用户名

        if (!username.empty()) {  // 只有设置了用户名的客户端才广播消息
            if (send(sock, message.c_str(), message.size(), 0) == SOCKET_ERROR) {
                cout << "------------------------------------------" << endl;
                cerr << "Failed to send message to client: " << WSAGetLastError() << endl;
                cout << "------------------------------------------" << endl;
            }
        }
    }
}


// 线程处理函数：处理客户端消息和用户名接收
void handle_client(SOCKET client_socket) {
    char buffer[BUF_SIZE];
    string message;
    int bytes_received;

    // 从客户端接收用户名
    char name_buffer[BUF_SIZE] = { 0 };
    int name_len = recv(client_socket, name_buffer, BUF_SIZE, 0);
    if (name_len > 0) {
        user_count++;
        string username(name_buffer, name_len);
        cout << "------------------------------------------" << endl;
        cout << "客户端已加入列表,用户名称: " << username << endl;
        cout << "当前在线人数：" << user_count <<endl;
        cout << "------------------------------------------" << endl;

        // 将用户名存储在 map 中
        {
            lock_guard<mutex> lock(client_mutex);
            client_usernames[client_socket] = username;  // 将用户名存入映射中
        }
        // 广播欢迎消息
        string timestamp = get_current_time();
        string welcome_message = "[" + timestamp + "] 系统消息: " + username + " 已进入聊天室，欢迎！当前在线人数：" + std::to_string(user_count);
        broadcast_message(welcome_message, client_socket);

        // 接收和处理消息的循环
        while (true) {
            memset(buffer, 0, BUF_SIZE);
            bytes_received = recv(client_socket, buffer, BUF_SIZE, 0);

            if (bytes_received > 0) {
                string received_message(buffer, bytes_received);

                message = received_message;

                cout << "------------------------------------------" << endl;
                cout << "Received message: " << message << endl;
                cout << "------------------------------------------" << endl;

                // 广播消息给其他客户端
                broadcast_message(message, client_socket);
            }
            else {
                // 客户端断开连接
                cout << "------------------------------------------" << endl;
                cout << "用户 " << username << " 断开连接: " << client_socket << endl;
                cout << "------------------------------------------" << endl;
                // 广播客户端离开消息
                string timestamp = get_current_time();
                string leave_message = "[" + timestamp + "] " + "系统消息: " + username + " 已离开聊天室。";
                broadcast_message(leave_message, client_socket);
                user_count--;
                break;
            }
        }

        // 移除客户端并关闭连接
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

    // 初始化 WinSock 库
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "Socket DLL 初始化成功" << endl;
    cout << "------------------------------------------" << endl;

    // 创建服务器套接字
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "Socket 创建成功" << endl;
    cout << "------------------------------------------" << endl;

    // 配置服务器地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 绑定到本地所有IP
    server_addr.sin_port = htons(PORT);  // 绑定到端口8000

    // 绑定套接字到指定的IP和端口
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "端口号已经绑定成功,端口号为8000" << endl;
    cout << "------------------------------------------" << endl;

    // 监听客户端连接
    if (listen(server_socket, MAX_CLIENTS) == SOCKET_ERROR) {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    cout << "------------------------------------------" << endl;
    cout << "开始监听，等待客户端连接中..." << endl;
    cout << "------------------------------------------" << endl;

    // 主循环：接受客户端连接
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    while (true) {
        SOCKET client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];  // 用于存储客户端IP地址
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);  // 使用 inet_ntop 替代 inet_ntoa

        // 获取当前时间戳
        string timestamp = get_current_time();

        cout << "------------------------------------------" << endl;
        cout << "新连接来自: " << client_ip << ":" << ntohs(client_addr.sin_port) << " at " << timestamp << endl;
        cout << "------------------------------------------" << endl;

        // 将新连接的客户端加入列表
        {
            lock_guard<mutex> lock(client_mutex);
            if (client_sockets.size() < MAX_CLIENTS) {
                client_sockets.push_back(client_socket);
            }
            else {
                cout << "------------------------------------------" << endl;
                cout << "达到最大客户端数量，拒绝新连接。" << endl;
                cout << "------------------------------------------" << endl;
                closesocket(client_socket);
                continue;
            }
        }

        // 创建线程处理客户端
        thread client_thread(handle_client, client_socket);
        client_thread.detach();  // 分离线程以便它可以独立运行
    }

    // 关闭服务器套接字
    closesocket(server_socket);
    WSACleanup();
    cout << "------------------------------------------" << endl;
    cout << "服务器已关闭" << endl;
    cout << "------------------------------------------" << endl;
    return 0;
}
