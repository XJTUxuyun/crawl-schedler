#ifndef __REPOSITORY_H
#define __REPOSITORY_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>

int global_repository_construct(void **pp_global);

int global_repository_destruct(void *p_gloabl);

int private_repository_construct(void *p_global, void **pp_private);

int private_repository_destruct(void *p_global, void *p_private);

int repository_work(void *p_gloabl, void *p_private, char *res, char *src, int src_len);

#endif
