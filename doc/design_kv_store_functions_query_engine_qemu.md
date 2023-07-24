# KV Store Functions and Query Engine in QEMU

We add a new KV store function for each of the newly added NVMe command, and put them under the util folder in the qemu repo. We use DuckDB to serve the select queries.

## File locations

The `store_object`, `read_object`, `file_exist`, `delete_object`, `list_objects` functions are implemented in `util/kv_store.c`, and the corresponding header file is `include/qemu/kv_store.h`. The `run_query` function is implemented in `util/query.c`, and the corresponding header file is `include/qemu/query.h`. The unit tests are in `tests/unit/test-kv.c`.

## Repository and Development

The QEMU repository is forked and a branch called `nvme_command` is created from the `stable-7.2` branch. All changes are made to the `nvme_command` branch. The latest DuckDB (0.8.0) header file is put into `include/duckdb`.

To build the project, simply run `build.sh` under the root directory of the repository. It will make a folder `~/duckdb` and download the library file of DuckDB, and then build QEMU under the `build` directory.

## Error Codes

- `#define KV_ERROR_INVALID_PARAMETER (-1)`- parameters are invalid, e.g. both `must_exist` and `must_not_exist` are true
- `#define KV_ERROR_FILE_PATH (-2)`- something wrong with the file path, like the `BASE_DIR` env variable is not set
- `#define KV_ERROR_FILE_EXISTS (-3)`- file should not exist but it is found
- `#define KV_ERROR_FILE_NOT_FOUND (-4)`- file should exist but it is not found
- `#define KV_ERROR_CANNOT_OPEN (-5)`- cannot open the file
- `#define KV_ERROR_FILE_WRITE (-6)`- something wrong when writing to the file
- `#define KV_ERROR_FILE_OFFSET (-7)` - cannot go to the specified offset of the file
- `#define KV_ERROR_QUERY (-8)` - something wrong when running the query
- `#define KV_ERROR_FILE_READ (-9)` - something wrong when reading file
- `#define KV_ERROR_MEMORY_ALLOCATION (-10)` - cannot allocate memory
- `#define KV_ERROR_PIPE (-11)` - cannot create / read from pipe
- `#define KV_ERROR_FORK (-12)` - cannot do fork
- `#define KV_ERROR_DUCKDB (-13)` - DuckDB error
- `#define KV_ERROR_REMOVE (-14)` - Cannot remove the key
- `#define KV_ERROR_KEY_TOO_LONG (-15)` - Key is longer than 16 bytes

## KV Store Functions

### `ssize_t store_object()`

returns number of bytes written, negative values on errors. If append is false, create new file overwriting and truncating if one already exists. If append is true, append to existing file. If file does not exist, return error

#### Parameters

- `uint32_t bus_number`
- `uint32_t namespace_id`
- `unsigned char *key` - the key of the key-value pair. can be binary. hex(key) will be the file name
- `size_t key_len` - the length of the key
- `unsigned char *value` - the value to be stored in the file. can be binary
- `size_t value_len` - length of the value
- `bool append` - If append is true, append to existing file. If file does not exist, return error
- `bool must_exist` - If file does not exist, return error
- `bool must_not_exist` - If file exists, return error

#### Returns

- number of bytes written on success
- `KV_ERROR_INVALID_PARAMETER`- parameters are invalid, e.g. both `must_exist` and `must_not_exist` are true
- `KV_ERROR_FILE_PATH`- something wrong with the file path, like the `BASE_DIR` env variable is not set
- `KV_ERROR_FILE_NOT_FOUND`- file should exist but it is not found
- `KV_ERROR_FILE_EXISTS`- file should not exist but it is found
- `KV_ERROR_CANNOT_OPEN`- cannot open the file
- `KV_ERROR_FILE_WRITE` - something wrong when writing to the file

#### Note

Need to set the `BASE_DIR` environment variable before using this function.

### `ssize_t read_object()`

returns number of bytes read, negative values on errors. if offset is non-zero, begin reading at that offset. buffer is where the data should be read into. `total_object_size` is the total size of the object

#### Parameters

- `uint32_t bus_number`
- `uint32_t namespace_id`
- `unsigned char *key` - key to read
- `size_t key_len`- size of the key
- `size_t offset` - offset for paging
- `unsigned char *buffer` - where the data should be read into
- `size_t max_buffer_len` - the length of the buffer
- `size_t *total_object_size` - the actual size of the object

