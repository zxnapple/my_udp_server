#ifndef NETWORK_LIB_H
#define NETWORK_LIB_H

#include <stdint.h>
#include <pthread.h>
#include "lwrb.h"

/* 配置结构 */
typedef struct {
    int listen_threads;     /* 监听线程数量 */
    int work_threads;       /* 工作线程数量 */
    size_t buffer_size;     /* 环形缓存区大小 */
    char server_ip[16];     /* 服务器IP地址 */
    uint16_t port;          /* 监听端口号 */
} network_config_t;

/* 网络库上下文 */
typedef struct {
    network_config_t config;        /* 配置信息 */
    lwrb_t ring_buffer;             /* 环形缓存区 */
    uint8_t* buffer_data;           /* 缓存区数据 */
    pthread_mutex_t buffer_mutex;   /* 缓存区互斥锁 */
    pthread_cond_t buffer_cond;     /* 缓存区条件变量 */
    int epoll_fd;                   /* epoll文件描述符 */
    int server_fd;                  /* 服务器socket文件描述符 */
    pthread_t* listen_threads;      /* 监听线程数组 */
    pthread_t* work_threads;        /* 工作线程数组 */
    int running;                    /* 运行状态 */
} network_context_t;

/* 函数声明 */
int network_lib_init(const char* config_file);
int network_lib_start(void);
void network_lib_stop(void);
void network_lib_destroy(void);

/* 回调函数类型 */
typedef void (*network_data_callback_t)(const uint8_t* data, size_t len);
void network_set_data_callback(network_data_callback_t callback);

#endif /* NETWORK_LIB_H */