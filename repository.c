#include "repository.h"
#include "queue.h"
#include "hashmap.h"
#include "log.h"
#include "common.h"
#include "cJSON.h"
#include "uuid4.h"

#include <errno.h>
#include <time.h>

struct item{
	char uuid[36];

	void *global_fifo_queue[2];
	void *private_fifo_queue[2];

	int retry_times;
	int data_len;
	void *data;

	time_t ctime;
	time_t atime;
	time_t mtime;
};

struct task{
	char key[128];
	// global task need mutex and private task need hashmap, so used anonymous union
	union{
		pthread_mutex_t mutex;
		void *processing_item_hashmap;
	};
	struct task *ps_task;		// Pointer to parent struct task, NULL indicate global task

	struct global_repo *global_repo;

	void *fifo_queue[2];
};

struct private_repo{
	struct global_repo *ps_global;				/* pointer to global_repo*/
	void *repo_queue[2];						/* used for queue, */
	void *task_hashmap;							/* task hashmap,*/
};

struct global_repo{
	pthread_mutex_t repo_queue_mutex;			/* mutex for repo queue*/
	void *repo_queue[2];						/* used for */

	pthread_mutex_t task_hashmap_mutex;			/* */
	void *task_hashmap;							/* */

	pthread_mutex_t completed_queue_mutex;		/* */
	void *completed_queue[2];					/* */
	int completed_queue_size;

	struct mempool mempool;
};

int item_new(struct task *ps_task, struct item **pps_item, void *data, int len);

int item_del(struct task *ps_task, struct item *ps_item);

int task_new(struct global_repo *global_repo, struct task **pps_task, struct task *ps_task, const char *key);

int task_add_item(struct task *ps_task, struct item *ps_item);

int task_get_item(struct task *ps_task, struct item **pps_item);

int task_ack_item(struct global_repo *ps_global, struct task *ps_task, char *uuid, int flag);

int task_del_item(struct task *ps_task, struct item *ps_item);

int task_del(struct task *ps_task);

int private_repo_new(struct global_repo *ps_global, struct private_repo **pps_private);

int private_task_iterate(void *udata, void *task);

int private_repo_del(struct private_repo *ps_private);

int private_repo_del(struct private_repo *p_private);

int global_repo_new(struct global_repo **pps_global);

int global_task_iterate(void *udata, void *task);

int global_repo_del(struct global_repo *ps_global);

int global_repo_inspector(struct global_repo *p_global);

int repo_work(struct global_repo *ps_global, struct private_repo *ps_private, char *res, char *src, int src_len);

int parse_req(const cJSON *req, const char *filed, void **value);

int private_repo_get_task(struct private_repo *ps_private, struct task **pps_task, char *key);

int pack_result(char *res, int n, ...);

int item_new(struct task *ps_task, struct item **pps_item, void *data, int len){
	if(!ps_task || !data)
		return ERR_NULL_POINTER;

	if(!ps_task->ps_task){
		log_error("only private task can new item");
		return ERR_NULL_POINTER;
	}

	if(mempool_alloc(&ps_task->global_repo->mempool, sizeof(struct item), (void **)pps_item)){
		return ERR_MEMPOOL;
	}
	//if((*pps_item = (struct item *)malloc(sizeof(struct item))) == NULL){
	//	return ERR_MALLOC;
	//}
	bzero(*pps_item, sizeof(struct item));

	// generate uuid
	uuid4_generate((*pps_item)->uuid);

	// ctime
	time(&(*pps_item)->ctime);
	(*pps_item)->mtime = (*pps_item)->ctime;
	(*pps_item)->atime = (*pps_item)->ctime;

	if(mempool_alloc(&ps_task->global_repo->mempool, len + 1, &(*pps_item)->data)){
		mempool_free(&ps_task->global_repo->mempool, *pps_item);
		return ERR_MEMPOOL;
	}
	memcpy((*pps_item)->data, data, len);
	(*pps_item)->data_len = len;

	// different queue type should hanlder seperate
	// handler fifo queue
	do{
		QUEUE_INIT(&(*pps_item)->global_fifo_queue);
		QUEUE_INIT(&(*pps_item)->private_fifo_queue);

		struct task *ps_parent = ps_task->ps_task;

		if(pthread_mutex_lock(&ps_parent->mutex)){
			log_error("struct task mutex lock op error->%s", strerror(errno));
			//QUEUE_REMOVE(&ps_task->fifo_queue);
			return ERR_MUTEX_LOCK;
		}
		// item add into private task queue
		QUEUE_INSERT_TAIL(&ps_task->fifo_queue, &(*pps_item)->private_fifo_queue);
		// item add into global task queue
		QUEUE_INSERT_TAIL(&ps_parent->fifo_queue, &(*pps_item)->global_fifo_queue);

		if(pthread_mutex_unlock(&ps_parent->mutex)){
			log_error("struct task mutex unlock op error->%s", strerror(errno));
			//QUEUE_REMOVE(&ps_task->fifo_queue);
			return ERR_MUTEX_UNLOCK;
		}
	}while(0);

	return 0;
}

