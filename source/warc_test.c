/*
 * Copyright (C) 2019 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

#include <lexbor/core/fs.h>
#include <lexbor/html/encoding.h>
#include <lexbor/html/parser.h>
#include <lexbor/encoding/encoding.h>
#include <lexbor/utils/http.h>
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

#define TO_LOG(tctx, ...)                                                      \
    do {                                                                       \
        fprintf((tctx)->log, __VA_ARGS__);                                     \
        fprintf((tctx)->log, "\n");                                            \
        fflush((tctx)->log);                                                   \
    }                                                                          \
    while (0)


typedef struct {
    lxb_utils_warc_t                *warc;
    lxb_utils_http_t                *http;
    lxb_html_parser_t               *parser;

    const lxb_char_t                *fullpath;

    lxb_html_document_t             *document;

    FILE                            *log;

    lxb_utils_warc_header_cb_f      h_cd;
    lxb_utils_warc_content_cb_f     c_cb;
    lxb_utils_warc_content_end_cb_f c_end_cb;

    const lxb_encoding_data_t       *enc_data;
    const lxb_encoding_data_t       *enc_utf_8;

    lxb_encoding_encode_t           encode;
    lxb_encoding_decode_t           decode;

    lxb_html_encoding_t             html_em;

    lxb_codepoint_t                 buf_decode[4096];
    lxb_char_t                      buf_encode[4096];

    size_t                          total;

    lxb_status_t                    status;
}
lxb_test_ctx_t;


static lexbor_action_t
dir_files_cb(const lxb_char_t *fullpath, size_t fullpath_len,
             const lxb_char_t *filename, size_t filename_len, void *ctx);

static lxb_status_t
gzip_cb(prgm_gzip_t *gzip, const lxb_char_t *data, size_t size);

static lxb_status_t
warc_single_header_cb(lxb_utils_warc_t *warc);

static lxb_status_t
warc_single_content_end_cb(lxb_utils_warc_t *warc);

static lxb_status_t
warc_multi_header_cb(lxb_utils_warc_t *warc);

static lxb_status_t
warc_multi_content_end_cb(lxb_utils_warc_t *warc);

static lxb_status_t
warc_content_header_cb(lxb_utils_warc_t *warc, const lxb_char_t *data,
                       const lxb_char_t *end);

static lxb_status_t
warc_content_body_cb(lxb_utils_warc_t *warc, const lxb_char_t *data,
                     const lxb_char_t *end);


static void
usage(void)
{
    printf("Usage: warc <mode> <log file> <directory>\n");
    printf("<mode>:\n");
    printf("    single -- one parser on all HTML\n");
    printf("    multi  -- own parser for each HTML\n");
    printf("<log file>: path to log file\n");
    printf("<directory>: path to directory with *.warc.gz files\n");
}

int
main(int argc, const char *argv[])
{
    size_t size;
    lxb_status_t status;
    const lxb_char_t *dirpath;
    lxb_test_ctx_t ctx = {0};

    static const char single[] = "single";
    static const char multi[] = "multi";

    if (argc < 4) {
        usage();
        return EXIT_SUCCESS;
    }

    status = lxb_html_encoding_init(&ctx.html_em);
    if (status != LXB_STATUS_OK) {
        FAILED(false, "Failed to create HTML encoding determiner");
    }

    ctx.enc_utf_8 = lxb_encoding_data(LXB_ENCODING_UTF_8);

    size = strlen(argv[1]);

    if (size == (sizeof(single) - 1)
        && memcmp(argv[1], single, (sizeof(single) - 1)) == 0)
    {
        ctx.document = lxb_html_document_create();
        if (ctx.document == NULL) {
            FAILED(false, "Failed to create HTML Document");
        }

        ctx.h_cd = warc_single_header_cb;
        ctx.c_end_cb = warc_single_content_end_cb;
    }
    else if (size == (sizeof(multi) - 1)
             && memcmp(argv[1], multi, (sizeof(multi) - 1)) == 0)
    {
        ctx.h_cd = warc_multi_header_cb;
        ctx.c_end_cb = warc_multi_content_end_cb;
    }
    else {
        usage();
        return EXIT_SUCCESS;
    }

    ctx.c_cb = warc_content_header_cb;

    ctx.log = fopen((const char *) argv[2], "ab");
    if (ctx.log == NULL) {
        goto failed;
    }

    dirpath = (const lxb_char_t *) argv[3];

    status = lexbor_fs_dir_read(dirpath, LEXBOR_FS_DIR_OPT_WITHOUT_HIDDEN
                            |LEXBOR_FS_DIR_OPT_WITHOUT_DIR, dir_files_cb, &ctx);
    if (status != LXB_STATUS_OK || ctx.status != LXB_STATUS_OK) {
        goto failed;
    }

    TO_LOG(&ctx, "Total processed: "LEXBOR_FORMAT_Z, ctx.total);

    return EXIT_SUCCESS;

failed:

    if (ctx.log != NULL) {
        TO_LOG(&ctx, "Total processed: "LEXBOR_FORMAT_Z, ctx.total);
        TO_LOG(&ctx, "Failed");

        fclose(ctx.log);
    }

    (void) lxb_html_document_destroy(ctx.document);
    (void) lxb_html_encoding_destroy(&ctx.html_em, false);

    return EXIT_FAILURE;
}

static lexbor_action_t
dir_files_cb(const lxb_char_t *fullpath, size_t fullpath_len,
             const lxb_char_t *filename, size_t filename_len, void *ctx)
{
    lxb_test_ctx_t *tctx = ctx;

    prgm_gzip_t gzip;
    lxb_char_t in_buf[LXB_UTILS_GZIP_CHUNK];
    lxb_char_t out_buf[LXB_UTILS_GZIP_CHUNK];

    FILE *fh = NULL;
    size_t size;

    if (filename_len < 8
        || lexbor_str_data_ncasecmp((const lxb_char_t *) "warc.gz",
                                    &filename[filename_len - 7], 7) == false)
    {
        return LEXBOR_ACTION_NEXT;
    }

    tctx->fullpath = fullpath;

    TO_LOG(tctx, "Start processing file: %s", (const char *) fullpath);

    /* Create WARC parser */
    tctx->warc = lxb_utils_warc_create();
    tctx->status = lxb_utils_warc_init(tctx->warc, tctx->h_cd, tctx->c_cb,
                                       tctx->c_end_cb, tctx);
    if (tctx->status != LXB_STATUS_OK) {
        TO_LOG(tctx, "Failed to init warc.");

        return LEXBOR_ACTION_STOP;
    }

    /* Create HTTP parser */
    tctx->http = lxb_utils_http_create();
    tctx->status = lxb_utils_http_init(tctx->http, tctx->warc->mraw);
    if (tctx->status != LXB_STATUS_OK) {
        TO_LOG(tctx, "Failed to init http.");

        return LEXBOR_ACTION_STOP;
    }

    /* Create GZIP decompressor */
    tctx->status = prgm_gzip_inflate_init(&gzip, out_buf, LXB_UTILS_GZIP_CHUNK,
                                          gzip_cb, tctx);
    if (tctx->status != LXB_STATUS_OK) {
        TO_LOG(tctx, "Failed to init gzip.");

        goto failed;
    }

    /* Open and read GZIP file */
    fh = fopen((const char *) fullpath, "rb");
    if (fh == NULL) {
        goto failed;
    }

    do {
        size = fread(in_buf, 1, LXB_UTILS_GZIP_CHUNK, fh);

        if (size != LXB_UTILS_GZIP_CHUNK) {
            if (feof(fh)) {
                tctx->status = prgm_gzip_inflate(&gzip, in_buf,
                                                 (unsigned) size);
                if (tctx->status != LXB_STATUS_OK) {
                    TO_LOG(tctx, "Failed to process inflate.");

                    goto failed;
                }

                break;
            }

            goto failed;
        }

        tctx->status = prgm_gzip_inflate(&gzip, in_buf, (unsigned) size);
        if (tctx->status != LXB_STATUS_OK) {
            TO_LOG(tctx, "Failed to process inflate.");

            goto failed;
        }
    }
    while (true);

    prgm_gzip_inflate_destroy(&gzip, false);
    lxb_utils_warc_destroy(tctx->warc, true);

    tctx->http->mraw = NULL;
    lxb_utils_http_destroy(tctx->http, true);

    fclose(fh);

    return LEXBOR_ACTION_OK;

