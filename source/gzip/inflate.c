/*
* Copyright (C) 2019 Alexander Borisov
*
* Author: Alexander Borisov <borisov@lexbor.com>
*/

#include "gzip.h"


lxb_status_t
prgm_gzip_inflate_init(prgm_gzip_t *gzip, lxb_char_t *out_buf,
                       unsigned out_size, prgm_gzip_cb_f cb, void *ctx)
{
    if (gzip == NULL) {
        return LXB_STATUS_ERROR_OBJECT_IS_NULL;
    }

    if (out_buf == NULL || out_size == 0 || cb == NULL) {
        return LXB_STATUS_ERROR_WRONG_ARGS;
    }

    memset(gzip, 0, sizeof(prgm_gzip_t));

    gzip->stream.zalloc = Z_NULL;
    gzip->stream.zfree = Z_NULL;
    gzip->stream.opaque = Z_NULL;

    /* Fake buffer before call inflateInit2. */
    gzip->stream.avail_in = out_size;
    gzip->stream.next_in = out_buf;

    gzip->cb = cb;
    gzip->ctx = ctx;

    gzip->out_buf = out_buf;
    gzip->out_buf_size = out_size;

    gzip->ret = inflateInit2(&gzip->stream, (32 + MAX_WBITS));
    if (gzip->ret != Z_OK) {
        return LXB_STATUS_ERROR;
    }

    return LXB_STATUS_OK;
}

prgm_gzip_t *
prgm_gzip_inflate_destroy(prgm_gzip_t *gzip, bool self_destroy)
{
    if (gzip == NULL) {
        return NULL;
    }

    (void) inflateEnd(&gzip->stream);

    if (self_destroy) {
        return lexbor_free(gzip);
    }

    return gzip;
}

lxb_status_t
prgm_gzip_inflate(prgm_gzip_t *gzip, lxb_char_t *data, unsigned size)
{
    lxb_status_t status;
    unsigned have;

next_chunk:

    do {
        gzip->stream.next_in = data;
        gzip->stream.avail_in = size;

        do {
            gzip->stream.avail_out = gzip->out_buf_size;
            gzip->stream.next_out = gzip->out_buf;

            gzip->ret = inflate(&gzip->stream, Z_NO_FLUSH);

            switch (gzip->ret) {
                case Z_NEED_DICT:
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    goto failed;

                case Z_BUF_ERROR:
                    return LXB_STATUS_OK;

                default:
                    break;
            }

            have = gzip->out_buf_size - gzip->stream.avail_out;

            status = gzip->cb(gzip, gzip->out_buf, have);
            if (status != LXB_STATUS_OK) {
                if (status == LXB_STATUS_STOP) {
                    return LXB_STATUS_STOP;
                }

                goto failed;
            }

            if (gzip->ret == Z_STREAM_END) {
                gzip->count++;

                data += size - gzip->stream.avail_in;
                size = gzip->stream.avail_in;

                (void) inflateEnd(&gzip->stream);

                status = prgm_gzip_inflate_init(gzip, gzip->out_buf,
                                                gzip->out_buf_size,
                                                gzip->cb, gzip->ctx);
                if (status != LXB_STATUS_OK) {
                    return status;
                }

                if (gzip->stream.avail_in == 0) {
                    return LXB_STATUS_OK;
                }

                goto next_chunk;
            }
        }
        while (gzip->stream.avail_out == 0);

        data += size - gzip->stream.avail_in;
        size = gzip->stream.avail_in;
    }
    while (size != 0);

    return LXB_STATUS_OK;

failed:

    (void) inflateEnd(&gzip->stream);

    return LXB_STATUS_ERROR;
}