int item_del(struct task *ps_task, struct item *ps_item){
	if(!ps_item || !ps_task)
		return ERR_NULL_POINTER;
	
	QUEUE_REMOVE(&ps_item->private_fifo_queue);
	QUEUE_REMOVE(&ps_item->global_fifo_queue);
	
	if(ps_item->data)
		mempool_free(&ps_task->global_repo->mempool, ps_item->data);
		//free(ps_item->data);
	// free(ps_item); mempool_free
	mempool_free(&ps_task->global_repo->mempool, ps_item);
	return 0;
}

int task_new(struct global_repo *global_repo, struct task **pps_task, struct task *ps_task, const char *key){
	if(!key || !global_repo)
		return ERR_NULL_POINTER;

	if(mempool_alloc(&global_repo->mempool, sizeof(struct task), (void **)pps_task)){
		log_error("malloc task error");
		return ERR_MEMPOOL;
	}
	//if((*pps_task = (struct task *)malloc(sizeof(struct task))) == NULL){
	//	log_error("malloc task error");
	//	return ERR_MALLOC;
	//}
	bzero(*pps_task, sizeof(struct task));
	strcpy((*pps_task)->key, key);
	(*pps_task)->global_repo = global_repo;
	
	QUEUE_INIT(&(*pps_task)->fifo_queue);

	if(!ps_task){	// ps_task == NULL
		// for gloal purpose
		if(pthread_mutex_init(&(*pps_task)->mutex, NULL)){
			log_error("initial task mutex error->%s", strerror(errno));
			// free(*pps_task);
			mempool_free(&global_repo->mempool, *pps_task);
			return ERR_MUTEX_INIT;
		}
	}else{
		// for private purpose
		(*pps_task)->ps_task = ps_task;
		// used for temprory stored processing item
		// hashmap new success?
		(*pps_task)->processing_item_hashmap = hashmap_new();
		if(!(*pps_task)->processing_item_hashmap){
			mempool_free(&global_repo->mempool, *pps_task);
			return ERR_HASHMAP;
		}
	}

	return 0;
}

int task_add_item(struct task *ps_task, struct item *ps_item){
	if(!ps_task|| !ps_item)
		return ERR_NULL_POINTER;

	if(!ps_task->ps_task){
		// indicate this is a global task
		if(pthread_mutex_lock(&ps_task->mutex)){
			log_error("task add item lock error->%s", strerror(errno));
			return ERR_MUTEX_LOCK;
		}
		QUEUE_INIT(&ps_item->global_fifo_queue);
		QUEUE_INSERT_TAIL(&ps_task->fifo_queue, &ps_item->global_fifo_queue);
		if(pthread_mutex_unlock(&ps_task->mutex)){
			log_error("task add item unlock error->%s", strerror(errno));
			return ERR_MUTEX_UNLOCK;
		}
	}else{
		// indicate this is a private task
		if(pthread_mutex_lock(&ps_task->ps_task->mutex)){
			log_error("task add item lock error->%s", strerror(errno));
			return ERR_MUTEX_LOCK;
		}
		QUEUE_INIT(&ps_item->private_fifo_queue);
		QUEUE_INSERT_TAIL(&ps_task->fifo_queue, &ps_item->private_fifo_queue);
		QUEUE_INIT(&ps_item->global_fifo_queue);
		QUEUE_INSERT_TAIL(&ps_task->ps_task->fifo_queue, &ps_item->global_fifo_queue);
		if(pthread_mutex_unlock(&ps_task->ps_task->mutex)){
			log_error("task add item unlock error->%s", strerror(errno));
			return ERR_MUTEX_UNLOCK;
		}
	}
	return 0;
}