failed:

    prgm_gzip_inflate_destroy(&gzip, false);
    lxb_utils_warc_destroy(tctx->warc, true);

    tctx->http->mraw = NULL;
    lxb_utils_http_destroy(tctx->http, true);

    if (fh != NULL) {
        fclose(fh);
    }

    return LEXBOR_ACTION_STOP;
}

static lxb_status_t
gzip_cb(prgm_gzip_t *gzip, const lxb_char_t *data, size_t size)
{
    lxb_status_t status;
    lxb_test_ctx_t *tctx = gzip->ctx;

    status = lxb_utils_warc_parse(tctx->warc, &data, (data + size));
    if (status != LXB_STATUS_OK && tctx->warc->error != NULL) {
        TO_LOG(tctx, "WARC error: %s", tctx->warc->error);
    }

    return status;
}

lxb_inline lxb_status_t
http_check_html_type(lxb_test_ctx_t *tctx)
{
    lxb_utils_warc_field_t *field;

    static const lxb_char_t lxb_wtype[] = "WARC-Type";
    static const lxb_char_t lxb_wtype_val[] = "response";
    static const lxb_char_t lxb_wident[] = "WARC-Identified-Payload-Type";
    static const lxb_char_t lxb_wident_val_html[] = "text/html";
    static const lxb_char_t lxb_wident_val_xml[] = "application/xhtml+xml";

    field = lxb_utils_warc_header_field(tctx->warc, lxb_wtype,
                                        (sizeof(lxb_wtype) - 1), 0);
    if (field == NULL
        || field->value.length != (sizeof(lxb_wtype_val) - 1)
        || lexbor_str_data_ncasecmp(field->value.data, lxb_wtype_val,
                                    field->value.length) == false)
    {
        goto next;
    }

    field = lxb_utils_warc_header_field(tctx->warc, lxb_wident,
                                        (sizeof(lxb_wident) - 1), 0);
    if (field == NULL) {
        goto next;
    }

    TO_LOG(tctx, LEXBOR_FORMAT_Z": %s", tctx->warc->count, field->value.data);

    if (field->value.length == (sizeof(lxb_wident_val_html) - 1)
        && lexbor_str_data_ncasecmp(field->value.data, lxb_wident_val_html,
                                field->value.length))
    {
        return LXB_STATUS_OK;
    }

    if (field->value.length == (sizeof(lxb_wident_val_xml) - 1)
        && lexbor_str_data_ncasecmp(field->value.data, lxb_wident_val_xml,
                                    field->value.length))
    {
        return LXB_STATUS_OK;
    }

    return LXB_STATUS_NEXT;

next:

    TO_LOG(tctx, LEXBOR_FORMAT_Z, tctx->warc->count);

    return LXB_STATUS_NEXT;
}

