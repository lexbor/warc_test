/*
* Copyright (C) 2019 Alexander Borisov
*
* Author: Alexander Borisov <borisov@lexbor.com>
*/

#ifndef PRGM_GZIP_H
#define PRGM_GZIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lexbor/utils/base.h"

#include <zlib.h>


#define LXB_UTILS_GZIP_CHUNK 4096 * 4


typedef struct prgm_gzip prgm_gzip_t;

typedef lxb_status_t
(*prgm_gzip_cb_f)(prgm_gzip_t *gzip, const lxb_char_t *data, size_t size);

struct prgm_gzip {
    z_stream       stream;

    int            ret;
    unsigned       in_size;
    unsigned       out_size;

    size_t         count;

    prgm_gzip_cb_f cb;
    void           *ctx;

    lxb_char_t     *out_buf;
    unsigned       out_buf_size;
};


/* Inflate */
lxb_status_t
prgm_gzip_inflate_init(prgm_gzip_t *gzip, lxb_char_t *out_buf,
                          unsigned out_size, prgm_gzip_cb_f cb, void *ctx);

prgm_gzip_t *
prgm_gzip_inflate_destroy(prgm_gzip_t *gzip, bool self_destroy);

lxb_status_t
prgm_gzip_inflate(prgm_gzip_t *gzip, lxb_char_t *data, unsigned size);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PRGM_GZIP_H */