int task_get_item(struct task *ps_task, struct item **pps_item){
	if(!ps_task)
		return ERR_NULL_POINTER;

	int ret = OK;
	if(ps_task->ps_task){
		// indicate this is a private task
		// check private task is empty?
		char flag = 0;
		if(pthread_mutex_lock(&ps_task->ps_task->mutex)){
			log_error("task get item lock error->%s", strerror(errno));
			return ERR_MUTEX_LOCK;
		}
		if(QUEUE_EMPTY(&ps_task->fifo_queue)){
			// private fifo queue is empty
			// get item from global queue
			flag = 1;
		}else{
			// private fifo queue not empty, get item from private queue
			QUEUE *q = QUEUE_HEAD(&ps_task->fifo_queue);
			QUEUE_REMOVE(q);
			QUEUE_INIT(q);
			*pps_item = QUEUE_DATA(q, struct item, private_fifo_queue);

			QUEUE_REMOVE(&(*pps_item)->global_fifo_queue);
			QUEUE_INIT(&(*pps_item)->global_fifo_queue);
		}
		if(pthread_mutex_unlock(&ps_task->ps_task->mutex)){
			log_error("task get item error->%s", strerror(errno));
			return ERR_MUTEX_UNLOCK;
		}

		// private task queue is empty, get item from global task queue
		if(flag)
			ret = task_get_item(ps_task->ps_task, pps_item);

		// add item to processing_item_hashmap
		if(*pps_item){
			hashmap_put(ps_task->processing_item_hashmap, (*pps_item)->uuid, *pps_item);
			time(&(*pps_item)->atime);
		}

	}else{
		// indicate this is a global task
		if(pthread_mutex_lock(&ps_task->mutex)){
			log_error("task get item lock error->%s", strerror(errno));
			return ERR_MUTEX_LOCK;
		}

		if(QUEUE_EMPTY(&ps_task->fifo_queue)){
			// fifo queue is empty
			ret = ERR_QUEUE_EMPTY;
		}else{
			// remove from global fifo queue
			QUEUE *q = QUEUE_HEAD(&ps_task->fifo_queue);
			QUEUE_REMOVE(q);
			QUEUE_INIT(q);
			*pps_item = QUEUE_DATA(q, struct item, global_fifo_queue);

			// remove from private fifo queue
			QUEUE_REMOVE(&(*pps_item)->private_fifo_queue);
			QUEUE_INIT(&(*pps_item)->private_fifo_queue);
		}

		if(pthread_mutex_unlock(&ps_task->mutex)){
			log_error("task get item unlock error->%s", strerror(errno));
			return ERR_MUTEX_UNLOCK;
		}
	}
	return ret;
}

