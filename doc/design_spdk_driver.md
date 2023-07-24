# SPDK Device Driver Design Document

This page outlines the implementation of the SPDK driver library for the CSD project.

## Overview

We intend to leverage the user space storage library, SPDK, for use in the CSD project. As part of this project we are extending the NVMe command set with new custom commands. To access these new command codes, we will extend the SPDK library to add new functions for these commands, mimicking existing commands such as `spdk_nvme_ns_cmd_read`.

As the commands are an extension of the existing NVMe command set, it makes sense to leverage the existing `spdk_nvme_ns_*` code base rather than creating new directories and copying code functionality. Doing this will allow us to reuse the standard command for probing, attaching, and detaching devices.

## Repository and Development

The development branch was forked from the latest long term support release tag (currently `v23.01.1`).

## AirMettle NVMe Additions

As part of this project, we extend the NVMe command set by modifying the virtualized NVMe code in the QEMU virtual machine. The externally viewable changes are in the form of the following new commands and error codes.

## Commands

These are the new NVMe commands added to the NVMe v1.4 command set.

| Command                       | Code   |
| ----------------------------- | ------ |
| `NVME_CMD_KV_LIST`            | `0x06` |
| `NVME_CMD_KV_DELETE`          | `0x10` |
| `NVME_CMD_KV_EXIST`           | `0x14` |
| `NVME_CMD_KV_STORE`           | `0x81` |
| `NVME_CMD_KV_RETRIEVE`        | `0x82` |
| `NVME_CMD_KV_SEND_SELECT`     | `0x83` |
| `NVME_CMD_KV_RETRIEVE_SELECT` | `0x84` |

See [KV list](kv_list_command_reference.md) for some further details on commands.

## Error Codes

These are the new NVMe error codes added to the NVMe v1.4 command set (from `/include/block/nvme.h` in the QEMU codebase):

| Error                       | Code     |
| --------------------------- | -------- |
| `NVME_INVALID_KV_SIZE`      | `0x0086` |
| `NVME_KV_NOT_FOUND`         | `0x0087` |
| `NVME_KV_ERROR`             | `0x0088` |
| `NVME_KV_EXISTS`            | `0x0089` |
| `NVME_KV_INVALID_PARAMETER` | `0x0090` |

## New Functions

Note: all the following functions will have the following arguments in addition to those mentioned explicitly below:

| Function                        | Description                                   |
| ------------------------------- | --------------------------------------------- |
| `struct spdk_nvme_ns *ns`       | namespace being accessed                      |
| `struct spdk_nvme_qpair *qpair` | queue pair used for accessing the controller. |

### `spdk_nvme_ns_cmd_kvlist()`

Fetches a list of all keys for any key-values (aka objects) stored on the device. The keys are returned in a preallocated `void *` payload buffer. The format of the returned buffer will be a `NULL` terminated array of `char *` pointers.

#### Parameters

| Name                     | Description                                                |
| ------------------------ | ---------------------------------------------------------- |
| `char *prefix`           | First key to list from                                     |
| `size_t prefix_len`      | Length of prefix                                           |
| `void *buffer`           | caller allocated buffer for returning list of keys.        |
| `uint32_t buffer_size`   | size of the allocated payload buffer                       |
| `spdk_nvme_cmd_cb cb_fn` | function to be called upon completion of command           |
| `void *cb_arg`           | argument to pass to the callback function                  |
| `uint32_t io_flags`      | I/O flags (see `include/spdk/nvme_spec.h` lines 3790-3810) |

#### Returns

| Code      | Description                                   |
| --------- | --------------------------------------------- |
| `0`       | Success                                       |
| `-EINVAL` | The request is malformed.                     |
| `-ENOMEM` | The request cannot be allocated.              |
| `-ENXIO`  | The queue pair failed at the transport level. |

#### Command Completion

| Field       | Type       | Value                           |
| ----------- | ---------- | ------------------------------- |
| `DWORD 0`   | `uint32_t` | -                               |
| `DWORD 1`   | `uint32_t` | Number of keys returned in list |
| Status Code | -          | See below                       |

| Status Code            | Description                                        |
| ---------------------- | -------------------------------------------------- |
| `00h`                  | Success                                            |
| `01h`                  | Invalid Command Opcode (eg. non KV-enabled device) |
| `NVME_INVALID_KV_SIZE` | if key too large                                   |

#### Notes

- See `spdk_nvme_ctrlr_cmd_get_feature()` for implementation inspiration.

### `spdk_nvme_ns_cmd_kvlistv()` FUTURE

Similar to above but return list in a scatter gather list.

#### Parameters

