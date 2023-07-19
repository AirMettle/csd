/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2023 AirMettle, Inc.
 *   All rights reserved.
 */

#include "parse_args.h"
#include "spdk/log.h"
#include <unistd.h>

// struct to hold the long options of the store command
struct option long_options_cmd_store[] = {
    {"file", required_argument, NULL, CMD_STORE_ARGS_INPUT_FILE},
    {"key", required_argument, NULL, CMD_STORE_ARGS_KEY},
    {"append", no_argument, NULL, CMD_STORE_ARGS_APPEND},
    {0, 0, 0, 0},
};

// struct to hold the long options of the list command
struct option long_options_cmd_list[] = {
    {"key", required_argument, NULL, CMD_LIST_ARGS_KEY},
    {0, 0, 0, 0},
};

// struct to hold the long options of the exists command
struct option long_options_cmd_exists[] = {
    {"key", required_argument, NULL, CMD_EXISTS_ARGS_KEY},
    {0, 0, 0, 0},
};

// struct to hold the long options of the delete command
struct option long_options_cmd_delete[] = {
    {"key", required_argument, NULL, CMD_DELETE_ARGS_KEY},
    {0, 0, 0, 0},
};

// struct to hold the long options of the retrieve command
struct option long_options_cmd_retrieve[] = {
    {"key", required_argument, NULL, CMD_RETRIEVE_ARGS_KEY},
    {"file", required_argument, NULL, CMD_RETRIEVE_ARGS_OUTPUT_FILE},
    {"offset", required_argument, NULL, CMD_RETRIEVE_ARGS_OFFSET},
    {0, 0, 0, 0},
};

// struct to hold the long options of the select command
struct option long_options_cmd_select[] = {
    {"key", required_argument, NULL, CMD_SELECT_ARGS_KEY},
    {"sql", required_argument, NULL, CMD_SELECT_ARGS_SQL},
    {"input_format", required_argument, NULL, CMD_SELECT_ARGS_INPUT_FORMAT},
    {"output_format", required_argument, NULL, CMD_SELECT_ARGS_OUTPUT_FORMAT},
    {"use_csv_header_for_input",
     no_argument,
     NULL,
     CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_INPUT},
    {"use_csv_header_for_output",
     no_argument,
     NULL,
     CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_OUTPUT},
    {"file", required_argument, NULL, CMD_SELECT_ARGS_FILE},
    {0, 0, 0, 0},
};

// used to validate whether the required args were provided
static uint8_t provided_args = 0;

// pointer to the command string
char *command = NULL;

// args struct of the respective command
void *cmd_args = NULL;

// save pointer to struct option args of respective command
struct option *cmd_long_options = NULL;

// long options for the respective command
// used by get_opt
int num_long_options = 0;

// print usage
void
kvcli_usage(void) {
    printf("Usage: kvcli BDEVNAME COMMAND [OPTION]... \n");
    printf("kvcli -h or kvcli --help: show this help message and exit\n");
    printf("BDEVNAME: Name of the block device to use. e.g. Nvme1n1\n");
    printf(
        "COMMAND: can be store, retrieve, list, exists, delete, or select.\n");
    printf(
        "OPTION: Command-specific options. These options are accepted in any order.\n");
    printf("Command reference:\n");
    printf("store: Store the contents of FILE under KEY.\n");
    printf(
        "    usage: kvcli BDEVNAME store --file FILE --key KEY [--append]\n");
    printf("retrieve: Retrieve the contents of KEY and write to FILE.\n");
    printf("    usage: kvcli BDEVNAME retrieve --key KEY --file FILE\n");
    printf("delete: Delete KEY from the KV store.\n");
    printf("    usage: kvcli BDEVNAME delete --key KEY\n");
    printf("list: List keys matching the prefix.\n");
    printf("    usage: kvcli BDEVNAME list --key KEY\n");
    printf(
        "select: Run SQL query on the contents of KEY and write the results to FILE.\n");
    printf("    usage: kvcli BDEVNAME select --key KEY\n"
           "                      --sql SQL\n"
           "                      --file FILE\n"
           "                      [--input_format csv|json|parquet]\n"
           "                      [--output_format csv|json|parquet]\n"
           "                      [--use_csv_header_for_input]\n"
           "                      [--use_csv_header_for_output]\n");
    printf("exists: Check if KEY exists.\n");
    printf("    usage: kvcli BDEVNAME exists --key KEY\n");
}