lxb_inline lxb_status_t
html_encode(lxb_test_ctx_t *tctx)
{
    lxb_status_t status, enc_status;
    const lxb_codepoint_t *buf, *buf_end;

    buf = tctx->buf_decode;
    buf_end = tctx->buf_decode + lxb_encoding_decode_buf_used(&tctx->decode);

    do {
        lxb_encoding_encode_buf_used_set(&tctx->encode, 0);

        enc_status = tctx->enc_utf_8->encode(&tctx->encode, &buf, buf_end);

        status = lxb_html_document_parse_chunk(tctx->document,
                                      tctx->buf_encode, tctx->encode.buffer_used);
        if (status != LXB_STATUS_OK) {
            TO_LOG(tctx, "HTML chunk parsing error");
            return LXB_STATUS_ERROR;
        }
    }
    while (enc_status == LXB_STATUS_SMALL_BUFFER);

    return LXB_STATUS_OK;
}

static lxb_status_t
warc_single_header_cb(lxb_utils_warc_t *warc)
{
    lxb_status_t status;
    lxb_test_ctx_t *tctx = warc->ctx;

    if (http_check_html_type(tctx) == LXB_STATUS_NEXT) {
        return LXB_STATUS_NEXT;
    }

    status = lxb_html_document_parse_chunk_begin(tctx->document);
    if (status != LXB_STATUS_OK) {
        TO_LOG(tctx, "HTML chunk begin error");
        return LXB_STATUS_ERROR;
    }

    return LXB_STATUS_OK;
}