| Name                                      | Description                                                         |
| ----------------------------------------- | ------------------------------------------------------------------- |
| `spdk_nvme_cmd_cb cb_fn`                  | function to be called upon completion of command                    |
| `void *cb_arg`                            | argument to pass to the callback function                           |
| `uint32_t io_flags`                       | I/O flags (see `include/spdk/nvme_spec.h` lines 3790-3810)          |
| `spdk_nvme_req_reset_sgl_cb reset_sgl_fn` | Callback function to reset scattered payload.                       |
| `spdk_nvme_req_next_sge_cb`               | Callback function to iterate each scattered payload memory segment. |

#### Returns

| Code      | Description                                   |
| --------- | --------------------------------------------- |
| `0`       | Success                                       |
| `-EINVAL` | The request is malformed.                     |
| `-ENOMEM` | The request cannot be allocated.              |
| `-ENXIO`  | The queue pair failed at the transport level. |

### `spdk_nvme_ns_cmd_kvdelete()`

Deletes the object associated with the provided key from the device.

#### Parameters

| Name                     | Description                                                |
| ------------------------ | ---------------------------------------------------------- |
| `char *key`              | Key identifying object to be deleted                       |
| `size_t key_len`         | Length of key                                              |
| `spdk_nvme_cmd_cb cb_fn` | function to be called upon completion of command           |
| `void *cb_arg`           | argument to pass to the callback function                  |
| `uint32_t io_flags`      | I/O flags (see `include/spdk/nvme_spec.h` lines 3790-3810) |

#### Returns

| Code      | Description                                   |
| --------- | --------------------------------------------- |
| `0`       | Success                                       |
| `-EINVAL` | The request is malformed.                     |
| `-ENOMEM` | The request cannot be allocated.              |
| `-ENXIO`  | The queue pair failed at the transport level. |

### `spdk_nvme_ns_cmd_kvexist()`

Checks if the given key is defined.

#### Parameters

| Name                     | Description                                                |
| ------------------------ | ---------------------------------------------------------- |
| `char *key`              | Key to be queried                                          |
| `size_t key_len`         | Length of key                                              |
| `spdk_nvme_cmd_cb cb_fn` | function to be called upon completion of command           |
| `void *cb_arg`           | argument to pass to the callback function                  |
| `uint32_t io_flags`      | I/O flags (see `include/spdk/nvme_spec.h` lines 3790-3810) |

#### Returns

| Code      | Description                                   |
| --------- | --------------------------------------------- |
| `0`       | Success                                       |
| `-EINVAL` | The request is malformed.                     |
| `-ENOMEM` | The request cannot be allocated.              |
| `-ENXIO`  | The queue pair failed at the transport level. |

### `spdk_nvme_ns_cmd_kvstore()`

Stores the passed data as an object associated with given key.

#### Parameters

| Name                     | Description                                      |
| ------------------------ | ------------------------------------------------ |
| `char *key`              | Key to be stored                                 |
| `size_t key_len`         | Length of key                                    |
| `void *payload`          | Data to be stored                                |
| `uint32_t payload_size`  | Size of data                                     |
| `spdk_nvme_cmd_cb cb_fn` | Function to be called upon completion of command |
| `void *cb_arg`           | Argument to pass to the callback function        |
| `uint8_t store_flags`    | Option flags for store operation                 |
| `uint32_t io_flags`      | I/O flags                                        |

#### Returns

| Code      | Description                                   |
| --------- | --------------------------------------------- |
| `0`       | Success                                       |
| `-EINVAL` | The request is malformed.                     |
| `-ENOMEM` | The request cannot be allocated.              |
| `-ENXIO`  | The qpair failed at the transport level. |

### `spdk_nvme_ns_cmd_kvstorev()` FUTURE

Same as above but using a scatter gather list for object

#### Parameters

| Name                     | Description                                      |
| ------------------------ | ------------------------------------------------ |
| `char *key`              | Key to be stored                                 |
| `size_t key_len`         | Length of key                                    |
| `uint32_t buffer_sz`     | Size of data                                     |
| `uint32_t io_flags`      | I/O flags                                        |
| `spdk_nvme_req_reset_sgl_cb reset_sgl_fn` | Callback function to reset scattered payload. |
| `spdk_nvme_req_next_sge_cb` | Callback function to iterate each scattered payload memory segment. |
| `uint8_t store_flags`    | Option flags for store operation                 |
| `uint32_t io_flags`      | I/O flags                                        |

#### Returns

| Code      | Description                                   |
| --------- | --------------------------------------------- |
| `0`       | Success                                       |
| `-EINVAL` | The request is malformed.                     |
| `-ENOMEM` | The request cannot be allocated.              |
| `-ENXIO`  | The qpair failed at the transport level.      |

### `spdk_nvme_ns_cmd_kvretrieve()`

Retrieve object for specified key

#### Parameters

