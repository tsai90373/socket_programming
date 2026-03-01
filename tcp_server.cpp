#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <vector>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

// ============================================================
// 訊息格式（自訂）
// 格式：TYPE|FIELD1=VALUE1|FIELD2=VALUE2\n
// 例如：ORDER|SIDE=BUY|QTY=100|PRICE=50.5\n
//       CANCEL|ORDER_ID=123\n
// 用 \n 作為訊息結尾
// ============================================================

struct Message {
    std::string type;
    std::unordered_map<std::string, std::string> fields;
};

bool parse_message(const std::string& raw, Message& msg) {
    std::vector<std::string> parts;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, '|')) {
        parts.push_back(token);
    }

    if (parts.empty()) return false;

    msg.type = parts[0];

    for (size_t i = 1; i < parts.size(); i++) {
        auto eq = parts[i].find('=');
        if (eq == std::string::npos) return false;
        msg.fields[parts[i].substr(0, eq)] = parts[i].substr(eq + 1);
    }

    return true;
}

std::string handle_message(const Message& msg) {
    if (msg.type == "ORDER") {
        if (!msg.fields.count("SIDE") ||
            !msg.fields.count("QTY") ||
            !msg.fields.count("PRICE")) {
            return "ERROR|REASON=MISSING_FIELDS\n";
        }
        std::cout << "[ORDER] SIDE=" << msg.fields.at("SIDE")
                  << " QTY="         << msg.fields.at("QTY")
                  << " PRICE="       << msg.fields.at("PRICE") << "\n";
        return "ACK|STATUS=RECEIVED\n";

    } else if (msg.type == "CANCEL") {
        if (!msg.fields.count("ORDER_ID")) {
            return "ERROR|REASON=MISSING_ORDER_ID\n";
        }
        std::cout << "[CANCEL] ORDER_ID=" << msg.fields.at("ORDER_ID") << "\n";
        return "ACK|STATUS=CANCELLED\n";

    } else {
        return "ERROR|REASON=UNKNOWN_TYPE\n";
    }
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    const int PORT = 8080;

    // 回傳一個file descriptor
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    if (listen(listenfd, 10) < 0) { perror("listen"); return 1; }
    set_nonblocking(listenfd);
    std::cout << "Server listening on port " << PORT << "\n";

    std::unordered_map<int, std::string> recv_bufs;

    // 1024-bit bit mask
    fd_set master_fds;
    // Set all bits to 0
    FD_ZERO(&master_fds);
    // Set certain bit to 1
    FD_SET(listenfd, &master_fds);
    int max_fd = listenfd;

    while (true) {
        fd_set read_fds = master_fds;
        // 這裡把 read_fds 傳進去以後，透過 select() 就會拿到有事件的fds
        /* 
            select 的功能：
            1. select 是一個 blocking function，功能是把的 thread(task_struct) 放到 socket 的 wainting_queue 裡面
               排隊以後，process 會先進入 sleep (blocking)
            2. 當 socket 有訊息時(網卡收到封包)，
               kernel 把資料放進 recv_buf 
               並且根據 waiting_queue 的去通知訂閱者（像是訂閱發行的概念）
            3. process 醒來，從 0 到 max_fd 掃過一次看 recv_buf 有沒有資料
               如果有資料的話 bit 留著，沒有就清掉
            
            結論：
            傳入我們要監聽的fds，select會幫忙監聽，並且在有訊息時喚醒process，並把有事件的fd篩選出來
        */
        int n = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
        if (n < 0) { perror("select"); break; }

        for (int fd = 0; fd <= max_fd; fd++) {
            if (!FD_ISSET(fd, &read_fds)) continue;

            // When a new connection is intiated
            if (fd == listenfd) {
                struct sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_len);
                if (connfd < 0) continue;

                set_nonblocking(connfd);
                FD_SET(connfd, &master_fds);
                if (connfd > max_fd) max_fd = connfd;
                recv_bufs[connfd] = "";

                std::cout << "New connection: fd=" << connfd
                          << " from " << inet_ntoa(client_addr.sin_addr) << "\n";

            } else {
                char tmp[1024];
                int bytes = read(fd, tmp, sizeof(tmp));

                if (bytes <= 0) {
                    std::cout << "Connection closed: fd=" << fd << "\n";
                    FD_CLR(fd, &master_fds);
                    close(fd);
                    recv_bufs.erase(fd);
                    continue;
                }

                recv_bufs[fd] += std::string(tmp, bytes);

                std::string& buf = recv_bufs[fd];
                size_t pos;
                while ((pos = buf.find('\n')) != std::string::npos) {
                    std::string raw_msg = buf.substr(0, pos);
                    buf = buf.substr(pos + 1);

                    Message msg;
                    if (!parse_message(raw_msg, msg)) {
                        std::string err = "ERROR|REASON=PARSE_FAILED\n";
                        write(fd, err.c_str(), err.size());
                        continue;
                    }

                    std::string response = handle_message(msg);
                    write(fd, response.c_str(), response.size());
                }
            }
        }
    }

    close(listenfd);
    return 0;
}