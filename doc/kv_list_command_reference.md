# KV List Commands

## Table of Contents

- [QEMU](#qemu)
  - [Commands](#commands)
    - [KV_LIST](#kv_list)
    - [KV_DELETE](#kv_delete)
    - [KV_EXIST](#kv_exist)
    - [KV_STORE](#kv_store)
    - [KV_RETRIEVE](#kv_retrieve)
    - [KV_SEND_SELECT](#kv_send_select)
    - [KV_RETRIEVE_SELECT](#kv_retrieve_select)
  - [Code changes](#code-changes)
- [SPDK Library Changes](#spdk-library-changes)

## QEMU

### Commands

We have modified the NVMe emulator in QEMU.

The following nvme commands have been added:

#### KV_LIST

- Fetch list of keys in KV store
- Opcode: 0x06
- Inputs:
  - DW10 - Host Buffer Size
  - DW11 - Length of key prefix to search for
  - DW2,3,14,15 - key prefix to search for
  - DPTR - buffer to put results in
- Outputs:
  - Write list of keys in host buffer. The format is defined in NVME Express Key Value Command Set Specification 1.0
- CQE (Complete Queue Entry) Status:
  - 0x00: Success
  - 0x86: Invalid key size
- CQE Result:
  - DW0 - Total number of keys found

#### KV_DELETE

- Delete an object from KV store
- Opcode: 0x10
- Inputs:
  - DW11 - Length of key
  - DW2,3,14,15 - key to delete
- CQE status:
  - 0x00: Success
  - 0x87: key not found
  - 0x86: invalid key size

#### KV_EXIST

- Check if object exists in KV store
- Opcode: 0x14
- Inputs:
  - DW11 - Length of key
  - DW2,3,14,15 - key to delete
- CQE Status:
  - 0x00: Success
  - 0x87: key not found
  - 0x86: invalid key size

#### KV_STORE

- Store object in KV store
- Opcode: 0x81
- Inputs
  - DW10 - Size of data to store
  - DW11 - Length of key and options
    - Length of key (at byte 0xff)
    - Options
      - 0x0100 - the object for the key must already exist
      - 0x0200 - the object for the key must not already exists
      - 0x0800 - append data to object if it exists rather than truncating file
  - DW2,3,14,15 - key to store
  - DPTR - data to store
- CQE Status:
  - 0x00: Success
  - 0x87: key not found and option 0x0100 was set
  - 0x86: invalid key size
  - 0x89: key exists and option 0x0200 was set

#### KV_RETRIEVE

- Retrieve object from KV store
- Opcode: 0x82
- Inputs
  - DW10 - Size of buffer to read data into
  - DW11 - Length of key
  - DW12 - offset to read data from
  - DW2,3,14,15 - key to read
  - DPTR - buffer to read data into (data is truncated if larger than buffer size)
- CQE Status:
  - 0x00: Success
  - 0x87: key not found
  - 0x86: invalid key size
- CQE Result:
  - DW0 - Total size of file

#### KV_SEND_SELECT

- Run query on object in KV store
- Opcode: 0x83
- Input
  - DW10 - Size of select command
  - DW11 - Length of key, input type, output type, options
    - Length of key (at byte 0xff)
    - Options
      - 0x0100 - use CSV header for input
      - 0x0200 - use CSV header for output
    - Input type (at byte 0xff0000)
      - CSV - 0
      - JSON - 1
      - PARQUET - 2
    - Output type (at byte 0xff000000)
      - CSV - 0
      - JSON - 1
      - PARQUET - 2
  - DW2,3,14,15 - key to read
  - DPTR - buffer to read select string from. The string sent should be of SQL “select …” syntax.
- CQE Status:
  - 0x00: Success
  - 0x87: key not found
  - 0x86: invalid key size
- CQE Result:
  - DW0 - ID for result to use in KV_RETRIEVE_SELECT

#### KV_RETRIEVE_SELECT

- Fetch results from KV_SEND_SELECT
- Opcode: 0x84
- Inputs
  - DW10 - Size of buffer to read data into
  - DW11 - Options
    - Options
      - 0x01 - do not free results after retrieving
      - 0x02 - only free results if they all fit into host buffer
  - DW12 - offset to read data from
  - DW13 - ID returned from KV_SEND_SELECT
  - DPTR - buffer to read data into (data is truncated if larger than buffer size)
- CQE Status:
  - 0x00: Success
  - 0x87: key not found
  - 0x86: invalid key size
- CQE Result:
  - DW0 - Total size of query data

### Code changes

The code changes in QEMU consists of:

- hw/nvme/ctrl.c, ctrl-kv.c:
  - The new commands were added into the QEMU nvme command processor. ctrl-kv.c is a new file so the new logic is largely isolated to that file rather than the existing ctrl.c The requests are parsed into a NvmeKvCmd structure and then handled using the KV and query engine we added.
- util/kv-store.c
  - This is the KV store we added the QEMU. It uses the host QEMU is being run on to store the KV objects in the file system as individual files.
- util/kv-tasks.c
  - In order for QEMU not to block as the new KV and query operations are run, we created a thread pool using the main loop and event notifier routines that are part of QEMU. A pool of threads process the requests and once complete notify the main loop that runs nvme/ctrl.c that the results are ready to send back.
- util/select-results.c
  - Because nvme commands are read or write, the select query was broken up into two commands. The KV_SEND_SELECT sends the buffer with the query command to run. The KV_RETRIEVE_SELECT commands retrieves the data. select-results.c is used to store the select results in between those commands.
- util/query.c
  - This is the query engine. It uses duckdb to run the query on the KV object passed in the select command and export the results to the desired output format. An example of a query it would run is:
    - `COPY (SELECT col1 FROM tbl) TO 'output_results.csv' (HEADER, DELIMITER ',')`;
  - The results of the query are then returned to the nvme/ctrl-kv.c layer which puts them in the dptr.
  - Multiple duckdb connections are used so that multiple threads can be doing select queries at the same time.

## SPDK Library Changes

todo
