#include "sched.h"
#include "queue.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

int sched_slaver_init(struct sched_master *p_master, struct sched_slaver **pp_slaver);

int sched_slaver_free(struct sched_slaver *p_slaver);

int handler_slaver_connection(struct sched_master *p_master);

int handler_slaver_disconnection(struct sched_slaver *p_slaver);

int handler_data_read(struct sched_slaver *p_slaver);

int handler_data_write(struct sched_slaver *p_slaver);

int set_fd_nonblock(int fd);

void slaver_work_wrapper(void *arg);

int sched_master_init(struct sched_master *p_master){
	if(p_master->master_construct == NULL){
		log_fatal("need set master_construct function");
		p_master->run_flag = 0;
		return -1;
	}

	if(p_master->master_destruct == NULL){
		log_fatal("need set master_destruct function");
		p_master->run_flag = 0;
		return -1;
	}

	if(p_master->slaver_construct == NULL){
		log_fatal("need set slaver_construct function");
		p_master->run_flag = 0;
		return -1;
	}

	if(p_master->slaver_destruct == NULL){
		log_fatal("need set slaver_destruct function");
		p_master->run_flag = 0;
		return -1;
	}

	if(p_master->slaver_work == NULL){
		log_fatal("need set slaver_work function");
		p_master->run_flag = 0;
		return -1;
	}

	if((p_master->sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		log_fatal("socket() error->%s", strerror(errno));
		p_master->run_flag = 0;
		return -1;
	};

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = inet_addr(p_master->ip),
		.sin_port = htons(p_master->port)
	};

	if(bind(p_master->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1){
		log_fatal("bind() error->%s", strerror(errno));
		goto failed;
	}

	if(listen(p_master->sock_fd, 100) == -1){
		log_fatal("listen() error->%s", strerror(errno));
		goto failed;
	}

	set_fd_nonblock(p_master->sock_fd);

	if((p_master->epoll_fd = epoll_create(10)) == -1){
		log_fatal("epoll_create() error->%s", strerror(errno));
		goto failed;
	}

	if(p_master->master_construct(&p_master->p_global_data) == -1){
		log_fatal("master_construct");
		goto failed;
	}

	QUEUE_INIT(&p_master->queue);

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET,
		.data = {
			.fd = p_master->sock_fd
		}
	};

	if(epoll_ctl(p_master->epoll_fd, EPOLL_CTL_ADD, p_master->sock_fd, &ev) == -1){
		log_fatal("epoll_ctl() error->%s", strerror(errno));
		goto failed;
	}

	// threadpool
	p_master->threadpool = NULL;
	if((p_master->threadpool = threadpool_create(4, 64, 0)) == NULL){
		log_fatal("threadpool_create() error");
		goto failed;
	}

	// 1Mb mempory pool for sched
	p_master->mempool = NULL;
	if(!(p_master->mempool = ngx_create_pool(1024000))){
		log_fatal("ngx_create_pool() error");
		goto failed;
	}

	p_master->run_flag = 1;

	return 0;

failed:
	close(p_master->sock_fd);
	close(p_master->epoll_fd);
	if(p_master->threadpool)
		threadpool_destroy(p_master->threadpool, 0);
	if(p_master->mempool)
		ngx_destroy_pool(p_master->mempool);
	p_master->run_flag = 0;
	return -1;
}

int sched_master_free(struct sched_master *p_master){
	
	for(;;){
		if(QUEUE_EMPTY(&p_master->queue)){
			break;
		}else{
			QUEUE *q = QUEUE_HEAD(&p_master->queue);
			QUEUE_REMOVE(q);
			QUEUE_INIT(q);
			struct sched_slaver *p_slaver = QUEUE_DATA(q, struct sched_slaver, wq);
			if(p_slaver != NULL){
				sched_slaver_free(p_slaver);
			}
		}
	}

	if(p_master->master_destruct != NULL){
		p_master->master_destruct(p_master->p_global_data);
	}
	
	close(p_master->sock_fd);
	// sock_fd need remove from epoll?
	close(p_master->epoll_fd);
	if(p_master->threadpool)
		threadpool_destroy(p_master->threadpool, 0);

	if(p_master->mempool)
		ngx_destroy_pool(p_master->mempool);

	log_info("sched master free success");
	return 0;
}

