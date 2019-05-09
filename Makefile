CC = gcc -g

CFLAGS = -std=c11

LIBS = -lpthread

.PHONY: all	clean

all: crawl_scheduler

crawl_scheduler:	main.o	sched.o	threadpool.o	log.o	repository.o	hashmap.o	binlog.o	tools.o		cJSON.o		uuid4.o ngx_alloc.o ngx_palloc.o common.o
	$(CC) $(CFLAGS) $(LIBS) -o crawl_scheduler main.o	sched.o threadpool.o	log.o	repository.o	hashmap.o	binlog.o	tools.o \
	cJSON.o		uuid4.o	ngx_alloc.o ngx_palloc.o common.o

main.o:	main.c
	$(CC) $(CFALGS) -c main.c

sched:	sched.c
	$(CC) $(CFLAGS) -c sched.c

threadpool.o:	threadpool.c
	$(CC)	$(CFLAGS) -c threadpool.c

log.o:	log.c
	$(CC)	$(CFLAGS) -c log.c

repository.o:	repository.c
	$(CC)	$(CFLAGS) -c repository.c

hashmap.o:	hashmap.c
	$(CC)	$(CFLAGS) -c hashmap.c

binlog.o:	binlog.c
	$(CC)	$(CFLAGS) -c binlog.c

tools.o:	tools.c
	$(CC)	$(CFLAGS) -c tools.c

cJSON.o:	cJSON.c
	$(CC)	$(CFLAGS) -c cJSON.c

uuid4.o:	uuid4.c
	$(CC)	$(CFLAGS) -c uuid4.c

ngx_palloc.o:	ngx_palloc.c
	$(CC)	$(CFALGS) -c ngx_palloc.c

ngx_alloc.o:	ngx_alloc.c
	$(CC)	$(CFLAGS) -c ngx_alloc.c

common.o:	common.c
	$(CC)	$(CFLAGS) -c common.c

clean:
	-rm crawl_scheduler
	-rm *.o
	-rm *.gch
