# 高性能UDP网络库

## 功能特性

- 基于UDP协议的网络通信
- 使用epoll机制进行高效事件监听
- 支持多线程架构：
  - 多个监听线程，支持SO_REUSEPORT模式
  - 多个工作线程，负责数据处理
- 使用lwrb实现的环形缓存区
- 基于条件变量的线程同步
- JSON格式的配置文件

## 依赖项

- liblwrb.a - 环形缓存区库
- cJSON - JSON解析库
- log - 日志库

## 编译

```bash
make
```

## 运行

```bash
./example
```

## 配置文件

配置文件 `config.json` 支持以下参数：

```json
{
    "listen_threads": 4,        // 监听线程数量
    "work_threads": 4,          // 工作线程数量
    "buffer_size": 1048576,     // 环形缓存区大小（字节）
    "server_ip": "0.0.0.0",     // 服务器IP地址
    "port": 8888                // 监听端口号
}
```

## 使用示例

```c
#include "network_lib.h"
#include "log.h"

void data_callback(const uint8_t* data, size_t len) {
    printf("Received data: %.*s\n", (int)len, data);
}

int main() {
    // 初始化日志
    log_add_fp(stdout, LOG_INFO);
    log_set_level(LOG_INFO);

    // 初始化网络库
    if (network_lib_init("config.json") < 0) {
        log_fatal("Failed to initialize network library");
        return 1;
    }

    // 设置数据回调
    network_set_data_callback(data_callback);

    // 启动网络库
    if (network_lib_start() < 0) {
        log_fatal("Failed to start network library");
        network_lib_destroy();
        return 1;
    }

    // 运行
    sleep(30);

    // 停止网络库
    network_lib_stop();

    // 销毁网络库
    network_lib_destroy();

    return 0;
}
```

## 性能优化

- 使用SO_REUSEPORT实现多线程同时监听同一端口
- 使用epoll进行高效事件监听
- 使用环形缓存区减少内存拷贝
- 工作线程在无数据时休眠，减少CPU占用