int sched_master_dispatch(struct sched_master *p_master){
	// event loop
	while(p_master->run_flag){
		struct epoll_event events[10] = {0, {0}};
		int nfds = epoll_wait(p_master->epoll_fd, events, 10, 10000);

		if(nfds == -1){
			// something error occur with epoll, exit graceful
			log_fatal("epoll wait error, set flag to zero, program will exit");
			p_master->run_flag = 0;
			continue;
		}else if(nfds == 0){
			// timeout
			continue;
		}

		for(int i=0; i<nfds; i++){
			if(events[i].data.fd == p_master->sock_fd){
				// new connection
				if(handler_slaver_connection(p_master) == -1){
					// error
					log_error("handler_new_connection() failed->%s", strerror(errno));
				}
				continue;
			}

			if(events[i].events & EPOLLIN){
				// new data need read
				if(handler_data_read((struct sched_slaver *)events[i].data.ptr) == -1){
					// error
					log_error("handler_data_read() error");
				}
				continue;
			}

			if(events[i].events & EPOLLOUT){
				// new data need write
				if(handler_data_write((struct sched_slaver *)events[i].data.ptr) == -1){
					// error
					log_error("handler_data_write() error");
				}
				continue;
			}

			// unknow reason
		}
	}

	return 0;
}

int sched_slaver_init(struct sched_master *p_master, struct sched_slaver **pp_slaver){
	if(!p_master){
		return -1;
	}

	if(*pp_slaver){
		return -1;
	}

	//if((*pp_slaver = (struct sched_slaver *)malloc(sizeof(struct sched_slaver))) == NULL){
	if((*pp_slaver = (struct sched_slaver *)ngx_palloc(p_master->mempool, sizeof(struct sched_slaver))) == NULL){
		log_error("malloc() error->%s", strerror(errno));
		return -1;
	}

	bzero(*pp_slaver, sizeof(struct sched_slaver));
	(*pp_slaver)->p_master = p_master;
	(*pp_slaver)->slaver_construct = p_master->slaver_construct;
	(*pp_slaver)->slaver_destruct = p_master->slaver_destruct;
	(*pp_slaver)->slaver_work = p_master->slaver_work;

	QUEUE_INIT(&(*pp_slaver)->wq);
	QUEUE_INSERT_TAIL(&p_master->queue, &(*pp_slaver)->wq);

	if(!(*pp_slaver)->slaver_construct){
		return 0;
	}

	if((*pp_slaver)->slaver_construct(p_master->p_global_data, &(*pp_slaver)->p_private_data)){
		//free(*pp_slaver);
		ngx_pfree(p_master->mempool, *pp_slaver);
		*pp_slaver = NULL;
		return -1;
	}

	return 0;
}

int sched_slaver_free(struct sched_slaver *p_slaver){
	if(!p_slaver){
		return 0;
	}

	QUEUE_REMOVE(&p_slaver->wq);

	if(close(p_slaver->sock_fd) == -1){
		log_error("close() error->%s", strerror(errno));
	}

	if(p_slaver->slaver_destruct){
		int ret = p_slaver->slaver_destruct(p_slaver->p_master->p_global_data, p_slaver->p_private_data);
		if(ret){
			//free(p_slaver);
			log_error("slaver_destruct failed");
		}
	}

	//free(p_slaver);
	ngx_pfree(p_slaver->p_master->mempool, p_slaver);
	log_info("slaver->%s:%d free", inet_ntoa(p_slaver->addr.sin_addr), ntohs(p_slaver->addr.sin_port));
	return 0;
}

int handler_slaver_connection(struct sched_master *p_master){
	// create new slaver
	struct sched_slaver *p_slaver = NULL;
	if(sched_slaver_init(p_master, &p_slaver) == -1){
		return -1;
	}

	int len = sizeof(struct sockaddr);
	if((p_slaver->sock_fd = accept(p_master->sock_fd, (struct sockaddr *)&p_slaver->addr, &len)) == -1){
		// error with accept
		sched_slaver_free(p_slaver);
		return -1;
	}

	set_fd_nonblock(p_slaver->sock_fd);

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET | EPOLLONESHOT,
		.data.ptr = p_slaver
	};

	if(epoll_ctl(p_master->epoll_fd, EPOLL_CTL_ADD, p_slaver->sock_fd, &ev) == -1){
		sched_slaver_free(p_slaver);
		return -1;
	}

	log_info("connection->%s:%d success", inet_ntoa(p_slaver->addr.sin_addr), ntohs(p_slaver->addr.sin_port));
	return 0;
}