int task_ack_item(struct global_repo *ps_global, struct task *ps_task, char *uuid, int flag){
	if(!ps_task || !uuid || !ps_global)
		return ERR_NULL_POINTER;

	if(!ps_task->ps_task){
		// global task cannot use ack op
		log_error("task is global and cannot use ack op");
		return ERR_NULL_POINTER;
	}

	struct item *ps_item;
	if(hashmap_get(ps_task->processing_item_hashmap, uuid, (void **)&ps_item) != MAP_OK)
		return ERR_ITEM_UNEXIST;

	// confirm next behavor
	if(flag == 1){
		// success, add item to completed queue
		if(pthread_mutex_lock(&ps_global->completed_queue_mutex) != 0){
			log_error("lock completed_queue_mutex error->%s", strerror(errno));
			return ERR_MUTEX_LOCK;
		}

		// remove from hashmap and add to completed queue
		// add item.global_fifo_queue to completed queue
		hashmap_remove(ps_task->processing_item_hashmap, uuid);
		QUEUE_INIT(&ps_item->global_fifo_queue);
		QUEUE_INSERT_TAIL(&ps_global->completed_queue, &ps_item->global_fifo_queue);
		ps_global->completed_queue_size++;

		if(pthread_mutex_unlock(&ps_global->completed_queue_mutex) != 0){
			log_error("unlock completed_queue_mutex error->%s", strerror(errno));
			return ERR_MUTEX_UNLOCK;
		}
	}else{
		// failed, item back to queue
		ps_item->retry_times++;
		time(&ps_item->mtime);
		return task_add_item(ps_task, ps_item);
	}

	return OK;
}

int task_del_item(struct task *ps_task, struct item *ps_item){
	if(!ps_task || !ps_item)
		return ERR_NULL_POINTER;

	if(!ps_task->ps_task){
		// indicate this is a global task
		if(pthread_mutex_lock(&ps_task->mutex)){
			log_error("task del item lock error->%s", strerror(errno));
			return ERR_MUTEX_LOCK;
		}

		QUEUE_REMOVE(&ps_item->global_fifo_queue);
		QUEUE_INIT(&ps_item->global_fifo_queue);

		if(pthread_mutex_unlock(&ps_task->mutex)){
			log_error("task del item unlock error->%s", strerror(errno));
			return ERR_MUTEX_UNLOCK;
		}
	}else{
		// indicate this is a private task
		if(pthread_mutex_lock(&ps_task->ps_task->mutex)){
			log_error("task del item lock error->%s", strerror(errno));
			return ERR_MUTEX_LOCK;
		}

		QUEUE_REMOVE(&ps_item->private_fifo_queue);
		QUEUE_INIT(&ps_item->private_fifo_queue);
		QUEUE_REMOVE(&ps_item->global_fifo_queue);
		QUEUE_INIT(&ps_item->global_fifo_queue);

		if(pthread_mutex_lock(&ps_task->ps_task->mutex)){
			log_error("task del item unlock error->%s", strerror(errno));
			return ERR_MUTEX_UNLOCK;
		}
	}
	return 0;
}

int task_free_internal(void *data, void *item){
	// lock contention to avoid service stop
	struct task *ps_task = (struct task *)data;
	struct item *ps_item = (struct item *)item;
	return task_add_item(ps_task->ps_task, ps_item);
}

int task_free(struct task *ps_task){
	if(ps_task == NULL)
		return ERR_NULL_POINTER;
	
	if(ps_task->ps_task == NULL){
		// indicate ps_task is a global task
		// save unhandler item
	}else{
		// indicate ps_task is a private task
		// processing item will back into global task queue
		hashmap_iterate(ps_task->processing_item_hashmap, task_free_internal, ps_task);
		hashmap_free(ps_task->processing_item_hashmap);
	}
	
	// free memory
	// free(ps_task);
	mempool_free(&ps_task->global_repo->mempool, ps_task);
	return 0;
}

int private_repo_new(struct global_repo *ps_global, struct private_repo **pps_private){
	if(!ps_global){
		log_error("ps_global is null");
		return ERR_NULL_POINTER;
	}
	if(mempool_alloc(&ps_global->mempool, sizeof(struct private_repo), (void **)pps_private)){
		log_error("malloc struct private_repo error");
		return ERR_MALLOC;
	}
	//if((*pps_private = (struct private_repo *)malloc(sizeof(struct private_repo))) == NULL){
	//	log_error("malloc struct private_repo error");
	//	return ERR_MALLOC;
	//}

	bzero(*pps_private, sizeof(struct private_repo));

	(*pps_private)->ps_global = ps_global;

	(*pps_private)->task_hashmap = hashmap_new();
	if(!(*pps_private)->task_hashmap)
		return ERR_HASHMAP;

	QUEUE_INIT(&(*pps_private)->repo_queue);

	pthread_mutex_lock(&ps_global->repo_queue_mutex);
	QUEUE_INSERT_TAIL(&ps_global->repo_queue, &(*pps_private)->repo_queue);
	pthread_mutex_unlock(&ps_global->repo_queue_mutex);

	return 0;
}