static lxb_status_t
warc_single_content_end_cb(lxb_utils_warc_t *warc)
{
    lxb_status_t status;
    lxb_test_ctx_t *tctx = warc->ctx;

    if (tctx->enc_data == NULL) {
        lxb_encoding_decode_buf_used_set(&tctx->decode, 0);

        (void) lxb_encoding_decode_finish(&tctx->decode);

        if (lxb_encoding_decode_buf_used(&tctx->decode) != 0) {
            status = html_encode(tctx);
            if (status != LXB_STATUS_OK) {
                return status;
            }
        }

        /* No need to call lxb_encoding_encode_finish(). */
    }

    lxb_utils_http_clear(tctx->http);

    status = lxb_html_document_parse_chunk_end(tctx->document);
    if (status != LXB_STATUS_OK) {
        TO_LOG(tctx, "HTML chunk end error");
        return LXB_STATUS_ERROR;
    }

    warc->content_cb = warc_content_header_cb;

    return LXB_STATUS_OK;
}

static lxb_status_t
warc_multi_header_cb(lxb_utils_warc_t *warc)
{
    lxb_status_t status;
    lxb_test_ctx_t *tctx = warc->ctx;

    tctx->document = lxb_html_document_create();
    if (tctx->document == NULL) {
        TO_LOG(tctx, "HTML document create error");
        return LXB_STATUS_ERROR;
    }

    status = lxb_html_document_parse_chunk_begin(tctx->document);
    if (status != LXB_STATUS_OK) {
        TO_LOG(tctx, "HTML chunk begin error");
        return LXB_STATUS_ERROR;
    }

    return LXB_STATUS_OK;
}

static lxb_status_t
warc_multi_content_end_cb(lxb_utils_warc_t *warc)
{
    lxb_status_t status;
    lxb_test_ctx_t *tctx = warc->ctx;

    if (tctx->enc_data == NULL) {
        lxb_encoding_decode_buf_used_set(&tctx->decode, 0);

        (void) lxb_encoding_decode_finish(&tctx->decode);

        if (lxb_encoding_decode_buf_used(&tctx->decode) != 0) {
            status = html_encode(tctx);
            if (status != LXB_STATUS_OK) {
                return status;
            }
        }

        /* No need to call lxb_encoding_encode_finish(). */
    }

    lxb_utils_http_clear(tctx->http);

    status = lxb_html_document_parse_chunk_end(tctx->document);
    if (status != LXB_STATUS_OK) {
        TO_LOG(tctx, "HTML chunk end error");
        return LXB_STATUS_ERROR;
    }

    tctx->document = lxb_html_document_destroy(tctx->document);

    warc->content_cb = warc_content_header_cb;

    return LXB_STATUS_OK;
}

