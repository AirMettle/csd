/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2023 AirMettle, Inc.
 *   All rights reserved.
 */

#include "kvcli.h"
#include "parse_args.h"
#include "spdk/nvme_kv.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// defined in parse_args.c
extern char *command;
extern void *cmd_args;
extern struct option *cmd_long_options;
extern int num_long_options;
extern struct option long_options_cmd_store[];
extern struct option long_options_cmd_list[];
extern struct option long_options_cmd_exists[];
extern struct option long_options_cmd_delete[];
extern struct option long_options_cmd_retrieve[];
extern struct option long_options_cmd_select[];

static int
write_buffer_to_file(char *buf, uint64_t nbytes, char *filename) {
    // SPDK_NOTICELOG("Writing buffer.\n");

    FILE *fp = NULL;

    fp = fopen(filename, "ab");
    if (fp == NULL) {
        SPDK_ERRLOG("Could not open file %s\n", filename);
        return -1;
    }

    uint64_t bytes_written = fwrite(buf, 1, nbytes, fp);
    if (bytes_written != nbytes) {
        SPDK_ERRLOG("Could not write to file %s\n", filename);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static void
create_empty_file(char *filename, int nbytes) {
    FILE *fp = NULL;
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        SPDK_ERRLOG("Could not open file %s\n", filename);
        return;
    }
    fclose(fp);
}

static int
read_key_from_buffer(void *buffer,
                     size_t buffer_size,
                     uint32_t *num_keys,
                     char *last_key,
                     bool skip_first) {
    size_t bytes_read;
    uint16_t len, pad_len;

    *num_keys = from_le32(buffer);
    // SPDK_NOTICELOG("Keys in the buffer: %d\n", *num_keys);

    bytes_read = 4;
    void *ptr;

    for (uint32_t i = 0; i < *num_keys; ++i) {
        ptr = buffer + bytes_read;
        len = from_le16(ptr);

        pad_len = len;
        if (pad_len % 4) {
            pad_len += 4 - (pad_len % 4);
        }

        if (!skip_first || i) {
            printf("key[%d] = %.*s\n",
                   skip_first ? i - 1 : i,
                   len,
                   (char *)(ptr + 2));
        }

        if (i == *num_keys - 1) {
            strncpy(last_key, (char *)(ptr + 2), len);
            last_key[len] = '\0';
        }

        if (bytes_read + pad_len + 2 > buffer_size) {
            SPDK_ERRLOG("Buffer overflow when reading keys\n");
            return -1;
        }

        bytes_read += pad_len + 2;
    }
    return 0;
}

static void
kvcli_reset_zone(void *arg) {
    struct kvcli_ctx_t *ctx = arg;
    int rc = 0;

    rc = spdk_bdev_zone_management(ctx->bdev_desc,
                                   ctx->bdev_io_channel,
                                   0,
                                   SPDK_BDEV_ZONE_RESET,
                                   kvcli_reset_zone_cb,
                                   ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        /* In case we cannot perform I/O now, queue I/O */
        ctx->bdev_io_wait.bdev = ctx->bdev;
        ctx->bdev_io_wait.cb_fn = kvcli_reset_zone;
        ctx->bdev_io_wait.cb_arg = ctx;
        spdk_bdev_queue_io_wait(ctx->bdev,
                                ctx->bdev_io_channel,
                                &ctx->bdev_io_wait);
    } else if (rc) {
        SPDK_ERRLOG("%s error while resetting zone: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(ctx->bdev_io_channel);
        spdk_bdev_close(ctx->bdev_desc);
        spdk_app_stop(-1);
    }
}

static void
kvcli_store_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv) {

    // cast callback argument to kvcli_store_cb_ctx_t
    struct kvcli_store_cb_ctx_t *cb_arg =
        (struct kvcli_store_cb_ctx_t *)cb_argv;

    // SPDK_NOTICELOG("Entered KV store callback.\n");

    spdk_bdev_free_io(bdev_io);

    if (success) {
        // SPDK_NOTICELOG("KV store completed successfully\n");
    } else {
        SPDK_ERRLOG("KV store error: %d\n", EIO);
        spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
        spdk_bdev_close(cb_arg->ctx->bdev_desc);
        spdk_app_stop(success ? 0 : -1);
        free(cb_arg);
        return;
    }

    // only continue if the previous command did not reach the end of file
    if (cb_arg->stop) {
        spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
        spdk_bdev_close(cb_arg->ctx->bdev_desc);
        spdk_app_stop(success ? 0 : -1);
        free(cb_arg);
    } else {
        // make context for another store command
        struct kvcli_store_ctx_t store_ctx = {};

        // populate store command context from args
        store_ctx.ctx = cb_arg->ctx;
        store_ctx.key = cb_arg->key;
        store_ctx.input_file = cb_arg->input_file;
        store_ctx.append = cb_arg->append;
        store_ctx.read_offset = cb_arg->read_offset + cb_arg->ctx->buff_size;

        free(cb_arg);

        // call store for the next chunk of the file
        kvcli_store(&store_ctx);
    }
}

static void
kvcli_send_select_cb(struct spdk_bdev_io *bdev_io,
                     bool success,
                     void *cb_argv) {

    // SPDK_NOTICELOG("Entered KV send select callback.\n");

    // cast callback argument to kvcli_send_select_cb_ctx_t
    struct kvcli_send_select_cb_ctx_t *cb_arg =
        (struct kvcli_send_select_cb_ctx_t *)cb_argv;

    // get return status and result id
    u_int32_t rc;
    int sct, sc;
    spdk_bdev_io_get_nvme_status(bdev_io, &rc, &sct, &sc);
    // SPDK_NOTICELOG("rc=%x, sct=%x, sc=%x\n", rc, sct, sc);

    spdk_bdev_free_io(bdev_io);

    if (success) {
        // SPDK_NOTICELOG("KV send select completed successfully\n");
    } else {
        SPDK_ERRLOG("KV send select error: %d\n", EIO);
        spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
        spdk_bdev_close(cb_arg->ctx->bdev_desc);
        // SPDK_NOTICELOG("Stopping app\n");
        spdk_app_stop(success ? 0 : -1);
        free(cb_arg);
        return;
    }

    // make context for receive select call
    struct kvcli_retrieve_select_ctx_t *ctx_retrieve_select =
        (struct kvcli_retrieve_select_ctx_t *)
            calloc(1, sizeof(struct kvcli_retrieve_select_ctx_t));

    // keep kvcli context
    ctx_retrieve_select->ctx = cb_arg->ctx;

    // populate all fields of context
    ctx_retrieve_select->offset = 0;
    ctx_retrieve_select->result_output_file = cb_arg->result_output_file;
    ctx_retrieve_select->result_id = rc;
    free(cb_arg);

    // call retrieve select to get the results of send command
    kvcli_retrieve_select(ctx_retrieve_select);
}

static void
kvcli_retrieve_select_cb(struct spdk_bdev_io *bdev_io,
                         bool success,
                         void *cb_argv) {
    // SPDK_NOTICELOG("Entered kv retrieve select callback.\n");

    // cast callback argument to kvcli_retrieve_select_cb_ctx_t
    struct kvcli_retrieve_select_cb_ctx_t *cb_arg =
        (struct kvcli_retrieve_select_cb_ctx_t *)cb_argv;

    if (success) {
        // SPDK_NOTICELOG("KV retrieve select completed successfully\n");

        // get total size of stored value using spdk_bdev_io_get_nvme_status
        uint32_t total_size;
        int sct, sc;
        spdk_bdev_io_get_nvme_status(bdev_io, &total_size, &sct, &sc);
        spdk_bdev_free_io(bdev_io);

        // create empty file of the specified size if the offset is 0,
        // which means this is the first callback for this command
        if (cb_arg->offset == 0) {
            // SPDK_NOTICELOG("Offset is 0.\n");

            // create a new file only on the first call
            create_empty_file(cb_arg->result_output_file, total_size);
        }

        // write the buffer to the file
        write_buffer_to_file(
            cb_arg->ctx->buff,
            MIN(cb_arg->ctx->buff_size, total_size - cb_arg->offset),
            cb_arg->result_output_file);

        // make another call if the offset is smaller than the total
        // size. kvcli_retrieve will be called recursively until the
        // whole file is retrieved
        if (cb_arg->offset + cb_arg->ctx->buff_size < total_size) {
            // printf("Making another call to retrieve\n");

            // make context for retrieve command
            struct kvcli_retrieve_select_ctx_t retrieve_ctx = {};

            // populate retrieve command context from args
            retrieve_ctx.ctx = cb_arg->ctx;
            retrieve_ctx.result_id = cb_arg->result_id;
            retrieve_ctx.offset = cb_arg->offset + cb_arg->ctx->buff_size;
            retrieve_ctx.result_output_file = cb_arg->result_output_file;
            free(cb_arg);

            // call retrieve to get the rest of the file
            kvcli_retrieve_select(&retrieve_ctx);
        } else {
            // only exit if the current offset will be the last one for this
            // result
            spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
            spdk_bdev_close(cb_arg->ctx->bdev_desc);
            // SPDK_NOTICELOG("Stopping app\n");
            spdk_app_stop(success ? 0 : -1);
            free(cb_arg);
            return;
        }
    } else {
        spdk_bdev_free_io(bdev_io);
        spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
        spdk_bdev_close(cb_arg->ctx->bdev_desc);
        // SPDK_NOTICELOG("Stopping app\n");
        spdk_app_stop(success ? 0 : -1);
        free(cb_arg);
        return;
    }
}

static void
kvcli_list_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv) {
    // SPDK_NOTICELOG("Entered KV list callback.\n");

    // cast callback argument to kvcli_list_cb_ctx_t
    struct kvcli_list_cb_ctx_t *cb_arg = (struct kvcli_list_cb_ctx_t *)cb_argv;

    uint32_t total_num_keys = 0, curr_num_keys = 0;
    int sct, sc;
    spdk_bdev_io_get_nvme_status(bdev_io, &total_num_keys, &sct, &sc);
    // SPDK_NOTICELOG("rc=%x, sct=%x, sc=%x\n", total_num_keys, sct, sc);
    spdk_bdev_free_io(bdev_io);

    if (success) {
        // SPDK_NOTICELOG("KV list completed successfully\n");

        // print out matching keys
        char last_key[17];
        read_key_from_buffer(cb_arg->ctx->buff,
                             cb_arg->ctx->buff_size,
                             &curr_num_keys,
                             last_key,
                             cb_arg->skip_first);

        if (curr_num_keys < total_num_keys) {
            // SPDK_NOTICELOG("Making another call to list\n");

            // make context for list command
            struct kvcli_list_ctx_t list_ctx = {};
            list_ctx.ctx = cb_arg->ctx;
            list_ctx.key = last_key;
            list_ctx.skip_first = true;
            free(cb_arg);

            // call list to get the rest of the keys
            kvcli_list(&list_ctx);
        } else {
            spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
            spdk_bdev_close(cb_arg->ctx->bdev_desc);
            // SPDK_NOTICELOG("Stopping app\n");
            spdk_app_stop(success ? 0 : -1);
            free(cb_arg);
        }
    } else {
        SPDK_ERRLOG("KV list error: %d\n", EIO);
        spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
        spdk_bdev_close(cb_arg->ctx->bdev_desc);
        // SPDK_NOTICELOG("Stopping app\n");
        spdk_app_stop(success ? 0 : -1);
        free(cb_arg);
    }
}

