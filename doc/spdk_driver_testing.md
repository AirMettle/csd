
# Notes on doing testing of SPDK and NVMe device

Assumes QEMU is set up per [How to: Setup QEMU Development Environment for NVME emulation](setup_qemu_nvme.md)

# Using `nvme` CLI

## Installing

On Ubuntu install using:

```bash
sudo apt install nvme-cli
```

## Running `nvme`

- Needs root access, use `sudo`
- Use the command `io-passthru` and pass the linux device name of the NVMe device (eg. `/dev/nvme0`)
- Device cannot be in use by SPDK (run `spdk/scripts/setup.sh reset`)
- Add the `-s` option after the device to see the command being sent

## Understanding the return code

When the command returns an error, a 4 digit hex numeral is included in the output:

```text
NVMe status: Unknown(0x4088)
```

The error code can be translated as follows:

- The first digit (`4h`) is a do-not-retry flag
- The second digit is the status code type (`0h`) in this example
- The third and fourth digits are the status code itself (`88h`). This can be looked up against NVME error codes. (It corresponds to `NVME_KV_ERROR`)

## Running KV Commands

### KV List

| **Field**                        | **Value**                 |
| -------------------------------- | ------------------------- |
| Opcode                           | 0x06                      |
| dword10                          | Output buffer length      |
| dword11                          | Prefix search key length  |
| dword15, dword14, dword3, dword2 | Search key (16 bytes max) |

#### Example

Return keys starting with 'f' (0x66) into a 1000 byte buffer.

```bash
sudo nvme io-passthru /dev/nvme0 --opcode=0x06 --namespace-id=1 -cdw11 1 -cdw15 0x66000000 -cdw10 10000 -r -l 10000
```

### KV Store

| **Field**                        | **Value**           |
| -------------------------------- | ------------------- |
| Opcode                           | 0x81                |
| dword10                          | Input buffer length |
| dword11                          | Key length          |
| dword15, dword14, dword3, dword2 | Key (16 bytes max)  |

#### Example

Store contents of file as object named `test_key`:

```bash
FILE=/etc/hosts
LEN=$(stat -c "%s" $FILE)
sudo nvme io-passthru /dev/nvme0 --opcode=0x81 --namespace-id=1 -cdw11 8 -cdw15 0x74657374 -cdw14 0x5f6b6579 -cdw10 $LEN -i $FILE -w -l $LEN
```

### KV Retrieve

| **Field**                        | **Value**                 |
| -------------------------------- | ------------------------- |
| Opcode                           | 0x82                      |
| dword10                          | Input buffer length       |
| dword11                          | Key length                |
| dword12                          | Offset to start read from |
| dword15, dword14, dword3, dword2 | Key (16 bytes max)        |

#### Example

Store contents of file as object named `test_key`:

```bash
sudo nvme io-passthru /dev/nvme0 --opcode=0x82 --namespace-id=1 -cdw11 8 -cdw15 0x74657374 -cdw14 0x5f6b6579 -cdw10 1024 -r -l 1024
```