| Name                     | Description                                      |
| ------------------------ | ------------------------------------------------ |
| `char *key`              | Key of object to be retrieved                    |
| `size_t key_len`         | Length of key                                    |
| `void *buffer`           | buffer to store retrieved object                 |
| `uint32_t buffer_sz`     | size of buffer                                   |
| `spdk_nvme_req_reset_sgl_cb reset_sgl_fn` | Callback function to reset scattered payload. |
| `spdk_nvme_req_next_sge_cb` | Callback function to iterate each scattered payload memory segment. |
| `uint32_t io_flags`      | I/O flags                                        |

#### Command Completion

| Field       | Type       | Value                           |
| ----------- | ---------- | ------------------------------- |
| `DWORD 0`   | `uint32_t` | -                               |
| `DWORD 1`   | `uint32_t` | Data size                       |
| Status Code | -          | See below                       |

| Status Code                 | Description                                        |
| --------------------------- | -------------------------------------------------- |
| `00h`                       | Success                                            |
| `01h`                       | Invalid Command Opcode (eg. non KV-enabled device) |
| `NVME_INVALID_KV_SIZE`      | if key too large                                   |
| `NVME_INVALID_KV_NOT_FOUND` | if key not found                                   |

### `spdk_nvme_ns_cmd_kvretrievev()` FUTURE

Same as above but using a scatter gather list for retrieving object.

### `spdk_nvme_ns_cmd_kvselect_send()`

Sends the select command. The actual data is not returned by this command. Instead a unique select ID is returned that can be used by `spdk_nvme_ns_cmd_retrieve_select()` below.

#### Parameters

- `char *key` - key to be queried
- `size_t key_len` - Key length
- `char *query` - SQL SELECT query
- `spdk_nvme_kv_datatype input_type` - Format of file (CSV, JSON, etc)
- `spdk_nvme_kv_datatype output_type` - format of generated results
- `uint8_t header_opts` - header parse options
- `spdk_nvme_req_reset_sgl_cb reset_sgl_fn` - Callback function to reset scattered payload.
- `spdk_nvme_req_next_sge_cb` - Callback function to iterate each scattered payload memory segment.
- `uint8_t store_flags` - option flags for store operation
- `uint32_t io_flags` - I/O flags

| Name                     | Description                                                |
| ------------------------ | ---------------------------------------------------------- |
| `char *key`              | Key to be queried                                          |
| `size_t key_len`         | Length of key                                              |
| `char *query`            | SQL SELECT query                                           |
| `spdk_nvme_kv_datatype input_type` | Format of file (CSV, JSON, etc)                    |
| `spdk_nvme_kv_datatype output_type` | format of generated results                     |
| `uint8_t header_opts`    | header parse options                                       |
| `spdk_nvme_req_reset_sgl_cb reset_sgl_fn` | Callback function to reset scattered payload. |
| `spdk_nvme_req_next_sge_cb` | Callback function to iterate each scattered payload memory segment. |
| `uint8_t store_flags`    | option flags for store operation                           |
| `uint32_t io_flags`      | I/O flags (see `include/spdk/nvme_spec.h` lines 3790-3810) |

#### Returns

#### Command Completion

| Field       | Type       | Value                                                     |
| ----------- | ---------- | --------------------------------------------------------- |
| `DWORD 0`   | `uint32_t` | -                                                         |
| `DWORD 1`   | `uint32_t` | Select ID (for use by `spdk_nvme_ns_cmd_retrieve_select`) |
| Status Code | -          | See below                                                 |

| Status Code            | Description                                        |
| ---------------------- | -------------------------------------------------- |
| `00h`                  | Success                                            |
| `01h`                  | Invalid Command Opcode (eg. non KV-enabled device) |

### `spdk_nvme_ns_cmd_kvselect_retrieve()`

Returns the results of a previous `spdk_nvme_ns_cmd_kvselect_send()`. Uses the `select_id` returned by that command as a unique identifier of the results.

#### Parameters

| Name                     | Description                                                |
| ------------------------ | ---------------------------------------------------------- |
| `uint32_t select_id`     | Select ID returned by command above                        |
| `uint32_t offset`        | Offset from start of results (in bytes)                    |
| `void *buffer`           | caller allocated buffer for returning results.             |
| `size_t buffer_size`     | buffer size                                                |
| `spdk_nvme_kv_select_opts opts` | option flags                                       |
| `spdk_nvme_req_reset_sgl_cb reset_sgl_fn` | Callback function to reset scattered payload. |
| `spdk_nvme_req_next_sge_cb` | Callback function to iterate each scattered payload memory segment. |
| `uint32_t io_flags`      | I/O flags (see `include/spdk/nvme_spec.h` lines 3790-3810) |

#### Returns

#### Command Completion

| Field       | Type       | Value                           |
| ----------- | ---------- | ------------------------------- |
| `DWORD 0`   | `uint32_t` | -                               |
| `DWORD 1`   | `uint32_t` | Select ID (for use by `spdk_nvme_ns_cmd_retrieve_select`) |
| Status Code | -          | See below                       |

