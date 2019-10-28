/*
 * Copyright (C) 2019 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

#include <lexbor/core/fs.h>
#include "lexbor/core/conv.h"
#include <lexbor/utils/warc.h>

#include "gzip.h"


#define FAILED(with_usage, ...)                                                \
    do {                                                                       \
        fprintf(stderr, __VA_ARGS__);                                          \
        fprintf(stderr, "\n");                                                 \
                                                                               \
        if (with_usage) {                                                      \
            usage();                                                           \
        }                                                                      \
                                                                               \
        exit(EXIT_FAILURE);                                                    \
    }                                                                          \
    while (0)


typedef struct {
    lxb_utils_warc_t *warc;
    const lxb_char_t *fullpath;

    unsigned long    index;
}
lxb_test_ctx_t;


static lxb_status_t
gzip_cb(prgm_gzip_t *gzip, const lxb_char_t *data, size_t size);

static lxb_status_t
warc_header_cb(lxb_utils_warc_t *warc);

static lxb_status_t
warc_content_end_cb(lxb_utils_warc_t *warc);

static lxb_status_t
warc_content_body_cb(lxb_utils_warc_t *warc, const lxb_char_t *data,
                     const lxb_char_t *end);


static void
usage(void)
{
    printf("Usage: warc_entry_by_index <index> <file.warc.gz>\n");
    printf("<index>: begin form 0\n");
    printf("<file.warc.gz>: path to *.warc.gz file\n");
}

int
main(int argc, const char *argv[])
{
    FILE *fh;
    size_t size;
    lxb_status_t status;
    const lxb_char_t *data, *filename;
    lxb_test_ctx_t ctx = {0};

    prgm_gzip_t gzip;
    lxb_char_t in_buf[LXB_UTILS_GZIP_CHUNK];
    lxb_char_t out_buf[LXB_UTILS_GZIP_CHUNK];

    if (argc < 3) {
        usage();
        return EXIT_SUCCESS;
    }

    data = (const lxb_char_t *) argv[1];
    size = strlen(argv[1]);

    ctx.index = lexbor_conv_data_to_ulong(&data, size);

    if ((const char *) data == argv[1]) {
        FAILED(true, "Bad index.");
    }

    filename = (const lxb_char_t *) argv[2];
    size = strlen(argv[2]);

    if (size < 8 || lexbor_str_data_ncasecmp((const lxb_char_t *) "warc.gz",
                                             &filename[size - 7], 7) == false)
    {
        FAILED(true, "Bad file extension.");
    }

    /* Create WARC parser */
    ctx.warc = lxb_utils_warc_create();
    status = lxb_utils_warc_init(ctx.warc, warc_header_cb, NULL, NULL, &ctx);
    if (status != LXB_STATUS_OK) {
        goto failed;
    }

    /* Create GZIP decompressor */
    status = prgm_gzip_inflate_init(&gzip, out_buf, LXB_UTILS_GZIP_CHUNK,
                                    gzip_cb, &ctx);
    if (status != LXB_STATUS_OK) {
        goto failed;
    }

    /* Open and read GZIP file */
    fh = fopen((const char *) filename, "rb");
    if (fh == NULL) {
        goto failed;
    }

    do {
        size = fread(in_buf, 1, LXB_UTILS_GZIP_CHUNK, fh);

        if (size != LXB_UTILS_GZIP_CHUNK) {
            if (feof(fh)) {
                status = prgm_gzip_inflate(&gzip, in_buf, (unsigned) size);
                if (status != LXB_STATUS_OK) {
                    if (status == LXB_STATUS_STOP) {
                        goto done;
                    }

                    goto failed;
                }

                break;
            }

            goto failed;
        }

        status = prgm_gzip_inflate(&gzip, in_buf, (unsigned) size);
        if (status != LXB_STATUS_OK) {
            if (status == LXB_STATUS_STOP) {
                goto done;
            }

            goto failed;
        }
    }
    while (true);

done:

    prgm_gzip_inflate_destroy(&gzip, false);
    lxb_utils_warc_destroy(ctx.warc, true);

    fclose(fh);

    return EXIT_SUCCESS;

failed:

    prgm_gzip_inflate_destroy(&gzip, false);
    lxb_utils_warc_destroy(ctx.warc, true);

    if (fh != NULL) {
        fclose(fh);
    }

    FAILED(false, "Failed to process inflate.");
}

static lxb_status_t
gzip_cb(prgm_gzip_t *gzip, const lxb_char_t *data, size_t size)
{
    lxb_status_t status;
    lxb_test_ctx_t *tctx = gzip->ctx;

    status = lxb_utils_warc_parse(tctx->warc, &data, (data + size));
    if (status != LXB_STATUS_OK && tctx->warc->error != NULL) {
        FAILED(false, "WARC error: %s", tctx->warc->error);
    }

    return status;
}

static lxb_status_t
warc_header_cb(lxb_utils_warc_t *warc)
{
    lxb_test_ctx_t *tctx = warc->ctx;

    if (tctx->index == tctx->warc->count) {
        warc->content_cb = warc_content_body_cb;
        warc->content_end_cb = warc_content_end_cb;
    }

    return LXB_STATUS_OK;
}

static lxb_status_t
warc_content_body_cb(lxb_utils_warc_t *warc, const lxb_char_t *data,
                     const lxb_char_t *end)
{
    size_t size;
    lxb_test_ctx_t *tctx = warc->ctx;

    size = fwrite(data, 1, (end - data), stdout);
    if (size != (end - data)) {
        FAILED(false, "Failed to write data to stdout.");
    }

    return LXB_STATUS_OK;
}

static lxb_status_t
warc_content_end_cb(lxb_utils_warc_t *warc)
{
    return LXB_STATUS_STOP;
}
