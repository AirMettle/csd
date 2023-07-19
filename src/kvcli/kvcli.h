/*
 *   kvcli - command line tool to run nvme kv commands
 *
 *   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2023 AirMettle, Inc.
 *   All rights reserved.
 */

#include "spdk/bdev.h"
#include "spdk/bdev_zone.h"
#include "spdk/endian.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/thread.h"

#ifndef KVCLI_H
#define KVCLI_H

// context passed to every kvcli function
struct kvcli_ctx_t {
    char *bdev_name;
    char *buff;
    struct spdk_bdev *bdev;
    struct spdk_bdev_desc *bdev_desc;
    struct spdk_bdev_io_wait_entry bdev_io_wait;
    struct spdk_io_channel *bdev_io_channel;
    uint32_t buff_size;
};

// context passed to the send select function
struct kvcli_send_select_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *sql_cmd;
    int input_format;
    int output_format;
    bool use_csv_header_for_input;
    bool use_csv_header_for_output;
    char *result_output_file;
    char *key;
};

struct kvcli_retrieve_select_ctx_t {
    struct kvcli_ctx_t *ctx;
    u_int32_t result_id;
    uint64_t offset;
    char *result_output_file;
};

struct kvcli_store_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *input_file;
    char *key;
    bool append;
    size_t read_offset;
};

struct kvcli_list_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *key;
    bool skip_first;
};

struct kvcli_exists_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *key;
};

struct kvcli_delete_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *key;
};

// retrieve value of key (not select retrieve)
struct kvcli_retrieve_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *key;
    char *output_file;
    uint64_t offset;
};

// call back context for the retrieve select function
struct kvcli_retrieve_select_cb_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *result_output_file;
    uint64_t offset;
    u_int32_t result_id;
};

struct kvcli_send_select_cb_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *result_output_file;
};

struct kvcli_delete_cb_ctx_t {
    struct kvcli_ctx_t *ctx;
};

struct kvcli_store_cb_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *input_file;
    char *key;
    bool append;
    size_t read_offset;
    bool stop;
};

struct kvcli_list_cb_ctx_t {
    struct kvcli_ctx_t *ctx;
    bool skip_first;
};

struct kvcli_exists_cb_ctx_t {
    struct kvcli_ctx_t *ctx;
};

struct kvcli_retrieve_cb_ctx_t {
    struct kvcli_ctx_t *ctx;
    char *output_file;
    uint64_t offset;
    char *key;
};

static void kvcli_delete(void *argv);
static void kvcli_exists(void *argv);
static void kvcli_list(void *argv);
static void kvcli_retrieve_select(void *argv);
static void kvcli_retrieve(void *argv);
static void kvcli_send_select(void *argv);
static void kvcli_store(void *argv);

static void
kvcli_delete_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv);
static void
kvcli_exists_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv);
static void
kvcli_list_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv);
static void
kvcli_retrieve_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv);
static void kvcli_retrieve_select_cb(struct spdk_bdev_io *bdev_io,
                                     bool success,
                                     void *cb_argv);
static void
kvcli_send_select_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv);
static void
kvcli_store_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_argv);
static void kvcli_event_cb(enum spdk_bdev_event_type type,
                           struct spdk_bdev *bdev,
                           void *event_ctx);
static void
kvcli_reset_zone_cb(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg);

static void kvcli_reset_zone(void *arg);
static void kvcli_start(void *arg);

#endif // KVCLI_H