static lxb_status_t
warc_content_header_cb(lxb_utils_warc_t *warc,
                       const lxb_char_t *data, const lxb_char_t *end)
{
    size_t len;
    lxb_status_t status;
    lxb_utils_http_field_t *field;
    lxb_test_ctx_t *tctx = warc->ctx;
    lxb_html_encoding_entry_t *enc_entry;
    const lxb_encoding_data_t *html_enc_data;
    const lxb_char_t *enc_name, *enc_end;

    static const lxb_char_t lxb_ctype[] = "Content-Type";

    status = lxb_utils_http_parse(tctx->http, &data, end);
    if (status != LXB_STATUS_OK) {
        if (status == LXB_STATUS_NEXT) {
            return LXB_STATUS_OK;
        }

        goto failed;
    }

    status = lxb_utils_http_header_parse_eof(tctx->http);
    if (status != LXB_STATUS_OK) {
        goto failed;
    }

    tctx->total++;

    html_enc_data = NULL;
    tctx->enc_data = NULL;

    /* Get encoding from HTTP Content-Type */
    field = lxb_utils_http_header_field(tctx->http, lxb_ctype,
                                        (sizeof(lxb_ctype) - 1), 0);
    if (field == NULL) {
        goto html_encoding;
    }

    enc_name = lxb_html_encoding_content(field->value.data, field->value.data
                                         + field->value.length, &enc_end);
    if (enc_name == NULL) {
        TO_LOG(tctx, "HTTP encoding not found in \"%s\"", field->value.data);
        goto html_encoding;
    }

    tctx->enc_data = lxb_encoding_data_by_pre_name(enc_name,
                                                   (enc_end - enc_name));
    if (tctx->enc_data == NULL) {
        TO_LOG(tctx, "HTTP encoding found but not determine by \"%.*s\"",
               (int) (enc_end - enc_name), enc_name);
    }

html_encoding:

    status = lxb_html_encoding_determine(&tctx->html_em, data, end);

    if (status != LXB_STATUS_OK) {
        TO_LOG(tctx, "Failed to determine encoding from HTML stream");
    }
    else {
        len = lxb_html_encoding_meta_length(&tctx->html_em);

        if (len == 0) {
            if (tctx->enc_data != NULL) {
                TO_LOG(tctx, "HTML encoding not determined but found in header:"
                       " \"%.*s\"", (int) (enc_end - enc_name), enc_name);
            }

            TO_LOG(tctx, "HTML fragment to determine encoding by meta tag:\n"
                   "%.*s", (int) (end - data), data);
        }
        else {
            enc_entry = lxb_html_encoding_meta_entry(&tctx->html_em, 0);

            html_enc_data = lxb_encoding_data_by_pre_name(enc_entry->name,
                                            (enc_entry->end - enc_entry->name));
            if (html_enc_data == NULL) {
                TO_LOG(tctx, "HTML meta encoding found but not determine by"
                       " \"%.*s\"", (int) (enc_entry->end - enc_entry->name),
                       enc_entry->name);
            }

            if (tctx->enc_data == NULL) {
                tctx->enc_data = html_enc_data;
            }
        }
    }

    lxb_html_encoding_clean(&tctx->html_em);

    if (tctx->enc_data != NULL) {
        lxb_encoding_encode_init(&tctx->encode, tctx->enc_data,
                                 tctx->buf_encode, sizeof(tctx->buf_encode));

        tctx->encode.replace_to = (const lxb_char_t *) "?";
        tctx->encode.replace_len = 1;

        lxb_encoding_decode_init(&tctx->decode, tctx->enc_utf_8, tctx->buf_decode,
                                 sizeof(tctx->buf_decode) / sizeof(lxb_codepoint_t));

        tctx->decode.replace_to = LXB_ENCODING_REPLACEMENT_BUFFER;
        tctx->encode.replace_len = LXB_ENCODING_REPLACEMENT_BUFFER_LEN;
    }

    status = warc_content_body_cb(warc, data, end);
    if (status != LXB_STATUS_OK) {
        return status;
    }

    warc->content_cb = warc_content_body_cb;

    return LXB_STATUS_OK;

failed:

    if (tctx->http->error != NULL) {
        TO_LOG(tctx, "HTML header parsing error: %s", tctx->http->error);
    }
    else {
        TO_LOG(tctx, "HTML header parsing error");
    }

    return LXB_STATUS_ERROR;
}

static lxb_status_t
warc_content_body_cb(lxb_utils_warc_t *warc, const lxb_char_t *data,
                     const lxb_char_t *end)
{
    lxb_status_t status, dec_status;
    lxb_test_ctx_t *tctx = warc->ctx;

    if (tctx->enc_data == NULL) {
        status = lxb_html_document_parse_chunk(tctx->document, data,
                                               (end - data));
        if (status != LXB_STATUS_OK) {
            TO_LOG(tctx, "HTML chunk parsing error");
            return LXB_STATUS_ERROR;
        }

        return LXB_STATUS_OK;
    }

    do {
        lxb_encoding_decode_buf_used_set(&tctx->decode, 0);

        dec_status = tctx->enc_data->decode(&tctx->decode, &data, end);

        status = html_encode(tctx);
        if (status != LXB_STATUS_OK) {
            return status;
        }
    }
    while (dec_status == LXB_STATUS_SMALL_BUFFER);

    return LXB_STATUS_OK;
}
