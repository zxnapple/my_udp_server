#include "network_lib.h"
#include "log.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

/* 全局上下文 */
static network_context_t g_context;
static network_data_callback_t g_data_callback = NULL;

/* 配置默认值 */
#define DEFAULT_LISTEN_THREADS 4
#define DEFAULT_WORK_THREADS 4
#define DEFAULT_BUFFER_SIZE (1024 * 1024) /* 1MB */
#define DEFAULT_SERVER_IP "0.0.0.0"
#define DEFAULT_PORT 8888

/* 读取配置文件 */
static int read_config(const char* config_file, network_config_t* config) {
    FILE* fp = fopen(config_file, "r");
    if (!fp) {
        log_warn("Config file not found, using default values");
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* buffer = (char*)malloc(len + 1);
    if (!buffer) {
        fclose(fp);
        log_error("Failed to allocate buffer for config");
        return -1;
    }

    fread(buffer, 1, len, fp);
    buffer[len] = '\0';
    fclose(fp);

    cJSON* root = cJSON_Parse(buffer);
    if (!root) {
        log_error("Failed to parse config file");
        free(buffer);
        return -1;
    }

    cJSON* item = cJSON_GetObjectItem(root, "listen_threads");
    if (item && cJSON_IsNumber(item)) {
        config->listen_threads = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "work_threads");
    if (item && cJSON_IsNumber(item)) {
        config->work_threads = item->valueint;
    }

    item = cJSON_GetObjectItem(root, "buffer_size");
    if (item && cJSON_IsNumber(item)) {
        config->buffer_size = (size_t)item->valuedouble;
    }

    item = cJSON_GetObjectItem(root, "server_ip");
    if (item && cJSON_IsString(item)) {
        strncpy(config->server_ip, item->valuestring, sizeof(config->server_ip) - 1);
    }

    item = cJSON_GetObjectItem(root, "port");
    if (item && cJSON_IsNumber(item)) {
        config->port = (uint16_t)item->valueint;
    }

    cJSON_Delete(root);
    free(buffer);
    return 0;
}

/* 初始化网络库 */
int network_lib_init(const char* config_file) {
    /* 设置默认配置 */
    g_context.config.listen_threads = DEFAULT_LISTEN_THREADS;
    g_context.config.work_threads = DEFAULT_WORK_THREADS;
    g_context.config.buffer_size = DEFAULT_BUFFER_SIZE;
    strcpy(g_context.config.server_ip, DEFAULT_SERVER_IP);
    g_context.config.port = DEFAULT_PORT;

    /* 读取配置文件 */
    if (config_file) {
        if (read_config(config_file, &g_context.config) < 0) {
            log_error("Failed to read config file, using default values");
        }
    }

    /* 分配环形缓存区 */
    g_context.buffer_data = (uint8_t*)malloc(g_context.config.buffer_size);
    if (!g_context.buffer_data) {
        log_error("Failed to allocate buffer");
        return -1;
    }

    /* 初始化环形缓存区 */
    if (!lwrb_init(&g_context.ring_buffer, g_context.buffer_data, g_context.config.buffer_size)) {
        log_error("Failed to initialize ring buffer");
        free(g_context.buffer_data);
        return -1;
    }

    /* 初始化互斥锁和条件变量 */
    pthread_mutex_init(&g_context.buffer_mutex, NULL);
    pthread_cond_init(&g_context.buffer_cond, NULL);

    /* 创建socket */
    g_context.server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_context.server_fd < 0) {
        log_error("Failed to create socket");
        pthread_mutex_destroy(&g_context.buffer_mutex);
        pthread_cond_destroy(&g_context.buffer_cond);
        free(g_context.buffer_data);
        return -1;
    }

    /* 设置SO_REUSEPORT */
    int reuse = 1;
    if (setsockopt(g_context.server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        log_warn("Failed to set SO_REUSEPORT, continue without it");
    }

    /* 绑定地址 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(g_context.config.server_ip);
    addr.sin_port = htons(g_context.config.port);

    if (bind(g_context.server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind socket");
        close(g_context.server_fd);
        pthread_mutex_destroy(&g_context.buffer_mutex);
        pthread_cond_destroy(&g_context.buffer_cond);
        free(g_context.buffer_data);
        return -1;
    }

    /* 创建epoll */
    g_context.epoll_fd = epoll_create1(0);
    if (g_context.epoll_fd < 0) {
        log_error("Failed to create epoll");
        close(g_context.server_fd);
        pthread_mutex_destroy(&g_context.buffer_mutex);
        pthread_cond_destroy(&g_context.buffer_cond);
        free(g_context.buffer_data);
        return -1;
    }

    /* 添加socket到epoll */
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = g_context.server_fd;
    if (epoll_ctl(g_context.epoll_fd, EPOLL_CTL_ADD, g_context.server_fd, &event) < 0) {
        log_error("Failed to add socket to epoll");
        close(g_context.epoll_fd);
        close(g_context.server_fd);
        pthread_mutex_destroy(&g_context.buffer_mutex);
        pthread_cond_destroy(&g_context.buffer_cond);
        free(g_context.buffer_data);
        return -1;
    }

    /* 分配线程数组 */
    g_context.listen_threads = (pthread_t*)malloc(sizeof(pthread_t) * g_context.config.listen_threads);
    g_context.work_threads = (pthread_t*)malloc(sizeof(pthread_t) * g_context.config.work_threads);
    if (!g_context.listen_threads || !g_context.work_threads) {
        log_error("Failed to allocate thread arrays");
        if (g_context.listen_threads) free(g_context.listen_threads);
        if (g_context.work_threads) free(g_context.work_threads);
        close(g_context.epoll_fd);
        close(g_context.server_fd);
        pthread_mutex_destroy(&g_context.buffer_mutex);
        pthread_cond_destroy(&g_context.buffer_cond);
        free(g_context.buffer_data);
        return -1;
    }

    g_context.running = 0;
    log_info("Network library initialized successfully");
    return 0;
}

/* 监听线程函数 */
static void* listen_thread_func(void* arg) {
    int thread_id = *(int*)arg;
    free(arg);

    log_info("Listen thread %d started", thread_id);

    while (g_context.running) {
        struct epoll_event events[10];
        int nfds = epoll_wait(g_context.epoll_fd, events, 10, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            log_error("Epoll wait failed");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == g_context.server_fd) {
                /* 接收UDP数据 */
                uint8_t buffer[65536];
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                ssize_t len = recvfrom(g_context.server_fd, buffer, sizeof(buffer), 0, 
                                     (struct sockaddr*)&client_addr, &addr_len);
                if (len < 0) {
                    log_error("Recvfrom failed");
                    continue;
                }

                /* 写入环形缓存区 */
                pthread_mutex_lock(&g_context.buffer_mutex);
                size_t written = lwrb_write(&g_context.ring_buffer, buffer, len);
                if (written < len) {
                    log_warn("Buffer full, data truncated");
                }
                pthread_cond_broadcast(&g_context.buffer_cond);
                pthread_mutex_unlock(&g_context.buffer_mutex);
            }
        }
    }

    log_info("Listen thread %d stopped", thread_id);
    return NULL;
}