int handler_slaver_disconnection(struct sched_slaver *p_slaver){
	char str[128] = {0};
	if(!p_slaver){
		return -1;
	}

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLOUT,
		.data.ptr = p_slaver
	};

	if(epoll_ctl(p_slaver->p_master->epoll_fd, EPOLL_CTL_DEL, p_slaver->sock_fd, &ev) == -1){
		log_error("epoll_ctl del fd error->%s", strerror(errno));
		return -1;
	}

	log_debug(str, "%s:%d", inet_ntoa(p_slaver->addr.sin_addr), ntohs(p_slaver->addr.sin_port));

	if(sched_slaver_free(p_slaver) == -1){
		return -1;
	}
	
	log_debug("disconnection->%s success", str);
	return 0;
}

int handler_data_read(struct sched_slaver *p_slaver){
	if(!p_slaver){
		return -1;
	}

	p_slaver->src_buf_len = read(p_slaver->sock_fd, p_slaver->src_buf, SOURCE_BUFFER_LEN_MAX);

	if(p_slaver->src_buf_len == -1){
		// indicates error
		log_error("read() error->%s", strerror(errno));
		return handler_slaver_disconnection(p_slaver);
	}

	if(p_slaver->src_buf_len == 0){
		// end of file
		return handler_slaver_disconnection(p_slaver);
	}
	
	if(threadpool_add(p_slaver->p_master->threadpool, slaver_work_wrapper, p_slaver, 0) != 0){
		log_error("threadpool add task error");
		return -1;
	}

	return 0;
}

int handler_data_write(struct sched_slaver *p_slaver){
	if(!p_slaver){
		return -1;
	}

	if(p_slaver->res_buf_len > 0){
		// write data
		int n;
		if((n = write(p_slaver->sock_fd, p_slaver->res_buf, p_slaver->res_buf_len)) == -1){
			log_error("write() error->%s", strerror(errno));
		}

		if(n < p_slaver->res_buf_len){
			log_error("some data hasnot write");
		}
	}

	struct epoll_event ev = {
		.events = EPOLLIN | EPOLLET | EPOLLONESHOT,
		.data.ptr = p_slaver
	};

	if(epoll_ctl(p_slaver->p_master->epoll_fd, EPOLL_CTL_MOD, p_slaver->sock_fd, &ev) == -1){
		log_error("epoll_ctl set slaver read error->%s this slaver cannot read data forever", strerror(errno));
		return -1;
	}

	bzero(p_slaver->src_buf, SOURCE_BUFFER_LEN_MAX);
	p_slaver->src_buf_len = 0;
	bzero(p_slaver->res_buf, RESULT_BUFFER_LEN_MAX);
	p_slaver->res_buf_len = 0;

	return 0;
}

int set_fd_nonblock(int fd){
	int flags = fcntl(fd, F_GETFL, 0);
	if(flags == -1){
		log_error("fcntl() error->%s", strerror(errno));
		return -1;
	}

	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1){
		log_error("fcntl() error->%s", strerror(errno));
		return -1;
	}

	return 0;
}

void slaver_work_wrapper(void *arg){
	struct sched_slaver *p_slaver = (struct sched_slaver *)arg;
	bzero(p_slaver->res_buf, RESULT_BUFFER_LEN_MAX);
	p_slaver->res_buf_len = p_slaver->slaver_work(p_slaver->p_master->p_global_data, p_slaver->p_private_data, p_slaver->res_buf, p_slaver->src_buf, p_slaver->src_buf_len);

	// work done, modify fd status and wait to read again
	struct epoll_event ev = {
		.events = EPOLLOUT | EPOLLET | EPOLLONESHOT,
		.data.ptr = p_slaver
	};

	if(epoll_ctl(p_slaver->p_master->epoll_fd, EPOLL_CTL_MOD, p_slaver->sock_fd, &ev) == -1){
		// epoll ctl error;
		log_error("epoll_ctl error->%s this slaver cannot send data forever", strerror(errno));
	}
}
