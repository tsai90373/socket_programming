#include <iostream>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

// ============================================================
// 最簡單的 TCP client，用來測試 server
// 用法：./client
// ============================================================

int main() {
    const char* SERVER_IP = "127.0.0.1";
    const int   PORT      = 8080;

    // 1. 建立 socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    // 2. connect 到 server
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    std::cout << "Connected to server\n";

    // 3. 送幾筆測試訊息
    auto send_msg = [&](const std::string& msg) {
        write(fd, msg.c_str(), msg.size());
        std::cout << "Sent: " << msg;

        // 讀回應
        char buf[1024];
        int n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            std::cout << "Recv: " << std::string(buf, n);
        }
    };

    // 正常訂單
    send_msg("ORDER|SIDE=BUY|QTY=100|PRICE=50.5\n");

    // 缺少欄位的訂單（應該收到 ERROR）
    send_msg("ORDER|SIDE=SELL\n");

    // 取消訂單
    send_msg("CANCEL|ORDER_ID=123\n");

    // 未知類型（應該收到 ERROR）
    send_msg("HELLO|FOO=BAR\n");

    close(fd);
    return 0;
}