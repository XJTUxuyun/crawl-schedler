#include "common.h"
#include "log.h"


#define ERROR_STR_GEN(val, name, str)	case ERR_##name : return str;
const char *error_code_desc(enum ERR code){
	switch(code){
		ERROR_MAP(ERROR_STR_GEN)
	default:
		return "unknow error";
	}
}
#undef ERROR_STR_GEN

#define ERROR_NAME_GEN(val, name, str)	case ERR_##name : return #name;
const char *error_code_name(enum ERR code){
	switch(code){
		ERROR_MAP(ERROR_NAME_GEN)
	default:
		return "unknow name";
	}
}
#undef ERROR_NAME_GEN

#define API_STR_GEN(val, name, str)		case API_##name : return str;
const char *api_code_desc(enum API_RET code){
	switch(code){
		API_RET_MAP(API_STR_GEN)
	default:
		return "unknow api ret";
	}
}
#undef API_STR_GEN

#define API_NAME_GEN(val, name, str)	case API_##name : return #name;
const char * api_code_name(enum API_RET code){
	switch(code){
		API_RET_MAP(API_NAME_GEN)
	default:
		return "unknow api ret";
	}
}
#undef API_NAME_GEN


int mempool_initial(struct mempool *pool){
	if(!pool)
		return ERR_NULL_POINTER;
	
	if(pthread_mutex_init(&pool->mutex, NULL)){
		log_fatal("init mempool mutex error->%s", strerror(errno));
		return ERR_MUTEX_INIT;
	}

	if(!(pool->pool = ngx_create_pool(1024*1000*10))){
		log_fatal("create mempool pool error");
		return ERR_MEMPOOL;
	}
	return OK;
}

int mempool_destroy(struct mempool *pool){
	if(pthread_mutex_lock(&pool->mutex)){
		log_fatal("mempool mutex lock error->%s", strerror(errno));
		return ERR_MUTEX_LOCK;
	}

	ngx_destroy_pool(pool->pool);

	if(pthread_mutex_unlock(&pool->mutex)){
		log_fatal("mempool mutex unlock error->%s", strerror(errno));
		return ERR_MUTEX_UNLOCK;
	}

	pthread_mutex_destroy(&pool->mutex);
	return OK;
}

int mempool_alloc(struct mempool *pool, size_t size, void **p){
	if(pthread_mutex_lock(&pool->mutex)){
		log_fatal("mempool mutex lock error->%s", strerror(errno));
		return ERR_MUTEX_LOCK;
	}
	*p = ngx_palloc(pool->pool, size);
	if(pthread_mutex_unlock(&pool->mutex)){
		log_fatal("mempool mutex unlock error->%s", strerror(errno));
		return ERR_MUTEX_UNLOCK;
	}
	
	return OK;
}

int mempool_free(struct mempool *pool, void *p){
	if(pthread_mutex_lock(&pool->mutex)){
		log_fatal("mempool mutex lock error->%s", strerror(errno));
		return ERR_MUTEX_LOCK;
	}
	ngx_pfree(pool->pool, p);
	if(pthread_mutex_unlock(&pool->mutex)){
		log_fatal("mempool mutex unlock error->%s", strerror(errno));
		return ERR_MUTEX_UNLOCK;
	}
	return OK;
}