/* 工作线程函数 */
static void* work_thread_func(void* arg) {
    int thread_id = *(int*)arg;
    free(arg);

    log_info("Work thread %d started", thread_id);

    while (g_context.running) {
        pthread_mutex_lock(&g_context.buffer_mutex);
        /* 等待数据 */
        while (g_context.running && lwrb_get_full(&g_context.ring_buffer) == 0) {
            pthread_cond_wait(&g_context.buffer_cond, &g_context.buffer_mutex);
        }

        if (!g_context.running) {
            pthread_mutex_unlock(&g_context.buffer_mutex);
            break;
        }

        /* 读取数据 */
        uint8_t buffer[65536];
        size_t len = lwrb_read(&g_context.ring_buffer, buffer, sizeof(buffer));
        pthread_mutex_unlock(&g_context.buffer_mutex);

        /* 处理数据 */
        if (len > 0 && g_data_callback) {
            g_data_callback(buffer, len);
        }
    }

    log_info("Work thread %d stopped", thread_id);
    return NULL;
}

/* 启动网络库 */
int network_lib_start(void) {
    if (g_context.running) {
        log_warn("Network library already running");
        return 0;
    }

    g_context.running = 1;

    /* 创建监听线程 */
    for (int i = 0; i < g_context.config.listen_threads; i++) {
        int* thread_id = (int*)malloc(sizeof(int));
        *thread_id = i;
        if (pthread_create(&g_context.listen_threads[i], NULL, listen_thread_func, thread_id) != 0) {
            log_error("Failed to create listen thread %d", i);
            g_context.running = 0;
            return -1;
        }
    }

    /* 创建工作线程 */
    for (int i = 0; i < g_context.config.work_threads; i++) {
        int* thread_id = (int*)malloc(sizeof(int));
        *thread_id = i;
        if (pthread_create(&g_context.work_threads[i], NULL, work_thread_func, thread_id) != 0) {
            log_error("Failed to create work thread %d", i);
            g_context.running = 0;
            return -1;
        }
    }

    log_info("Network library started successfully");
    return 0;
}

/* 停止网络库 */
void network_lib_stop(void) {
    if (!g_context.running) {
        log_warn("Network library not running");
        return;
    }

    g_context.running = 0;

    /* 唤醒所有工作线程 */
    pthread_mutex_lock(&g_context.buffer_mutex);
    pthread_cond_broadcast(&g_context.buffer_cond);
    pthread_mutex_unlock(&g_context.buffer_mutex);

    /* 等待线程结束 */
    for (int i = 0; i < g_context.config.listen_threads; i++) {
        pthread_join(g_context.listen_threads[i], NULL);
    }

    for (int i = 0; i < g_context.config.work_threads; i++) {
        pthread_join(g_context.work_threads[i], NULL);
    }

    log_info("Network library stopped successfully");
}

/* 销毁网络库 */
void network_lib_destroy(void) {
    if (g_context.running) {
        network_lib_stop();
    }

    if (g_context.epoll_fd >= 0) {
        close(g_context.epoll_fd);
    }

    if (g_context.server_fd >= 0) {
        close(g_context.server_fd);
    }

    pthread_mutex_destroy(&g_context.buffer_mutex);
    pthread_cond_destroy(&g_context.buffer_cond);

    if (g_context.buffer_data) {
        free(g_context.buffer_data);
    }

    if (g_context.listen_threads) {
        free(g_context.listen_threads);
    }

    if (g_context.work_threads) {
        free(g_context.work_threads);
    }

    log_info("Network library destroyed successfully");
}

/* 设置数据回调函数 */
void network_set_data_callback(network_data_callback_t callback) {
    g_data_callback = callback;
}
