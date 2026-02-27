#include "network_lib.h"
#include "log.h"
#include <stdio.h>
#include <unistd.h>

/* 数据处理回调函数 */
void data_callback(const uint8_t* data, size_t len) {
    printf("Received data: %.*s\n", (int)len, data);
}

int main(int argc, char* argv[]) {
    /* 初始化日志 */
    log_add_fp(stdout, LOG_INFO);
    log_set_level(LOG_INFO);

    /* 初始化网络库 */
    if (network_lib_init("config.json") < 0) {
        log_fatal("Failed to initialize network library");
        return 1;
    }

    /* 设置数据回调 */
    network_set_data_callback(data_callback);

    /* 启动网络库 */
    if (network_lib_start() < 0) {
        log_fatal("Failed to start network library");
        network_lib_destroy();
        return 1;
    }

    log_info("Server started, listening on port 8888");

    /* 运行一段时间 */
    sleep(30);

    /* 停止网络库 */
    network_lib_stop();

    /* 销毁网络库 */
    network_lib_destroy();

    log_info("Server stopped");
    return 0;
}
