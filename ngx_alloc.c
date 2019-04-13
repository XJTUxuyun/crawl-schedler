
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include "ngx_alloc.h"



void *
ngx_alloc(size_t size)
{
    void  *p;

    p = malloc(size);
    if (p == NULL) {
        //ngx_error(NGX_LOG_EMERG, log, ngx_errno, "malloc(%uz) failed", size);
    }

    // ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, log, 0, "malloc: %p:%uz", p, size);

    return p;
}


void *
ngx_calloc(size_t size)
{
    void  *p;

    p = ngx_alloc(size);

    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


#if (NGX_HAVE_POSIX_MEMALIGN)

void *
ngx_memalign(size_t alignment, size_t size)
{
    void  *p;
    int    err;

    err = posix_memalign(&p, alignment, size);

    if (err) {
        //ngx_log_error(NGX_LOG_EMERG, log, err, "posix_memalign(%uz, %uz) failed", alignment, size);
        p = NULL;
    }

    //ngx_log_debug3(NGX_LOG_DEBUG_ALLOC, log, 0, "posix_memalign: %p:%uz @%uz", p, size, alignment);

    return p;
}

#elif (NGX_HAVE_MEMALIGN)

void *
ngx_memalign(size_t alignment, size_t size)
{
    void  *p;

    p = memalign(alignment, size);
    if (p == NULL) {
        // ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "memalign(%uz, %uz) failed", alignment, size);
    }

    //ngx_log_debug3(NGX_LOG_DEBUG_ALLOC, log, 0, "memalign: %p:%uz @%uz", p, size, alignment);

    return p;
}

#endif
