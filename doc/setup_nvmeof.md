# How to Set Up a QEMU Development Environment to Use NVMe-oF

This document describes how to set up two Ubuntu Linux VMs with QEMU that are able to communicate with each other using NVMe over Fabrics (NVMe-oF). One VM is the initiator and the other is the target. The target has the NVMe drive and the initiator connects to it over the network TCP so it can use the target's NVMe drive.

We'll use SPDK running on the target. The initiator can use either the NVMe Linux driver or SPDK.

## Table of Contents

- [Note about transports](#note-about-transports)
- [Part 1: Create a second virtual machine](#part-1-create-a-second-virtual-machine)
  - [1. Copy files for the new VM](#1-copy-files-for-the-new-vm)
  - [2. Start both VMs](#2-start-both-vms)
- [Part 2: Setup the Target VM](#part-2-setup-the-target-vm)
  - [1. Run the SPDK setup script](#1-run-the-spdk-setup-script)
  - [2. Get the IP address of the VM](#2-get-the-ip-address-of-the-vm)
  - [3. Create a configuration file for SPDK](#3-create-a-configuration-file-for-spdk)
  - [4. Start the target process](#4-start-the-target-process)
- [Part 3: Setup the Initiator VM](#part-3-setup-the-initiator-vm)
- [Part 4: Try out some commands](#part-4-try-out-some-commands)
  - [Method 1: Using `nvme-cli`](#method-1-using-nvme-cli)
  - [Method 2: Using SPDK on the Initiator](#method-2-using-spdk-on-the-initiator)

## Note about transports

For NVMe-oF, SPDK supports either TCP or RDMA (remote direct memory access). If you'd like to use RDMA, the SPDK configure commands must be run with `--with-rdma` before building the code. The host must also support RDMA.

For Mac, it did not support it so the setup here uses TCP.

## Part 1: Create a second virtual machine

You'll need an existing virtual machine for this step. See [How to Set Up a QEMU Development Environment for NVMe Emulation](setup_qemu_nvme.md) to create and configure the first virtual machine. You'll also need to install SPDK.

### 1. Copy files for the new VM

In the `build` directory, copy the following files:

```bash
cp ubuntu-desktop.qcow ubuntu-desktop2.qcow
cp nvm.qcow nvm2.qcow
cp startup.sh startup2.sh
```

We'll also modify the startup script from the linked document. Replace the contents of `startup.sh` with:

```bash
export KV_BASE_DIR=./kv1

./qemu-system-x86_64 \
    -M q35 \
    -smp 4 \
    -m 8G \
    -mem-prealloc \
    -drive file=nvm.qcow,if=none,id=nvm \
    -drive file=ubuntu-desktop.qcow,if=virtio \
    -device nvme,serial=deadbeef,drive=nvm \
    -cpu host,phys-bits=39 \
    -machine pc-q35-3.1 \
    -accel hvf \
    -net user,hostfwd=tcp::2222-:22,hostfwd=tcp::4444-:4444 \
    -net nic
```

Replace the contents of the new start up script, `startup2.sh`, with:

```bash
export KV_BASE_DIR=./kv2

./qemu-system-x86_64 \
    -M q35 \
    -smp 4 \
    -m 8G \
    -mem-prealloc \
    -drive file=nvm2.qcow,if=none,id=nvm \
    -drive file=ubuntu-desktop2.qcow,if=virtio \
    -device nvme,serial=deadbeef,drive=nvm \
    -cpu host,phys-bits=39 \
    -machine pc-q35-3.1 \
    -accel hvf \
    -net user,hostfwd=tcp::2223-:22 \
    -net nic
```

Create directories for the KV store:

```bash
mkdir kv1 kv2
```

### 2. Start both VMs

You can start each VM using its startup script:

```bash
./startup.sh
./startup2.sh
```

You'll have to launch each in a separate terminal window.

After starting them, you'll be able to SSH into the VMs using:

```bash
ssh -p 2222 username@localhost
ssh -p 2223 username@localhost
```

## Part 2: Setup the Target VM

The first VM (accessible via SSH on port `2222`) will act as the NVMe-oF target.

### 1. Run the SPDK setup script

Run `spdk-lan/scripts/setup.sh` to get the `traddr` of the NVMe drive. More information about this script and how to configure SPDK is available in [How to Set Up a QEMU Development Environment for NVMe Emulation](setup_qemu_nvme.md).

Note the `traddr` of the NVMe drive in the output as we'll use it in the next steps.

### 2. Get the IP address of the VM

Run the following command to find the IP address:

```bash
ip addr
```

The output of the command will look something like this:

```text
2: enp0s2: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
    link/ether 52:54:00:12:34:56 brd ff:ff:ff:ff:ff:ff
    inet 10.0.2.15/24 brd 10.0.2.255 scope global dynamic noprefixroute enp0s2
```

In this case, `10.0.2.15` is the IP address. We'll use this in the next step.

### 3. Create a configuration file for SPDK

Write the following to a file called `config.json`:

```json
{
    "subsystems": [
        {
            "subsystem": "bdev",
            "config": [
                {
                    "params": {
                        "retry_count": 4,
                        "timeout_us": 0,
                        "nvme_adminq_poll_period_us": 100000,
                        "nvme_ioq_poll_period_us": 0,
                        "action_on_timeout": "none"
                    },
                    "method": "bdev_nvme_set_options"
                },
                {
                    "method": "bdev_nvme_attach_controller",
                    "params": {
                        "name": "Nvme1",
                        "trtype": "PCIe",
                        "traddr": "00:03.0"
                    }
                }
            ]
        },
        {
            "subsystem": "nvmf",
            "config": [
                {
                    "method": "nvmf_create_transport",
                    "params": {
                        "trtype": "TCP"
                    }
                },
                {
                    "params": {
                        "max_namespaces": 20,
                        "allow_any_host": true,
                        "serial_number": "SPDK00000000000001",
                        "model_number": "SPDK_Controller1",
                        "nqn": "nqn.2016-06.io.spdk:cnode1"
                    },
                    "method": "nvmf_create_subsystem"
                },
                {
                    "method": "nvmf_subsystem_add_listener",
                    "params": {
                        "nqn": "nqn.2016-06.io.spdk:cnode1",
                        "listen_address": {
                            "trtype": "TCP",
                            "traddr": "10.0.2.15",
                            "adrfam": "IPv4",
                            "trsvcid": "4444"
                        }
                    }
                },
                {
                    "params": {
                        "host": "nqn.2016-06.io.spdk:init",
                        "nqn": "nqn.2016-06.io.spdk:cnode1"
                    },
                    "method": "nvmf_subsystem_add_host"
                },
                {
                    "params": {
                        "namespace": {
                            "bdev_name": "Nvme1n1",
                            "nsid": 1
                        },
                        "nqn": "nqn.2016-06.io.spdk:cnode1"
                    },
                    "method": "nvmf_subsystem_add_ns"
                }
            ]
        }
    ]
}
```

Replace `10.0.2.15` with your own IP address under `nvmef`:

```json
{
    "method": "nvmf_subsystem_add_listener",
    "params": {
        "nqn": "nqn.2016-06.io.spdk:cnode1",
        "listen_address": {
            "trtype": "TCP",
            "traddr": "10.0.2.15",
            "adrfam": "IPv4",
            "trsvcid": "4444"
        }
    }
}
```

Replace the `traddr` of the drive under `bdev` from the previous steps:

```json
{
    "method": "bdev_nvme_attach_controller",
    "params": {
        "name": "Nvme1",
        "trtype": "PCIe",
        "traddr": "00:03.0"
    }
}
```

### 4. Start the target process

Run the following command to start the process that handles connections from the initiator:

```bash
build/bin/nvmf_tgt -m 0x1 -p 0 -r /var/run/spdk.sock -c /path/config.json
```

Make sure `-c` points to the path of the `config.json` file you created in the previous step.

## Part 3: Setup the Initiator VM

The second VM (SSH port `2223`) will be the NVMe-oF initiator and will connect to the NVMe drive of the first VM.

### Get the IP address of the host

The host running QEMU will forward traffic to the other VM. You can find the host's IP address by running the following command in the SSH session:

```bash
w
```

This will output something like:

```text
    16:25:51 up  3:28,  3 users,  load average: 2.34, 2.43, 4.56
USER     TTY      FROM             LOGIN@   IDLE   JCPU   PCPU WHAT
cwu      tty2     tty2             12:57    3:28m  0.05s  0.05s /usr/libexec/gnome-session-binary --session=ubuntu
cwu      pts/0    10.0.2.2         13:15   44.00s  0.09s  0.03s sshd: cwu [priv]
```

In this example, the IP address is `10.0.2.2`.

### Test the connection

Use the host IP and `nvme-cli` to test connecting to the other VM:

```bash
nvme discover -t tcp -a 10.0.2.2 -s 4444
```

The command should output:

```text
Discovery Log Number of Records 1, Generation counter 2
=====Discovery Log Entry 0======
trtype:  tcp
adrfam:  ipv4
subtype: nvme subsystem
treq:    not required
portid:  0  
trsvcid: 4444
subnqn:  nqn.2016-06.io.spdk:cnode1
traddr:  10.0.2.15
sectype: none
```

## Part 4: Try out some commands

You can either use SPDK which runs in user space to send commands from the initiator to the target or you can use `nvme-cli` which uses Linux kernel driver.

### Method 1: Using `nvme-cli`

#### Connect

```bash
nvme connect -t tcp -n nqn.2016-06.io.spdk:cnode1 -a 10.0.2.2 -s 4444 --keep-alive-tmo=300
```

#### List devices

```bash
nvme list
```

Output:

```text
Node                  SN                   Model                                    Namespace Usage                      Format           FW Rev
--------------------- -------------------- ---------------------------------------- --------- -------------------------- ---------------- --------
/dev/nvme0n1          deadbeef             QEMU NVMe Ctrl                           1           1.07  GB /   1.07  GB    512   B +  0 B   7.2.2
/dev/nvme1n1          SPDK00000000000001   SPDK_Controller1                         1           1.07  GB /   1.07  GB    512   B +  0 B   23.01.1
```

#### KV commands

Any of the KV commands, like this retrieve, will work:

```bash
nvme io-passthru /dev/nvme1 --opcode=0x82 --namespace-id=1 -cdw11 1 -cdw15 1711276032 -cdw10 100 -r -l 100
```

Output:

```text
IO Command Vendor Specific is Success and result: 0x00000064
```

### Method 2: Using SPDK on the Initiator

#### Create a configuration file

Create a `config.json` file with the following:

```json
{
    "subsystems": [
        {
            "subsystem": "bdev",
            "config": [
                {
                    "method": "bdev_nvme_attach_controller",
                    "params": {
                        "name": "Nvme1",
                        "trtype": "TCP",
                        "traddr": "10.0.2.2",
                        "trsvcid": "4444",
                        "adrfam": "IPv4",
                        "subnqn": "nqn.2016-06.io.spdk:cnode1"
                    }
                }
            ]
        }
    ]
}
```

Replace `10.0.2.2` with the IP address of the host.

#### Run the example

```bash
hello_kv_bdev -c config.json -b Nvme1n1
```

Learn more about running examples at [How to Set Up a QEMU Development Environment for NVMe Emulation](setup_qemu_nvme.md).
