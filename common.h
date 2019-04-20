#ifndef __COMMON_H
#define __COMMON_H

#include "ngx_palloc.h"

#include <pthread.h>
#include <errno.h>

#define OK 0

#define ERROR_MAP(XX)											\
	XX(0, OK, "success")										\
	XX(-1, NULL_POINTER, "using NULL pointer")					\
	XX(-2, MUTEX_LOCK, "pthread_mutex_lock error")				\
	XX(-3, MUTEX_UNLOCK, "pthread_mutex_unlock error")			\
	XX(-4, MUTEX_INIT, "pthread_mutex_init error")				\
	XX(-5, MALLOC, "malloc error")								\
	XX(-6, ITEM_UNEXIST, "not such item")						\
	XX(-7, QUEUE_EMPTY, "queue is empty")						\
	XX(-8, JSON_OP, "json error")								\
	XX(-9, MEMPOOL, "memory pool error")						\
	XX(-10, HASHMAP, "hashmap error")							\
	XX(1, UNKNOW, "unknow error")

#define ERROR_GEN(val, name, str)	ERR_##name = val,
enum ERR{
	ERROR_MAP(ERROR_GEN)
};
#undef ERROR_GEN

#define API_RET_MAP(XX)												\
	XX(0, SUCCESS, "success")										

#define API_RET_GEN(val, name, str)		API_##name = val,
enum API_RET{
	API_RET_MAP(API_RET_GEN)
};
#undef API_RET_GEN

const char *error_code_desc(enum ERR code);
const char *error_code_name(enum ERR code);
const char *api_code_desc(enum API_RET code);
const char *api_code_name(enum API_RET code);

struct mempool{
	pthread_mutex_t mutex;
	ngx_pool_t *pool;
};

int mempool_initial(struct mempool *pool);

int mempool_destroy(struct mempool *pool);

int mempool_alloc(struct mempool *pool, size_t size, void **p);

int mempool_free(struct mempool *pool, void *p);

#endif