#### Returns

- The actual bytes read on success
- `KV_ERROR_FILE_PATH`- something wrong with the file path, like the `BASE_DIR` env variable is not set
- `KV_ERROR_CANNOT_OPEN`- cannot open the file
- `KV_ERROR_FILE_OFFSET` - cannot go to the specified offset of the file

#### Note

Need to set the `BASE_DIR` environment variable before using this function.

### `int delete_object()`

delete the file of the key. return 0 on success

#### Parameters

- `uint32_t bus_number`
- `uint32_t namespace_id`
- `unsigned char *key` - key to delete
- `size_t key_len` - length of the key

#### Returns

- 0 on success
- `KV_ERROR_FILE_PATH`- something wrong with the file path, like the `BASE_DIR` env variable is not set
- `KV_ERROR_FILE_NOT_FOUND`- key not found
- `KV_ERROR_REMOVE` - Cannot remove the key

#### Note

Need to set the `BASE_DIR` environment variable before using this function.

### `int file_exist()`

returns whether the file exists given a key. 1 means file exists, 0 means file doesn't exist. negative values on errors

#### Parameters

- `uint32_t bus_number`
- `uint32_t namespace_id`
- `unsigned char *key` - key to delete
- `size_t key_len` - length of the key

#### Returns

- 0 on success
- `KV_ERROR_FILE_PATH`- something wrong with the file path, like the `BASE_DIR` env variable is not set

### `int list_objects()`

return keys in order that are greater or equal to key prefix, `NULL` on errors

#### Parameters

- `uint32_t bus_number`
- `uint32_t namespace_id`
- `unsigned char *key_prefix` - any key greater or equal to the prefix will be returned
- `size_t key_prefix_len` - length of the prefix
- `size_t offset` - offset for paging
- `size_t max_to_return` - max number of objects to return. If it is 0, it will be set to `0xFFFFFFFF`
- `size_t *num_objects_returned` - actual number of elements returned
- `ObjectKey **objects` - where the pointer to the result object list is stored

#### Returns

- 0 on success
- `KV_ERROR_FILE_PATH`- something wrong with the file path, like the `BASE_DIR` env variable is not set
- `KV_ERROR_MEMORY_ALLOCATION` - cannot allocate memory
- `KV_ERROR_KEY_TOO_LONG` - Key is longer than 16 bytes

```c
typedef struct ObjectKey {
    unsigned char key[16];
    size_t key_len;
} ObjectKey;
```

#### Note

- User of this function needs to free the `ObjectKey *` after use.
- Need to set the `BASE_DIR` environment variable before using this function.

### `unsigned char *run_query()`

run the query on the duckdb. `input_format` and `output_format` can be JSON, CSV, PARQUET. if `use_csv_headers` is true, assumes the input contains column names if `input_format` is CSV, and the output will contain the column names if `output_format` is CSV. query result is stored in result. `output_len` is the length of the output, return 0 on success, negative value on error.

#### Parameters

- `uint32_t bus_number`
- `uint32_t namespace_id`
- `unsigned char *key` - the key of the file to run query on
- `size_t key_length`
- `char *sql` - the query
- `size_t *output_len` - the actual length of output
- `char input_format` - can be JSON, CSV, PARQUET
- `char output_format` - can be JSON, CSV, PARQUET
- `bool use_csv_headers_input`- whether the input csv file has a header
- `bool use_csv_headers_output`- whether to use header in the output csv file
- `unsigned char **result` - where the pointer to the query result will be stored

#### Returns

- 0 on success
- `KV_ERROR_FILE_PATH`- something wrong with the file path, like the `BASE_DIR` env variable is not set
- `KV_ERROR_PIPE` - cannot create / read from pipe
- `KV_ERROR_FORK` - cannot do fork
- `KV_ERROR_QUERY` - something wrong when running the query
- `KV_ERROR_MEMORY_ALLOCATION` - cannot allocate memory

#### Note

- User of this function need to free the query result after use
- Need to do `init_db` before and `close_db` after using this function
- `init_db` specifies the size of connection pool of DuckDB
- examples of usage can be found in `tests/unit/test-kv.c`

## Unit Tests

The test case `tests/unit/test-kv.c` tests the functions above. The unit tests can be run by `make check-unit` and optionally adding `-j4` or other number to use multi-processing to speed up.
