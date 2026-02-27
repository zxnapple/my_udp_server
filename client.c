#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
#define BUFFER_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    /* 创建socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Failed to create socket");
        return 1;
    }

    /* 设置服务器地址 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    /* 发送数据 */
    printf("Enter message to send: ");
    fgets(buffer, BUFFER_SIZE, stdin);
    
    ssize_t len = sendto(sockfd, buffer, strlen(buffer), 0, 
                        (struct sockaddr*)&server_addr, addr_len);
    if (len < 0) {
        perror("Failed to send data");
        close(sockfd);
        return 1;
    }

    printf("Sent %zd bytes to server\n", len);

    /* 关闭socket */
    close(sockfd);
    return 0;
}