| Status Code            | Description                                        |
| ---------------------- | -------------------------------------------------- |
| `00h`                  | Success                                            |
| `01h`                  | Invalid Command Opcode (eg. non KV-enabled device) |
| `NVME_INVALID_KV_SIZE` | if key too large                                   |
| `NVME_INVALID_KV_NOT_FOUND` | if key not found                              |

#### Notes

- If the size of the select results is greater than the provided buffer, only the buffer size of data will be returned. `DWORD 1` will contain the total number of bytes in the select results. No error status will be sent.

### `spdk_nvme_ns_cmd_retrieve_selectv()` FUTURE

Same as above but return to a scatter-gather list

## Callbacks

Most of the new functions take arguments for a callback function (type `spdk_nvme_cmd_cb`) and an opaque `void *` callback argument that will be passed to the function upon command completion.

The callback function type is defined as:

```c
/**
 * Signature for callback function invoked when a command is completed.
 *
 * \param ctx Callback context provided when the command was submitted.
 * \param cpl Completion queue entry that contains the completion status.
 */
typedef void (*spdk_nvme_cmd_cb)(void *ctx, const struct spdk_nvme_cpl *cpl);
```

With the completion queue entry defined as:

```c
/**
 * Completion queue entry
 */
struct spdk_nvme_cpl {
    /* dword 0 */
    uint32_t        cdw0;   /* command-specific */

    /* dword 1 */
    uint32_t        cdw1;   /* command-specific */

    /* dword 2 */
    uint16_t        sqhd;   /* submission queue head pointer */
    uint16_t        sqid;   /* submission queue identifier */

    /* dword 3 */
    uint16_t        cid;    /* command identifier */
    union {
        uint16_t                status_raw;
        struct spdk_nvme_status status;
    };
};
```

The status struct is defined as:

```c
struct spdk_nvme_status {
    uint16_t p   :  1;   /* phase tag */
    uint16_t sc  :  8;   /* status code */
    uint16_t sct :  3;   /* status code type */
    uint16_t crd :  2;   /* command retry delay */
    uint16_t m   :  1;   /* more */
    uint16_t dnr :  1;   /* do not retry */
};
```

The new functions outlined above will return errors only if there is a problem submitting to the command queue, they are otherwise asynchronous and rely on caller-supplied callbacks to handle the actual command completion.

The NVMe protocol 1.4 specifies that `dword 0` is command specific (ie. we can define what we return in this field) and `dword 1` is reserved. Thus we can return a total of ~40 bits of command specific information from the NVMe command (32 bits in `dword 0` and an 8 bit status code).

## Implementation Notes

The above new functions will mostly be small wrappers around two key functions:

| Function                        | Description                                           |
| ------------------------------- | ----------------------------------------------------- |
| `_nvme_ns_cmd_setup_request()`  | Sets up the command request with the given parameters |
| `nvme_qpair_submit_request()`   | Sends the request to the given queue pair             |

For an example look at the existing `spdk_nvme_ns_cmd_read()` implementation.

Note: The standard NVMe request references LBA (logical block addresses) and other block-related parameters that may need to be redefined or ignored for our use case.

## Unit Tests

SPDK contains a unit test framework that allows for testing of functions without requiring special hardware or additional setup. The relevant unit tests for NVMe commands are contained in the file `test/unit/lib/nvme/nvme_ns_cmd.c/nvme_ns_cmd_ut.c`. New tests can be added by creating a test function (`static void test_xxx(void)`) and use the macro `CU_ADD_TEST()` in the `main()` function of the above file.

Unit tests for SPDK can be run by calling the `test/unit/unittest.sh` script or by running the individual compiled test executable directly (in our case, `test/unit/lib/nvme/nvme_ns_cmd.c/nvme_ns_cmd_ut`).

Within the each unit test, the helper function `prepare_for_test()` can be used to create a mock controller (`struct spdk_nvme_ctrlr`) and associated mock queue pair (`struct spdk_nvme_qpair`) for use in the test. Based on the existing tests, it appears most of the unit tests focus on ensuring the request is properly constructed. There does not seem to be any mocked triggering of the callback functions that I see.

## Integration Tests

NVMe related integration tests are located in `test/nvme`. Several individual test executables are defined in separate directories and are then run from the `nvme.sh` test runner script. We should be able to use this for basic first pass testing. It may be desirable to keep separate from the main script (see `nvme_bp.sh` and other standalone test scripts) as KV select enabled drives are not always present.

For initial development, compile QEMU from the repository using a version containing hardcoded command responses (commit `4d49338f5b`). Once basic send/receive of commands is verified, one can move on to more recently modified version of QEMU.