// parse the parameters that are specific to this application
int
kvcli_parse_args(int ch, char *arg) {
    // printf("Entered with ch=%d, arg=%s\n", ch, arg);

    // parse the args based on the command and the enum of that command args
    if (strcmp(command, "store") == 0) {
        switch (ch) {
        case CMD_STORE_ARGS_KEY:
            // reject key if too long
            if (strlen(arg) >= NVME_KV_MAX_KEY_LENGTH) {
                SPDK_ERRLOG(
                    "The provided key is too long. The max length is %d.\n",
                    NVME_KV_MAX_KEY_LENGTH);
                return -EINVAL;
            }

            ((struct cmd_store_args *)cmd_args)->key = arg;
            // printf("CMD_STORE_ARGS_KEY set to: %s\n",
            //        ((struct cmd_store_args *)cmd_args)->key);
            provided_args |= 1 << CMD_STORE_ARGS_KEY;
            break;
        case CMD_STORE_ARGS_INPUT_FILE:
            if (access(arg, F_OK) == -1) {
                SPDK_ERRLOG("Input file does not exist.\n");
            }

            ((struct cmd_store_args *)cmd_args)->input_file = arg;
            // printf("CMD_STORE_ARGS_INPUT_FILE set to: %s\n",
            //        ((struct cmd_store_args *)cmd_args)->input_file);
            provided_args |= 1 << CMD_STORE_ARGS_INPUT_FILE;
            break;
        case CMD_STORE_ARGS_APPEND:
            ((struct cmd_store_args *)cmd_args)->append = true;
            // printf("CMD_STORE_ARGS_APPEND set to: %d\n",
            //        ((struct cmd_store_args *)cmd_args)->append);
            break;
        default:
            return -EINVAL;
        }
    } else if (strcmp(command, "list") == 0) {
        switch (ch) {
        case CMD_LIST_ARGS_KEY:
            // reject key if too long
            if (strlen(arg) >= NVME_KV_MAX_KEY_LENGTH) {
                SPDK_ERRLOG(
                    "The provided key is too long. The max length is %d.\n",
                    NVME_KV_MAX_KEY_LENGTH);
                return -EINVAL;
            }
            ((struct cmd_list_args *)cmd_args)->key = arg;
            // printf("CMD_LIST_ARGS_KEY set to: %s\n",
            //        ((struct cmd_list_args *)cmd_args)->key);
            break;
        default:
            return -EINVAL;
        }
    } else if (strcmp(command, "exists") == 0) {
        switch (ch) {
        case CMD_EXISTS_ARGS_KEY:
            // reject key if too long
            if (strlen(arg) >= NVME_KV_MAX_KEY_LENGTH) {
                SPDK_ERRLOG(
                    "The provided key is too long. The max length is %d.\n",
                    NVME_KV_MAX_KEY_LENGTH);
                return -EINVAL;
            }
            ((struct cmd_exists_args *)cmd_args)->key = arg;
            // printf("CMD_EXISTS_ARGS_KEY set to: %s\n",
            //        ((struct cmd_exists_args *)cmd_args)->key);
            provided_args |= 1 << CMD_EXISTS_ARGS_KEY;
            break;
        default:
            return -EINVAL;
        }
    } else if (strcmp(command, "delete") == 0) {
        switch (ch) {
        case CMD_DELETE_ARGS_KEY:
            // reject key if too long
            if (strlen(arg) >= NVME_KV_MAX_KEY_LENGTH) {
                SPDK_ERRLOG(
                    "The provided key is too long. The max length is %d.\n",
                    NVME_KV_MAX_KEY_LENGTH);
                return -EINVAL;
            }
            ((struct cmd_delete_args *)cmd_args)->key = arg;
            // printf("CMD_DELETE_ARGS_KEY set to: %s\n",
            //        ((struct cmd_delete_args *)cmd_args)->key);
            provided_args |= 1 << CMD_DELETE_ARGS_KEY;
            break;
        default:
            return -EINVAL;
        }
    } else if (strcmp(command, "retrieve") == 0) {
        switch (ch) {
        case CMD_RETRIEVE_ARGS_KEY:
            // reject key if too long
            if (strlen(arg) >= NVME_KV_MAX_KEY_LENGTH) {
                SPDK_ERRLOG(
                    "The provided key is too long. The max length is %d.\n",
                    NVME_KV_MAX_KEY_LENGTH);
                return -EINVAL;
            }
            ((struct cmd_retrieve_args *)cmd_args)->key = arg;
            // printf("CMD_RETRIEVE_ARGS_KEY set to: %s\n",
            //        ((struct cmd_retrieve_args *)cmd_args)->key);
            provided_args |= 1 << CMD_RETRIEVE_ARGS_KEY;
            break;
        case CMD_RETRIEVE_ARGS_OUTPUT_FILE:
            ((struct cmd_retrieve_args *)cmd_args)->output_file = arg;
            // printf("CMD_RETRIEVE_ARGS_OUTPUT_FILE set to: %s\n",
            //        ((struct cmd_retrieve_args *)cmd_args)->output_file);
            provided_args |= 1 << CMD_RETRIEVE_ARGS_OUTPUT_FILE;
            break;
        case CMD_RETRIEVE_ARGS_OFFSET:
            ((struct cmd_retrieve_args *)cmd_args)->offset = atoi(arg);
            // printf("CMD_RETRIEVE_ARGS_OFFSET set to: %lu\n",
            //        ((struct cmd_retrieve_args *)cmd_args)->offset);
            break;
        default:
            return -EINVAL;
        }
    } else if (strcmp(command, "select") == 0) {
        switch (ch) {
        case CMD_SELECT_ARGS_KEY:
            // reject key if too long
            if (strlen(arg) >= NVME_KV_MAX_KEY_LENGTH) {
                SPDK_ERRLOG(
                    "The provided key is too long. The max length is %d.\n",
                    NVME_KV_MAX_KEY_LENGTH);
                return -EINVAL;
            }
            ((struct cmd_select_args *)cmd_args)->key = arg;
            // printf("CMD_SELECT_ARGS_KEY set to: %s\n",
            //        ((struct cmd_select_args *)cmd_args)->key);
            provided_args |= 1 << CMD_SELECT_ARGS_KEY;
            break;
        case CMD_SELECT_ARGS_SQL:
            ((struct cmd_select_args *)cmd_args)->sql = arg;
            // printf("CMD_SELECT_ARGS_SQL set to: %s\n",
            //        ((struct cmd_select_args *)cmd_args)->sql);
            provided_args |= 1 << CMD_SELECT_ARGS_SQL;
            break;
        case CMD_SELECT_ARGS_INPUT_FORMAT:
            int input_format_code = -1;
            if (strcmp(arg, "csv") == 0) {
                input_format_code = 0;
            } else if (strcmp(arg, "json") == 0) {
                input_format_code = 1;
            } else if (strcmp(arg, "parquet") == 0) {
                input_format_code = 2;
            }
            if (input_format_code == -1) {
                SPDK_ERRLOG(
                    "Invalid input format. Valid formats are: csv, json, parquet\n");
                return -EINVAL;
            }
            ((struct cmd_select_args *)cmd_args)->input_format =
                input_format_code;
            // printf("CMD_SELECT_ARGS_INPUT_FORMAT set to: %s\n",
            //        ((struct cmd_select_args *)cmd_args)->input_format);
            provided_args |= 1 << CMD_SELECT_ARGS_INPUT_FORMAT;
            break;
        case CMD_SELECT_ARGS_OUTPUT_FORMAT:
            int output_format_code = -1;
            if (strcmp(arg, "csv") == 0) {
                output_format_code = 0;
            } else if (strcmp(arg, "json") == 0) {
                output_format_code = 1;
            } else if (strcmp(arg, "parquet") == 0) {
                output_format_code = 2;
            }
            if (output_format_code == -1) {
                SPDK_ERRLOG(
                    "Invalid output format. Valid formats are: csv, json, parquet\n");
                return -EINVAL;
            }
            ((struct cmd_select_args *)cmd_args)->output_format =
                output_format_code;
            // printf("CMD_SELECT_ARGS_OUTPUT_FORMAT set to: %s\n",
            //        ((struct cmd_select_args *)cmd_args)->output_format);
            provided_args |= 1 << CMD_SELECT_ARGS_OUTPUT_FORMAT;
            break;
        case CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_INPUT:
            ((struct cmd_select_args *)cmd_args)->use_csv_header_for_input =
                true;
            // printf(
            //     "CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_INPUT set to: %d\n",
            //     ((struct cmd_select_args
            //     *)cmd_args)->use_csv_header_for_input);
            break;
        case CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_OUTPUT:
            ((struct cmd_select_args *)cmd_args)->use_csv_header_for_output =
                true;
            // printf("CMD_SELECT_ARGS_USE_CSV_HEADER_FOR_OUTPUT set to: %d\n",
            //        ((struct cmd_select_args *)cmd_args)
            //            ->use_csv_header_for_output);
            break;
        case CMD_SELECT_ARGS_FILE:
            ((struct cmd_select_args *)cmd_args)->file = arg;
            printf("CMD_SELECT_ARGS_FILE set to: %s\n",
                   ((struct cmd_select_args *)cmd_args)->file);
            provided_args |= 1 << CMD_SELECT_ARGS_FILE;
            break;
        default:
            return -EINVAL;
        }
    } else {
        return -EINVAL;
    }

    return 0;
}

