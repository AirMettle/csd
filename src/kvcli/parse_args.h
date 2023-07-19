/*
 *   Utilities for parsing command line arguments of each subcommand of kvcli.
 *
 *   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2023 AirMettle, Inc.
 *   All rights reserved.
 */

#ifndef KVCLI_PARSE_ARGS_H
#define KVCLI_PARSE_ARGS_H
#include "spdk/bdev.h"

struct cmd_store_args {
    char *input_file;
    char *key;
    bool append;
};

struct cmd_list_args {
    char *key;
};

struct cmd_exists_args {
    char *key;
};

struct cmd_delete_args {
    char *key;
};

struct cmd_retrieve_args {
    char *key;
    char *output_file;
    uint64_t offset;
};

struct cmd_select_args {
    char *key;
    char *sql;
    int input_format;
    int output_format;
    bool use_csv_header_for_input;
    bool use_csv_header_for_output;
    char *file;
};

// short way to reference options of the store command
enum cmd_store_args_enum {
    CMD_STORE_ARGS_INPUT_FILE,
    CMD_STORE_ARGS_KEY,
    CMD_STORE_ARGS_APPEND
};

// args of the list command
enum cmd_list_args_enum { CMD_LIST_ARGS_KEY };

// args of the exists command
enum cmd_exists_args_enum { CMD_EXISTS_ARGS_KEY };

// args of the delete command
enum cmd_delete_args_enum { CMD_DELETE_ARGS_KEY };

// args of the retrieve command
enum cmd_retrieve_args_enum {
    CMD_RETRIEVE_ARGS_KEY,
    CMD_RETRIEVE_ARGS_OUTPUT_FILE,
    CMD_RETRIEVE_ARGS_OFFSET
};

// args of the select command
enum cmd_select_args_enum {
    CMD_SELECT_ARGS_KEY,
    CMD_SELECT_ARGS_SQL,
    CMD_SELECT_ARGS_INPUT_FORMAT,
    CMD_SELECT_ARGS_OUTPUT_FORMAT,
    CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_INPUT,
    CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_OUTPUT,
    CMD_SELECT_ARGS_FILE
};

// print usage
void kvcli_usage(void);

// parse the parameters that are specific to this application
int kvcli_parse_args(int ch, char *arg);

// validates whether the required arguments for the command are provided
int validate_args(void);

#endif // KVCLI_PARSE_ARGS_H