static void
kvcli_exists_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv) {
    // SPDK_NOTICELOG("Entered KV exists callback.\n");

    // cast callback argument to kvcli_exists_cb_ctx_t
    struct kvcli_exists_cb_ctx_t *cb_arg =
        (struct kvcli_exists_cb_ctx_t *)cb_argv;

    u_int32_t rc;
    int sct, sc;
    spdk_bdev_io_get_nvme_status(bdev_io, &rc, &sct, &sc);
    // SPDK_NOTICELOG("rc=%d, sct=%d, sc=%x\n", rc, sct, sc);

    if (sc == 0x87 && success == false) {
        printf("Key does not exist.\n");
    } else if (sc == 0x0) {
        printf("Key exists.\n");
    } else {
        printf("Unknown error.\n");
    }

    // complete the bdev io and close the channel
    spdk_bdev_free_io(bdev_io);
    spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
    spdk_bdev_close(cb_arg->ctx->bdev_desc);
    // SPDK_NOTICELOG("Stopping app\n");
    spdk_app_stop(success ? 0 : -1);
    free(cb_arg);
}

static void
kvcli_delete_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv) {
    // SPDK_NOTICELOG("Entered KV delete callback\n");

    // cast callback argument to kvcli_delete_cb_ctx_t
    struct kvcli_delete_cb_ctx_t *cb_arg =
        (struct kvcli_delete_cb_ctx_t *)cb_argv;

    u_int32_t rc;
    int sct, sc;
    spdk_bdev_io_get_nvme_status(bdev_io, &rc, &sct, &sc);
    // SPDK_NOTICELOG("rc=%d, sct=%d, sc=%x\n", rc, sct, sc);

    if (success && sc == 0x00) {
        // printf("KV key deleted\n");
    } else {
        SPDK_ERRLOG("KV delete error: %x\n", sc);
    }

    /* Complete the bdev io and close the channel */
    spdk_bdev_free_io(bdev_io);
    spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
    spdk_bdev_close(cb_arg->ctx->bdev_desc);
    // SPDK_NOTICELOG("Stopping app\n");
    spdk_app_stop(success ? 0 : -1);
    free(cb_arg);
}