int private_task_iterate(void *udata, void *task){
	struct task *ps_task = (struct task *)task;
	return task_free(ps_task);
}

int private_repo_del(struct private_repo *ps_private){
	// free task
	hashmap_iterate(ps_private->task_hashmap, private_task_iterate, NULL);
	hashmap_free(ps_private->task_hashmap);

	pthread_mutex_lock(&ps_private->ps_global->repo_queue_mutex);
	QUEUE_REMOVE(&ps_private->repo_queue);
	pthread_mutex_unlock(&ps_private->ps_global->repo_queue_mutex);

	mempool_free(&ps_private->ps_global->mempool, ps_private);

	return 0;
}

int global_repo_new(struct global_repo **pps_global){
	// mempool is unvariable now
	if((*pps_global = (struct global_repo *)malloc(sizeof(struct global_repo))) == NULL){
		log_fatal("malloc struct global_repo error");
		return ERR_NULL_POINTER;
	}

	bzero(*pps_global, sizeof(struct global_repo));

	QUEUE_INIT(&(*pps_global)->repo_queue);
	QUEUE_INIT(&(*pps_global)->completed_queue);

	if(!((*pps_global)->task_hashmap = hashmap_new())){
		log_fatal("create global task hashmap error");
		return ERR_HASHMAP;
	}

	if(mempool_initial(&(*pps_global)->mempool) != OK){
		log_fatal("initial repo mempool error");
		hashmap_free((*pps_global)->task_hashmap);
		return ERR_MEMPOOL;
	}

	if(pthread_mutex_init(&(*pps_global)->repo_queue_mutex, NULL) != 0){
		log_fatal("initial repo queue mutex error->%s", strerror(errno));
		hashmap_free((*pps_global)->task_hashmap);
		mempool_destroy(&(*pps_global)->mempool);
		return ERR_MUTEX_INIT;
	}
	if(pthread_mutex_init(&(*pps_global)->completed_queue_mutex, NULL) != 0){
		log_fatal("initial repo queue mutex error->%s", strerror(errno));
		hashmap_free((*pps_global)->task_hashmap);
		pthread_mutex_destroy(&(*pps_global)->repo_queue_mutex);
		mempool_destroy(&(*pps_global)->mempool);
		return ERR_MUTEX_INIT;
	}
	if(pthread_mutex_init(&(*pps_global)->task_hashmap_mutex, NULL) != 0){
		log_fatal("initial repo queue mutex error->%s", strerror(errno));
		hashmap_free((*pps_global)->task_hashmap);
		pthread_mutex_destroy(&(*pps_global)->repo_queue_mutex);
		pthread_mutex_destroy(&(*pps_global)->completed_queue_mutex);
		mempool_destroy(&(*pps_global)->mempool);
		return ERR_MUTEX_INIT;
	}

	return 0;
}

int global_task_iterate(void *udata, void *task){
	struct task *ps_task = (struct task *)task;
	return task_free(ps_task);
}

int global_repo_del(struct global_repo *ps_global){
	if(!ps_global)
		return ERR_NULL_POINTER;

	hashmap_iterate(ps_global->task_hashmap, global_task_iterate, NULL);

	pthread_mutex_destroy(&ps_global->task_hashmap_mutex);
	pthread_mutex_destroy(&ps_global->completed_queue_mutex);
	pthread_mutex_destroy(&ps_global->repo_queue_mutex);

	hashmap_free(ps_global->task_hashmap);
	mempool_destroy(&ps_global->mempool);
	free(ps_global);
	return 0;
}

int parse_req(const cJSON *req, const char *filed, void **value){
	cJSON *t_ = NULL;
	if((t_ = cJSON_GetObjectItem(req, filed)) == NULL){
		return ERR_ITEM_UNEXIST;
	}
	if((*value = t_->valuestring) == NULL){
		return ERR_NULL_POINTER;
	}
	return strlen(*value);
}

