#ifndef __SCHEDULER_H
#define __SCHEDULER_H

/**
 * Crawl scheduler network module
 * @author Xuyun
 * @time 02/22/2019
 */

#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "threadpool.h"
#include "ngx_palloc.h"

#define SOURCE_BUFFER_LEN_MAX 5120
#define RESULT_BUFFER_LEN_MAX 5120

struct sched_master{
	char ip[16];
	int port;
	char run_flag;

	int sock_fd;
	int epoll_fd;

	void *queue[2];
	pthread_mutex_t mutex;

	threadpool_t *threadpool;
	ngx_pool_t *mempool;
	void *p_global_data;

	//int pipe[2];

	int (*master_construct)(void **pp_global_data);
	int (*master_destruct)(void *p_global_data);
	
	int (*slaver_construct)(void *p_global_data, void **pp_private_data);
	int (*slaver_destruct)(void *p_global_data, void *p_private_data);
	int (*slaver_work)(void *p_global_data, void *p_private_data, char *res_buf, char *src_buf, int src_buf_len);
};

struct sched_slaver{
	int sock_fd;
	struct sockaddr_in addr;
	void *wq[2];
	char src_buf[SOURCE_BUFFER_LEN_MAX];
	char res_buf[RESULT_BUFFER_LEN_MAX];
	int src_buf_len;
	int res_buf_len;
	void *p_private_data;
	struct sched_master *p_master;
	
	int (*slaver_construct)(void *p_global_data, void **pp_private_data);
	int (*slaver_destruct)(void *p_global_data, void *p_private_data);
	int (*slaver_work)(void *p_global_data, void *p_private_data, char *res_buf, char *src_buf, int src_buf_len);
};

int sched_master_init(struct sched_master *p_master);

int sched_master_free(struct sched_master *p_master);

int sched_master_dispatch(struct sched_master *p_master);

#endif