// validates whether the required arguments for the command are provided
int
validate_args(void) {
    if (strcmp(command, "store") == 0) {
        if (provided_args !=
            (1 << CMD_STORE_ARGS_KEY | 1 << CMD_STORE_ARGS_INPUT_FILE)) {
            SPDK_ERRLOG("Invalid arguments for store command.\n");
            return -EINVAL;
        }
    } else if (strcmp(command, "exists") == 0) {
        if (provided_args != (1 << CMD_EXISTS_ARGS_KEY)) {
            SPDK_ERRLOG("Invalid arguments for exists command.\n");
            return -EINVAL;
        }
    } else if (strcmp(command, "delete") == 0) {
        if (provided_args != (1 << CMD_DELETE_ARGS_KEY)) {
            SPDK_ERRLOG("Invalid arguments for delete command.\n");
            return -EINVAL;
        }
    } else if (strcmp(command, "retrieve") == 0) {
        if (provided_args !=
            (1 << CMD_RETRIEVE_ARGS_KEY | 1 << CMD_RETRIEVE_ARGS_OUTPUT_FILE)) {
            SPDK_ERRLOG("Invalid arguments for retrieve command.\n");
            return -EINVAL;
        }
    } else if (strcmp(command, "select") == 0) {
        if (provided_args !=
            (1 << CMD_SELECT_ARGS_KEY | 1 << CMD_SELECT_ARGS_SQL |
             1 << CMD_SELECT_ARGS_INPUT_FORMAT |
             1 << CMD_SELECT_ARGS_OUTPUT_FORMAT | 1 << CMD_SELECT_ARGS_FILE)) {
            SPDK_ERRLOG("Invalid arguments for select command.\n");
            return -EINVAL;
        }
    }

    return 0;
}