int private_repo_get_task(struct private_repo *ps_private, struct task **pps_task, char *key){
	if(!ps_private || !key)
		return ERR_NULL_POINTER;

	// check task exsit in private repo task hashmap
	if(hashmap_get(ps_private->task_hashmap, key, (void **)pps_task) == MAP_MISSING){
		// task not is private repo task hashmap, should create a new struct task
		// check task exist in global repo task hashmap
		struct task *t_ = NULL;

		// lock hashmap
		if(pthread_mutex_lock(&ps_private->ps_global->task_hashmap_mutex))
			return ERR_MUTEX_LOCK;

		if(hashmap_get(ps_private->ps_global->task_hashmap, key, (void **)&t_) == MAP_MISSING){
			// task is not in global repo task hashmap
			// create task and put into global task hashmap
			task_new(ps_private->ps_global, &t_, NULL, key);
			hashmap_put(ps_private->ps_global->task_hashmap, key, t_);
		}

		// unlock hashmap
		if(pthread_mutex_unlock(&ps_private->ps_global->task_hashmap_mutex))
			return ERR_MUTEX_UNLOCK;

		task_new(ps_private->ps_global, pps_task, t_, key);
		hashmap_put(ps_private->task_hashmap, key, *pps_task);
	}

	return OK;
}

int pack_result(char *res, int n, ...){
	char *k = NULL;
	char *v = NULL;
	cJSON *json = cJSON_CreateObject();
	if(!json)
		return ERR_NULL_POINTER;
	va_list args;
	va_start(args, n);
	while(n > 0){
		n--;
		// n is odd indicate it's value else key
		if(n % 2){
			k = va_arg(args, char *);
		}else{
			v = va_arg(args, char *);
			if(!v || !k)
				continue;
			cJSON *v_ = cJSON_CreateStringReference(v);
			if(!v_){
				va_end(args);
				cJSON_Delete(json);
				return ERR_NULL_POINTER;
			}
			cJSON_AddItemToObject(json, k, v_);
			v = NULL;
			k = NULL;
		}
	}
	va_end(args);

	if(cJSON_PrintPreallocated(json, res, 5120, 1) == 0){
		cJSON_Delete(json);
		return ERR_JSON_OP;
	}
	cJSON_Delete(json);
	return strlen(res);
}

/**
 * src:
 *		{
 *			'mid': 'xxxx'
 *			'op': 'get' ('put')
 *			'data': str
 *		}
 */
int repo_work(struct global_repo *ps_global, struct private_repo *ps_private, char *res, char *src, int src_len){
	int r;
	// check data
	cJSON *req = cJSON_Parse(src);
	if(req == NULL){
		// request data parse error.
		// something error occur
		log_error("cannot parse request data");
		cJSON_Delete(req);
		return pack_result(res, 2, "ret", "need obey protocal");
	}

	// parse mid
	char *mid;
	r = parse_req(req, "mid", (void **)&mid);
	if(r <= 0){
		log_error("parse request json error->%s", error_code_desc(r));
		cJSON_Delete(req);
		return pack_result(res, 2, "ret", "mid missing");
	}

	// parse operation
	char *op;
	r = parse_req(req, "op", (void **)&op);
	if(r <= 0){
		log_error("parse request json error->%s", error_code_desc(r));
		cJSON_Delete(req);
		return pack_result(res, 2, "ret", "op missing");
	}

	struct task *ps_task = NULL;
	r = private_repo_get_task(ps_private, &ps_task, mid);
	if(r != OK){
		log_error("get task from hashmap error->%s", error_code_desc(r));
		cJSON_Delete(req);
		return pack_result(res, 2, "ret", "get task error");
	}
	
	struct item *ps_item = NULL;
	if(!strcmp(op, "GET")){
		log_debug("get op");
		r = task_get_item(ps_task, &ps_item);
		if(r != OK){
			log_error("task_get_item error->%s", error_code_desc(r));
			pack_result(res, 2, "ret", "get item error, maybe empty");
		}else{
			pack_result(res, 6, "ret", "OK", "uuid", ps_item->uuid, "data", ps_item->data);
		}
	}else if(!strcmp(op, "PUT")){
		log_debug("put op");
		char *buf;
		int l = parse_req(req, "data", (void **)&buf);
		if(l > 0){
			r = item_new(ps_task, &ps_item, buf, l);
			if(r == OK){
				pack_result(res, 2, "ret", "OK");
			}else{
				pack_result(res, 2, "ret", "create and add item error");
			}
		}else{
			pack_result(res, 2, "ret", "cannot parse data");
		}
	}else if(!strcmp(op, "ACK")){
		log_debug("ack op");
		char *uuid;
		r = parse_req(req, "uuid", (void **)&uuid);
		if(r <= 0){
			log_error("parse uuid error");
			cJSON_Delete(req);
			return pack_result(res, 2, "ret", "parse uuid error");
		}
		char *status;
		r = parse_req(req, "status", (void **)&status);
		if(r <= 0){
			log_error("parse status error");
			cJSON_Delete(req);
			return pack_result(res, 2, "ret" , "parse status error");
		}
		int flags = 0;
		if(!strcmp(status, "OK")){
			flags = 1;
		}
		r = task_ack_item(ps_global, ps_task, uuid, flags);
		if(r == OK){
			pack_result(res, 2, "ret", "OK");
		}else{
			pack_result(res, 2, "ret", error_code_desc(r));
		}
	}else{
		// unknow op
		pack_result(res, 2, "ret", "unknow op");
	}
	// free req
	cJSON_Delete(req);
	return strlen(res);
}