static void
kvcli_event_cb(enum spdk_bdev_event_type type,
               struct spdk_bdev *bdev,
               void *event_ctx) {
    SPDK_NOTICELOG("Unsupported bdev event: type %d\n", type);
}

static void
kvcli_reset_zone_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg) {
    struct kvcli_ctx_t *ctx = cb_arg;

    /* Complete the I/O */
    spdk_bdev_free_io(bdev_io);

    if (!success) {
        SPDK_ERRLOG("bdev io reset zone error: %d\n", EIO);
        spdk_put_io_channel(ctx->bdev_io_channel);
        spdk_bdev_close(ctx->bdev_desc);
        spdk_app_stop(-1);
        return;
    }
}

static void
kvcli_retrieve_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv) {
    // SPDK_NOTICELOG("Entered KV retrieve callback.\n");

    // cast callback argument to kvcli_retrieve_cb_ctx_t
    struct kvcli_retrieve_cb_ctx_t *cb_arg =
        (struct kvcli_retrieve_cb_ctx_t *)cb_argv;

    spdk_bdev_free_io(bdev_io);

    if (success) {
        // SPDK_NOTICELOG("KV retrieve completed successfully\n");

        // get total size of stored value using spdk_bdev_io_get_nvme_status
        uint32_t total_size;
        int sct, sc;
        spdk_bdev_io_get_nvme_status(bdev_io, &total_size, &sct, &sc);

        // create empty file of the specified size if the offset is 0,
        // which means this is the first callback for this command
        if (cb_arg->offset == 0) {
            create_empty_file(cb_arg->output_file, total_size);
        }

        bool make_another_call =
            cb_arg->offset + cb_arg->ctx->buff_size < total_size;

        int bytes_to_write = cb_arg->ctx->buff_size;

        // if this is the last call, the buffer may not be completely
        // full, write only the bytes that are needed
        if (!make_another_call) {
            // SPDK_NOTICELOG("This is the last call.\n");
            bytes_to_write = total_size % cb_arg->ctx->buff_size;
        } else {
            // SPDK_NOTICELOG("This is not the last call.\n");
        }

        // SPDK_NOTICELOG("bytes_to_write=%d\n", bytes_to_write);

        // write the buffer to the file
        write_buffer_to_file(cb_arg->ctx->buff,
                             bytes_to_write,
                             cb_arg->output_file);

        // make another call if the offset is smaller than the total
        // size. kvcli_retrieve will be called recursively
        if (make_another_call) {

            // make context for retrieve command
            struct kvcli_retrieve_ctx_t retrieve_ctx = {};

            // populate retrieve command context from args
            retrieve_ctx.ctx = cb_arg->ctx;
            retrieve_ctx.key = cb_arg->key;
            retrieve_ctx.offset = cb_arg->offset + cb_arg->ctx->buff_size;
            retrieve_ctx.output_file = cb_arg->output_file;

            free(cb_arg);

            // call retrieve to get the rest of the file
            kvcli_retrieve(&retrieve_ctx);
        } else {
            // stopping app because this is the last callback
            spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
            spdk_bdev_close(cb_arg->ctx->bdev_desc);
            spdk_app_stop(success ? 0 : -1);
            free(cb_arg);
        }
    } else {
        SPDK_ERRLOG("KV retrieve error: %d\n", EIO);
        spdk_put_io_channel(cb_arg->ctx->bdev_io_channel);
        spdk_bdev_close(cb_arg->ctx->bdev_desc);
        spdk_app_stop(success ? 0 : -1);
        free(cb_arg);
    }
}

