# CSD NVMe Commands #

As part of the Computational Storage Device (CSD) development,
the following new commands were added to the standard NVMe command set:

- [NVME_CMD_KV_LIST](#nvme_cmd_kv_list) (`0x06`)
- [NVME_CMD_KV_DELETE](#nvme_cmd_kv_delete) (`0x10`)
- [NVME_CMD_KV_EXIST](#nvme_cmd_kv_exist) (`0x14`)
- [NVME_CMD_KV_STORE](#nvme_cmd_kv_store) (`0x81`)
- [NVME_CMD_KV_RETRIEVE](#nvme_cmd_kv_retrieve) (`0x82`)
- [NVME_CMD_KV_SEND_SELECT](#nvme_cmd_kv_send_select) (`0x83`)
- [NVME_CMD_KV_RETRIEVE_SELECT](#nvme_cmd_kv_retrieve_select) (`0x84`)

Where possible, the commands were chosen to align with the existing
[NVMe Key Value Command Set](https://nvmexpress.org/wp-content/uploads/NVM-Express-Key-Value-Command-Set-Specification-1.0b-2021.12.18-Ratified.pdf)

## Common Arguments ##

Several of the commands take similar arguments. The encoding of those arguments
is defined below:

### Key ###

Objects within the CSD are accessed via a key (16 bytes maximum).
Those commands which require a key argument will expect the key to be encoded
as follows:

Key Byte Range|NVMe Command Field
:-----------: | :--------------:
0 - 3         | `dword15`
4 - 7         | `dword14`
8 - 11        | `dword3`
12 - 15       | `dword2`

As the key length is separately encoded (see [Key Length](#key-length) below),
only those byte ranges needed for the desired key are required. It is not necessary
to provide zero termination or padding to the encoded key.

### Key Length ###

Key letngth is encoded in the lowest two bytes of `dword11`. The remaining bytes
of `dword11` are used for some command specific flags (see individual command definitions).

## Command Definitions ##

### NVME_CMD_KV_LIST ###

`NVME_CMD_KV_LIST` returns a list all of keys stored on the CSD device starting from the given key.

#### Command Inputs ####

NVMe Command Field | Description
:----------------: | :----------
`opcode`           | `0x06`
`dword2`           | key (bytes 12-15)
`dword3`           | key (bytes 8-11)
`dword10`          | host buffer size
`dword11`          | key length
`dword14`          | key (bytes 4-7)
`dword15`          | key (bytes 0-3)
`DPTR`             | address of host buffer

#### Completion ####

Upon completion of the command, one of the following status values is returned:

Status Code | Definition
:---------: | :---------
`0x00`      | success
`0x86`      | invalid key size

Upon successful completion, the list of matching keys is written to the host buffer.
The format is defined in [NVMe Key Value Command Set Specification](https://nvmexpress.org/wp-content/uploads/NVM-Express-Key-Value-Command-Set-Specification-1.0b-2021.12.18-Ratified.pdf).

| NVMe Completion Field | Description
| :-------------------: | :----------
| `dword0`              | total number of keys found

### NVME_CMD_KV_DELETE ###

`NVME_CMD_KV_DELETE` will delete the object associated with the passed key from the device.

#### Command Inputs ####

NVMe Command Field | Description
:----------------: | :----------
`opcode`           | `0x10`
`dword2`           | key (bytes 12-15)
`dword3`           | key (bytes 8-11)
`dword11`          | key length
`dword14`          | key (bytes 4-7)
`dword15`          | key (bytes 0-3)

#### Completion ####

Upon completion of the command, one of the following status values is returned:

Status Code | Definition
:---------: | :---------
`0x00`      | success
`0x86`      | invalid key size
`0x87`      | key not found

### NVME_CMD_KV_EXIST ###

`NVME_CMD_KV_EXIST` checks if object associated w2ith passed key exists in device.

#### Command Inputs ####

NVMe Command Field | Description
:----------------: | :----------
`opcode`           | `0x14`
`dword2`           | key (bytes 12-15)
`dword3`           | key (bytes 8-11)
`dword11`          | key length
`dword14`          | key (bytes 4-7)
`dword15`          | key (bytes 0-3)

#### Completion ####

Upon completion of the command, one of the following status values is returned:

Status Code | Definition
:---------: | :---------
`0x00`      | success
`0x86`      | invalid key size
`0x87`      | key not found

### NVME_CMD_KV_STORE ###

`NVME_CMD_KV_STORE` stores an object in the device and associates it with the given key.

#### Command Inputs ####

NVMe Command Field | Description
:----------------: | :----------
`opcode`           | `0x81`
`dword2`           | key (bytes 12-15)
`dword3`           | key (bytes 8-11)
`dword10`          | size of host buffer
`dword11`          | key length and flags (see below)
`dword14`          | key (bytes 4-7)
`dword15`          | key (bytes 0-3)
`DPTR`             | address of host buffer containing object

##### Store options #####

The following two byte option values are passed as the highest two bytes of `dword11`:

Value | Definition
:---: | :---------
`0x01`| Object for key must already exist (update only)
`0x02`| Object for key must not exist (create only)
`0x08`| Append data to existing object rather than overwriting

#### Completion ####

Upon completion of the command, one of the following status values is returned:

Status Code | Definition
:---------: | :---------
`0x00`      | success
`0x86`      | invalid key size
`0x87`      | key not found (if option flag `0x01` set)
`0x89`      | key exists (if option `0x02` set)

### NVME_CMD_KV_RETRIEVE ###

`NVME_CMD_KV_RETRIEVE` retrieves the contents of the object associated with key.

#### Command Inputs ####

NVMe Command Field | Description
:----------------: | :----------
`opcode`           | `0x82`
`dword2`           | key (bytes 12-15)
`dword3`           | key (bytes 8-11)
`dword10`          | size of host buffer
`dword11`          | key length
`dword14`          | key (bytes 4-7)
`dword15`          | key (bytes 0-3)
`DPTR`             | address of host buffer to receive object

#### Completion ####

Upon completion of the command, one of the following status values is returned:

Status Code | Definition
:---------: | :---------
`0x00`      | success
`0x86`      | invalid key size
`0x87`      | key not found

On success, the object is copied to the passed buffer and the size of the
original object is passed in `dword0` of the command completion entry.
If object is larger than the buffer, the copied object will be truncated, but
no error is reported.

### NVME_CMD_KV_SEND_SELECT ###

### NVME_CMD_KV_RETRIEVE_SELECT ###

## References ##

- [NVM Express Base Specification (v1.4c)]( https://nvmexpress.org/wp-content/uploads/NVM-Express-1_4c-2021.06.28-Ratified.pdf)
- [NVM Express Key Value Command Set Specification (v1.0b)](https://nvmexpress.org/wp-content/uploads/NVM-Express-Key-Value-Command-Set-Specification-1.0b-2021.12.18-Ratified.pdf)
- [NVM Command Set Specification (v1.0b)](https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-1.0b-2021.12.18-Ratified.pdf)

<!-- markdownlint-disable-file MD024 -->