int global_repository_construct(void **pp_global){
#define p_global (**(struct global_repo ***)&pp_global)

	if(global_repo_new(&p_global) == -1){
		goto failed;
	}

	log_info("global_repository_construct success");

#undef p_global
	return 0;

failed:
#undef p_global
	return -1;
}

int global_repository_destruct(void *p_global1){
#define p_global (*(struct global_repo **)&p_global1)

	if(global_repo_del(p_global) == -1){
		goto failed;
	}

	log_info("global_repository_destruct success");

#undef p_global
	return 0;

failed:
#undef p_global
	return -1;
}

int global_repository_inspector(void *p_global1){
#define p_global (*(struct global_repo **)&p_global1)

#undef p_global
}

int private_repository_construct(void *p_global1, void **pp_private){
#define p_global2 (*(struct global_repo **)&p_global1)
#define p_private (**(struct private_repo ***)&pp_private) 

	if(private_repo_new(p_global2, &p_private) == -1){
		goto failed;
	}

	log_info("private repository construct success");

#undef p_private
#undef p_global
	return 0;
failed:
#undef p_private
#undef p_global
	return -1;
}

int private_repository_destruct(void *p_global1, void *p_private1){
#define p_global (*(struct global_repo **)&p_global1)
#define p_private (*(struct private_repo **)&p_private1)

	if(private_repo_del(p_private) == -1){
		goto failed;
	}

	log_info("private repository destruct success");

#undef p_private
#undef p_global
	return 0;

failed:
#undef p_private
#undef p_global
	return -1;
}

int private_repository_inspector(void *p_global1, void *p_private1){
#define p_global (*(struct global_repo **)&p_global1)
#define p_private (*(struct private_repo **)&p_private1)

	if(private_repo_del(p_private) == -1){
		goto failed;
	}

	log_info("private repository destruct success");

#undef p_private
#undef p_global
	return 0;

failed:
#undef p_private
#undef p_global
	return -1;
}

int repository_work(void *p_global1, void *p_private1, char *res, char *src, int src_len){
#define p_global (*(struct global_repo **)&p_global1)
#define p_private (*(struct private_repo **)&p_private1)

	log_debug("recv->%s\n", src);
	// log_info("p_global %p, p_private %p, res %p src %p src_len %d", p_global, p_private, res, src, src_len);
	int res_len = repo_work(p_global, p_private, res, src, src_len);

#undef p_private
#undef p_global
	return res_len;

failed:
#undef p_private
#undef p_global
	return -1;
}
