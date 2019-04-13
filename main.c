#include <stdio.h>
#include <stdlib.h>

#include "sched.h"
#include "log.h"
#include "repository.h"

pthread_mutex_t log_mutex;

void log_lock_fn(void *udata, int lock){
	if(lock == 0){
		pthread_mutex_unlock(&log_mutex);
	}

	if(lock == -1){
		pthread_mutex_lock(&log_mutex);
	}
}

int main(int argc, char **argv){
	FILE *log_fp;
	if((log_fp = fopen("crawl.log", "w")) == NULL){
		printf("open log file error\n");
		return -1;
	}

	if(pthread_mutex_init(&log_mutex, NULL)){
		printf("init log mutex error\n");
		fclose(log_fp);
		return -1;
	}

	log_set_fp(log_fp);
	log_set_level(LOG_INFO);
	log_set_lock(log_lock_fn);

	struct sched_master master = {
		.master_construct = global_repository_construct,
		.master_destruct = global_repository_destruct,
		.slaver_construct = private_repository_construct,
		.slaver_destruct = private_repository_destruct,
		.slaver_work = repository_work,
		.ip = "172.16.5.130",
		.port = 8888
	};

	if(sched_master_init(&master) == -1){
		printf("inti master error\n");
		return -1;
	}

	if(sched_master_dispatch(&master) == -2){
		printf("dispatch error");
		return -1;
	}

	log_info("sched fuck");

	sched_master_free(&master);
	return 0;
}