static void
kvcli_store(void *argv) {
    // cast argument to kvcli_store_ctx_t
    struct kvcli_store_ctx_t *arg = (struct kvcli_store_ctx_t *)argv;

    // SPDK_NOTICELOG("Entered KV store.\n");
    // SPDK_NOTICELOG("arg->key=%s\n", arg->key);
    // SPDK_NOTICELOG("arg->input_file=%s\n", arg->input_file);
    // SPDK_NOTICELOG("arg->append=%d\n", arg->append);
    // SPDK_NOTICELOG("arg->read_offset=%lu\n", arg->read_offset);
    // SPDK_NOTICELOG("arg->ctx->buff_size=%lu\n", arg->ctx->buff_size);

    int rc = 0;

    // clear the buffer
    memset(arg->ctx->buff, 0, arg->ctx->buff_size);

    // open input file
    FILE *fp = NULL;
    fp = fopen(arg->input_file, "rb");
    if (fp == NULL) {
        SPDK_ERRLOG("Could not open file %s\n", arg->input_file);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
        return;
    }

    // start reading from offset in file
    rc = fseek(fp, arg->read_offset, SEEK_SET);
    if (rc) {
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
        return;
    }

    // read from file into buffer
    size_t bytes_read = fread(arg->ctx->buff, 1, arg->ctx->buff_size, fp);
    if (bytes_read == 0) {
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
        return;
    }

    fclose(fp);

    // make callback struct
    struct kvcli_store_cb_ctx_t *cb_ctx = (struct kvcli_store_cb_ctx_t *)calloc(
        1,
        sizeof(struct kvcli_store_cb_ctx_t));

    // keep kvcli context
    cb_ctx->ctx = arg->ctx;

    // set options in callback context
    cb_ctx->input_file = arg->input_file;
    cb_ctx->read_offset = arg->read_offset;
    cb_ctx->key = arg->key;
    cb_ctx->append = arg->append;
    cb_ctx->stop = false;

    // if the bytes read is less than the buffer size, we have reached the
    // end. this is the last store call for this file
    if (bytes_read < arg->ctx->buff_size) {
        cb_ctx->stop = true;
    }

    u_int8_t options = 0;

    if (arg->read_offset != 0) {
        options |= NVME_KV_STORE_CMD_OPTION_APPEND;
    }

    rc = spdk_bdev_kv_store(arg->ctx->bdev_desc,
                            arg->ctx->bdev_io_channel,
                            arg->key,         // key name
                            strlen(arg->key), // key length
                            arg->ctx->buff,   // buffer
                            bytes_read,       // bytes to read from buffer
                            options,
                            kvcli_store_cb,
                            cb_ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        // In case we cannot perform I/O now, queue I/O
        arg->ctx->bdev_io_wait.bdev = arg->ctx->bdev;
        arg->ctx->bdev_io_wait.cb_fn = kvcli_store;
        arg->ctx->bdev_io_wait.cb_arg = arg->ctx;
        spdk_bdev_queue_io_wait(arg->ctx->bdev,
                                arg->ctx->bdev_io_channel,
                                &arg->ctx->bdev_io_wait);
    } else if (rc) {
        SPDK_ERRLOG("%s error while writing to bdev: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
    }
}

static void
kvcli_list(void *argv) {
    // SPDK_NOTICELOG("Entered KV list.\n");

    // cast argument to kvcli_list_ctx_t
    struct kvcli_list_ctx_t *arg = (struct kvcli_list_ctx_t *)argv;

    int rc = 0;

    // make callback struct
    struct kvcli_list_cb_ctx_t *cb_ctx = (struct kvcli_list_cb_ctx_t *)calloc(
        1,
        sizeof(struct kvcli_list_cb_ctx_t));

    // keep kvcli context
    cb_ctx->ctx = arg->ctx;
    cb_ctx->skip_first = arg->skip_first;

    rc = spdk_bdev_kv_list(arg->ctx->bdev_desc,
                           arg->ctx->bdev_io_channel,
                           arg->key,
                           strlen(arg->key),
                           arg->ctx->buff,
                           arg->ctx->buff_size,
                           kvcli_list_cb,
                           cb_ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        // In case we cannot perform I/O now, queue I/O
        arg->ctx->bdev_io_wait.bdev = arg->ctx->bdev;
        arg->ctx->bdev_io_wait.cb_fn = kvcli_list;
        arg->ctx->bdev_io_wait.cb_arg = arg;
        spdk_bdev_queue_io_wait(arg->ctx->bdev,
                                arg->ctx->bdev_io_channel,
                                &arg->ctx->bdev_io_wait);
    } else if (rc) {
        SPDK_ERRLOG("%s error while listing from bdev: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
    }
}

static void
kvcli_exists(void *argv) {
    // SPDK_NOTICELOG("Entered KV exists.\n");

    // cast argument to kvcli_exists_ctx_t
    struct kvcli_exists_ctx_t *arg = (struct kvcli_exists_ctx_t *)argv;

    int rc = 0;

    // make callback struct
    struct kvcli_exists_cb_ctx_t *cb_ctx = (struct kvcli_exists_cb_ctx_t *)
        calloc(1, sizeof(struct kvcli_exists_cb_ctx_t));

    // keep kvcli context
    cb_ctx->ctx = arg->ctx;

    rc = spdk_bdev_kv_exist(arg->ctx->bdev_desc,
                            arg->ctx->bdev_io_channel,
                            arg->key,
                            strlen(arg->key),
                            kvcli_exists_cb,
                            cb_ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        // In case we cannot perform I/O now, queue I/O
        arg->ctx->bdev_io_wait.bdev = arg->ctx->bdev;
        arg->ctx->bdev_io_wait.cb_fn = kvcli_exists;
        arg->ctx->bdev_io_wait.cb_arg = arg->ctx;
        spdk_bdev_queue_io_wait(arg->ctx->bdev,
                                arg->ctx->bdev_io_channel,
                                &arg->ctx->bdev_io_wait);
    } else if (rc) {
        SPDK_ERRLOG("%s error while checking if key exists: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
    }
}

static void
kvcli_delete(void *argv) {
    // SPDK_NOTICELOG("Entered KV delete\n");

    // cast argument to kvcli_delete_ctx_t
    struct kvcli_delete_ctx_t *arg = (struct kvcli_delete_ctx_t *)argv;

    int rc = 0;

    // print arg
    // printf("Deleting key: %s\n", arg->key);

    // make callback struct
    struct kvcli_delete_cb_ctx_t *cb_ctx = (struct kvcli_delete_cb_ctx_t *)
        calloc(1, sizeof(struct kvcli_delete_cb_ctx_t));

    // keep kvcli context
    cb_ctx->ctx = arg->ctx;

    rc = spdk_bdev_kv_delete(arg->ctx->bdev_desc,
                             arg->ctx->bdev_io_channel,
                             arg->key,
                             strlen(arg->key),
                             kvcli_delete_cb,
                             cb_ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        // In case we cannot perform I/O now, queue I/O
        arg->ctx->bdev_io_wait.bdev = arg->ctx->bdev;
        arg->ctx->bdev_io_wait.cb_fn = kvcli_delete;
        arg->ctx->bdev_io_wait.cb_arg = cb_ctx;
        spdk_bdev_queue_io_wait(arg->ctx->bdev,
                                arg->ctx->bdev_io_channel,
                                &arg->ctx->bdev_io_wait);
    } else if (rc) {
        SPDK_ERRLOG("%s error while deleting from bdev: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
    }
}

static void
kvcli_retrieve(void *argv) {
    // SPDK_NOTICELOG("Entered KV retrieve.\n");

    // cast argument to kvcli_retrieve_ctx_t
    struct kvcli_retrieve_ctx_t *arg = (struct kvcli_retrieve_ctx_t *)argv;

    int rc = 0;

    // clear the buffer
    memset(arg->ctx->buff, 0, arg->ctx->buff_size);

    // make callback struct
    struct kvcli_retrieve_cb_ctx_t *cb_ctx = (struct kvcli_retrieve_cb_ctx_t *)
        calloc(1, sizeof(struct kvcli_retrieve_cb_ctx_t));

    // keep kvcli context
    cb_ctx->ctx = arg->ctx;

    // populate all fields of callback context
    cb_ctx->output_file = arg->output_file;
    cb_ctx->key = arg->key;
    cb_ctx->offset = arg->offset;

    // SPDK_NOTICELOG("Offset: %lu\n", cb_ctx->offset);

    rc = spdk_bdev_kv_retrieve(arg->ctx->bdev_desc,
                               arg->ctx->bdev_io_channel,
                               cb_ctx->key,
                               strlen(cb_ctx->key),
                               arg->ctx->buff,
                               cb_ctx->offset,
                               arg->ctx->buff_size,
                               kvcli_retrieve_cb,
                               cb_ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        // In case we cannot perform I/O now, queue I/O
        arg->ctx->bdev_io_wait.bdev = arg->ctx->bdev;
        arg->ctx->bdev_io_wait.cb_fn = kvcli_retrieve;
        arg->ctx->bdev_io_wait.cb_arg = arg->ctx;
        spdk_bdev_queue_io_wait(arg->ctx->bdev,
                                arg->ctx->bdev_io_channel,
                                &arg->ctx->bdev_io_wait);
    } else if (rc) {
        SPDK_ERRLOG("%s error while reading from bdev: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
    }
}

static void
kvcli_send_select(void *argv) {
    // SPDK_NOTICELOG("Entered KV send select.\n");

    // cast argument to kvcli_send_select_ctx_t
    struct kvcli_send_select_ctx_t *arg =
        (struct kvcli_send_select_ctx_t *)argv;

    int rc = 0;

    // save size of sql command
    uint64_t select_sql_size = strlen(arg->sql_cmd);

    // set options arg
    uint8_t options = 0;
    if (arg->use_csv_header_for_input) {
        options |= 0x01;
    }
    if (arg->use_csv_header_for_output) {
        options |= 0x02;
    }

    struct kvcli_send_select_cb_ctx_t *cb_ctx =
        (struct kvcli_send_select_cb_ctx_t *)
            calloc(1, sizeof(struct kvcli_send_select_cb_ctx_t));

    // keep kvcli context
    cb_ctx->ctx = arg->ctx;
    cb_ctx->result_output_file = arg->result_output_file;

    rc = spdk_bdev_kv_send_select(arg->ctx->bdev_desc,
                                  arg->ctx->bdev_io_channel,
                                  arg->key,
                                  strlen(arg->key),
                                  arg->sql_cmd,
                                  select_sql_size,
                                  options,
                                  arg->input_format,
                                  arg->output_format,
                                  kvcli_send_select_cb,
                                  cb_ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        // In case we cannot perform I/O now, queue I/O
        arg->ctx->bdev_io_wait.bdev = arg->ctx->bdev;
        arg->ctx->bdev_io_wait.cb_fn = kvcli_send_select;
        arg->ctx->bdev_io_wait.cb_arg = cb_ctx;
        spdk_bdev_queue_io_wait(arg->ctx->bdev,
                                arg->ctx->bdev_io_channel,
                                &arg->ctx->bdev_io_wait);
        return;
    } else if (rc) {
        SPDK_ERRLOG("%s error while sending select to bdev: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
        return;
    }
}

static void
kvcli_retrieve_select(void *argv) {
    // SPDK_NOTICELOG("Entered kv retrieve select.\n");

    // cast argument to kvcli_retrieve_select_ctx_t
    struct kvcli_retrieve_select_ctx_t *arg =
        (struct kvcli_retrieve_select_ctx_t *)argv;

    int rc = 0;

    // make context for callback of retrieve select call
    struct kvcli_retrieve_select_cb_ctx_t *cb_ctx =
        (struct kvcli_retrieve_select_cb_ctx_t *)
            calloc(1, sizeof(struct kvcli_retrieve_select_cb_ctx_t));

    // populate callback context
    cb_ctx->ctx = arg->ctx;
    cb_ctx->result_output_file = arg->result_output_file;
    cb_ctx->offset = arg->offset;
    cb_ctx->result_id = arg->result_id;

    // make call to get results of previous select call
    rc = spdk_bdev_kv_retrieve_select(arg->ctx->bdev_desc,
                                      arg->ctx->bdev_io_channel,
                                      arg->ctx->buff,
                                      arg->offset,
                                      arg->ctx->buff_size,
                                      arg->result_id,
                                      SPDK_NVME_KV_SELECT_FREE_IF_FIT,
                                      kvcli_retrieve_select_cb,
                                      cb_ctx);

    if (rc == -ENOMEM) {
        // SPDK_NOTICELOG("Queueing io\n");
        // In case we cannot perform I/O now, queue I/O
        arg->ctx->bdev_io_wait.bdev = arg->ctx->bdev;
        arg->ctx->bdev_io_wait.cb_fn = kvcli_retrieve_select;
        arg->ctx->bdev_io_wait.cb_arg = cb_ctx;
        spdk_bdev_queue_io_wait(arg->ctx->bdev,
                                arg->ctx->bdev_io_channel,
                                &arg->ctx->bdev_io_wait);
        return;
    } else if (rc) {
        SPDK_ERRLOG("%s error while retrieving select from bdev: %d\n",
                    spdk_strerror(-rc),
                    rc);
        spdk_put_io_channel(arg->ctx->bdev_io_channel);
        spdk_bdev_close(arg->ctx->bdev_desc);
        spdk_app_stop(-1);
    }
}

static void
kvcli_start(void *argv) {

    // cast argument to kvcli_ctx_t
    struct kvcli_ctx_t *arg = (struct kvcli_ctx_t *)argv;

    int rc = 0;
    arg->bdev = NULL;
    arg->bdev_desc = NULL;

    // SPDK_NOTICELOG("Successfully started the application\n");

    // Open the bdev by calling spdk_bdev_open_ext() with its name.
    // The function will return a descriptor
    // SPDK_NOTICELOG("Opening the bdev %s\n", arg->bdev_name);
    rc = spdk_bdev_open_ext(arg->bdev_name,
                            true,
                            kvcli_event_cb,
                            NULL,
                            &arg->bdev_desc);
    if (rc) {
        SPDK_ERRLOG("Could not open bdev: %s\n", arg->bdev_name);
        spdk_app_stop(-1);
        return;
    }

    // a bdev pointer is valid while the bdev is opened
    arg->bdev = spdk_bdev_desc_get_bdev(arg->bdev_desc);

    // SPDK_NOTICELOG("Opening IO channel\n");
    arg->bdev_io_channel = spdk_bdev_get_io_channel(arg->bdev_desc);
    if (arg->bdev_io_channel == NULL) {
        SPDK_ERRLOG("Could not create bdev IO channel\n");
        spdk_bdev_close(arg->bdev_desc);
        spdk_app_stop(-1);
        return;
    }

    arg->buff_size = 16 * 1024 * 1024;

    uint32_t buf_align = spdk_bdev_get_buf_align(arg->bdev);
    // SPDK_NOTICELOG("Buffer alignment: %d\n", buf_align);

    // allocate memory for the write buffer
    arg->buff = spdk_dma_zmalloc(arg->buff_size, buf_align, NULL);
    // SPDK_NOTICELOG("Buffer allocated (%d).\n",arg->buff_size);

    if (!arg->buff) {
        SPDK_ERRLOG("Failed to allocate buffer\n");
        spdk_put_io_channel(arg->bdev_io_channel);
        spdk_bdev_close(arg->bdev_desc);
        spdk_app_stop(-1);
        return;
    }

    if (spdk_bdev_is_zoned(arg->bdev)) {
        kvcli_reset_zone(arg);
        SPDK_WARNLOG("bdev is zoned\n");
        // If zoned, the callback, reset_zone_complete, will call entry
        // function
        return;
    }

    if (strcmp(command, "store") == 0) {
        // make context for store command
        struct kvcli_store_ctx_t store_ctx = {};

        // populate store command context from args
        store_ctx.ctx = arg;
        store_ctx.key = ((struct cmd_store_args *)cmd_args)->key;
        store_ctx.input_file = ((struct cmd_store_args *)cmd_args)->input_file;
        store_ctx.append = ((struct cmd_store_args *)cmd_args)->append;
        store_ctx.read_offset = 0;

        kvcli_store(&store_ctx);
    } else if (strcmp(command, "list") == 0) {
        // make context for list command
        struct kvcli_list_ctx_t list_ctx = {};

        // populate list command context from args
        list_ctx.ctx = arg;
        list_ctx.key = ((struct cmd_list_args *)cmd_args)->key;
        if (list_ctx.key == NULL) {
            list_ctx.key = "";
        }

        kvcli_list(&list_ctx);
    } else if (strcmp(command, "exists") == 0) {
        // make context for exists command
        struct kvcli_exists_ctx_t exists_ctx = {};

        // populate exists command context from args
        exists_ctx.ctx = arg;
        exists_ctx.key = ((struct cmd_exists_args *)cmd_args)->key;

        kvcli_exists(&exists_ctx);
    } else if (strcmp(command, "delete") == 0) {
        // make context for delete command
        struct kvcli_delete_ctx_t delete_ctx = {};

        // populate delete command context from args
        delete_ctx.ctx = arg;
        delete_ctx.key = ((struct cmd_delete_args *)cmd_args)->key;

        kvcli_delete(&delete_ctx);
    } else if (strcmp(command, "retrieve") == 0) {
        // make context for retrieve command
        struct kvcli_retrieve_ctx_t retrieve_ctx = {};

        // populate retrieve command context from args
        retrieve_ctx.ctx = arg;
        retrieve_ctx.key = ((struct cmd_retrieve_args *)cmd_args)->key;
        retrieve_ctx.offset = 0;
        retrieve_ctx.output_file =
            ((struct cmd_retrieve_args *)cmd_args)->output_file;

        kvcli_retrieve(&retrieve_ctx);
    } else if (strcmp(command, "select") == 0) {
        struct kvcli_send_select_ctx_t sel_ctx = {};

        struct cmd_select_args *sel_args = (struct cmd_select_args *)cmd_args;

        // keep kvcli context
        sel_ctx.ctx = arg;

        // populate select args from cmd_args
        sel_ctx.sql_cmd = sel_args->sql;
        sel_ctx.input_format = sel_args->input_format;
        sel_ctx.output_format = sel_args->output_format;
        sel_ctx.use_csv_header_for_input = sel_args->use_csv_header_for_input;
        sel_ctx.use_csv_header_for_output = sel_args->use_csv_header_for_output;
        sel_ctx.result_output_file = sel_args->file;
        sel_ctx.key = sel_args->key;

        kvcli_send_select(&sel_ctx);
    } else {
        SPDK_ERRLOG("Command not recognized\n");
        spdk_put_io_channel(arg->bdev_io_channel);
        spdk_bdev_close(arg->bdev_desc);
        spdk_app_stop(-1);
        return;
    }
}

int
main(int argc, char **argv) {
    if (argc < 3) {
        kvcli_usage();
        return 1;
    }
    struct spdk_app_opts opts = {};
    int rc = 0;
    struct kvcli_ctx_t ctx = {};

    spdk_log_enable_timestamps(false);

    // set default values in opts structure.
    spdk_app_opts_init(&opts, sizeof(opts));
    opts.name = "kvcli";
    opts.print_level = SPDK_LOG_ERROR;

    // set default config file location
    opts.json_config_file = getenv("SPDK_KV_CONFIG_PATH");

    // copy cli args into new array but skip the bdname and command
    int new_argc = argc - 2;
    char *new_argv[new_argc];
    new_argv[0] = argv[0];
    for (int i = 3; i < argc; i++) {
        new_argv[i - 2] = argv[i];
    }

    command = argv[2];

    cmd_long_options = NULL;

    // save long opt ptr to args depending on which command was given.
    if (strcmp(command, "store") == 0) {
        cmd_args = calloc(1, sizeof(struct cmd_store_args));
        memset(cmd_args, 0, sizeof(struct cmd_store_args));
        cmd_long_options = long_options_cmd_store;
        num_long_options = 4;
    } else if (strcmp(command, "list") == 0) {
        cmd_args = calloc(1, sizeof(struct cmd_list_args));
        memset(cmd_args, 0, sizeof(struct cmd_list_args));
        cmd_long_options = long_options_cmd_list;
        num_long_options = 2;
    } else if (strcmp(command, "exists") == 0) {
        cmd_args = calloc(1, sizeof(struct cmd_exists_args));
        memset(cmd_args, 0, sizeof(struct cmd_exists_args));
        cmd_long_options = long_options_cmd_exists;
        num_long_options = 2;
    } else if (strcmp(command, "delete") == 0) {
        cmd_args = calloc(1, sizeof(struct cmd_delete_args));
        memset(cmd_args, 0, sizeof(struct cmd_delete_args));
        cmd_long_options = long_options_cmd_delete;
        num_long_options = 2;
    } else if (strcmp(command, "retrieve") == 0) {
        cmd_args = calloc(1, sizeof(struct cmd_retrieve_args));
        memset(cmd_args, 0, sizeof(struct cmd_retrieve_args));
        cmd_long_options = long_options_cmd_retrieve;
        num_long_options = 4;
    } else if (strcmp(command, "select") == 0) {
        cmd_args = calloc(1, sizeof(struct cmd_select_args));
        memset(cmd_args, 0, sizeof(struct cmd_select_args));
        cmd_long_options = long_options_cmd_select;
        num_long_options = 8;
    } else {
        SPDK_ERRLOG("Command not recognized\n");
        kvcli_usage();
        return 1;
    }

    // parse built-in SPDK command line parameters
    rc = spdk_app_parse_args(new_argc,
                             new_argv,
                             &opts,
                             "",
                             cmd_long_options,
                             kvcli_parse_args,
                             kvcli_usage);
    if (rc != SPDK_APP_PARSE_ARGS_SUCCESS) {
        exit(rc);
    }

    rc = validate_args();
    if (rc) {
        kvcli_usage();
        exit(rc);
    }

    ctx.bdev_name = argv[1];

    // spdk_app_start() will initialize the SPDK framework, call
    // kvcli_start(), and then block until spdk_app_stop() is called (or if
    // an initialization error occurs, spdk_app_start() will return with rc
    // even without calling kvcli_start().
    rc = spdk_app_start(&opts, kvcli_start, &ctx);
    if (rc) {
        SPDK_ERRLOG("App exited\n");
    }

    // At this point either spdk_app_stop() was called, or spdk_app_start()
    // failed because of internal error.

    // When the app stops, free up memory that we allocated.
    spdk_dma_free(ctx.buff);
    free(cmd_args);

    // Gracefully close out all of the SPDK subsystems.
    spdk_app_fini();
    return rc;
}
