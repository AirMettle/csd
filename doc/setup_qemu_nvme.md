# How to Set Up a QEMU Development Environment for NVMe Emulation

This page describes setting up the QEMU emulator and modifying the code for the NVMe emulation. We will use QEMU and its NVMe emulation so we can create new NVMe commands and have a modified SPDK and client use them.

## Table of Contents

- [First time setup](#first-time-setup)
  - [1. Clone the AirMettle QEMU repository](#1-clone-the-airmettle-qemu-repository)
  - [2. Install dependencies](#2-install-dependencies)
  - [3. Download an Ubuntu image](#3-download-an-ubuntu-image)
  - [4. Build QEMU](#4-build-qemu)
  - [5. Set up disk files for the guest](#5-set-up-disk-files-for-the-guest)
  - [6. Create a startup shortcut script](#6-create-a-startup-shortcut-script)
  - [7. Start the guest for the first time](#7-start-the-guest-for-the-first-time)
  - [8. Follow the on-screen instructions to install Ubuntu](#8-follow-the-on-screen-instructions-to-install-ubuntu)
  - [9. Make the guest boot from the new installation](#9-make-the-guest-boot-from-the-new-installation)
  - [10. Configure the guest](#10-configure-the-guest)
  - [11. Use SSH to connect to the guest](#11-use-ssh-to-connect-to-the-guest)
- [QEMU Development Workflow](#qemu-development-workflow)
- [SPDK Development](#spdk-development)
  - [Download and build SPDK from the repository](#download-and-build-spdk-from-the-repository)
  - [Trying out the examples](#trying-out-the-examples)
    - [1. Set up the NVMe drive](#1-set-up-the-nvme-drive)
    - [2. Create a configuration file](#2-create-a-configuration-file)
    - [3. Run the examples](#3-run-the-examples)
  - [HugePages troubleshooting](#hugepages-troubleshooting)

## First time setup

The commands in this guide were tested on these systems:

- Mac Intel laptop
- Ubuntu 22.04 server
- VirtualBox Linux VM (slowest)

Your CPU must support nested virtualization if you want to run the emulator in a VM. See [NestedKVM](https://wiki.qemu.org/Features/NestedKVM) for more information.

This guide is based on information from these sites:

- <https://blog.reds.ch/?p=1597>
- <https://graspingtech.com/ubuntu-desktop-18.04-virtual-machine-macos-qemu/>

### 1. Clone the AirMettle QEMU repository

Clone the repository and checkout the branch which has a modification to add a test NVMe admin command. I used the code from the <https://blog.reds.ch/?p=1597> blog. I made the changes off of the QEMU `stable-7.2` branch. It contains changes to `hw/nvme/ctrl.c` and `include/block/nvme.h` for the test admin command.

```bash
git checkout nvme_command
git pull origin nvme_command
```

### 2. Install dependencies

Mac:

```bash
brew install glib ninja pkgconfig pixman libslirp wget
```

Linux:

```bash
sudo apt update
sudo apt install -y libpixman-1-dev libslirp-dev
```

**Note that your system may require additional packages.** See [Hosts/Linux](https://wiki.qemu.org/Hosts/Linux) and [Hosts/Mac](https://wiki.qemu.org/Hosts/Mac) on the QEMU Wiki for a more complete list of build dependencies.

### 3. Download an Ubuntu image

Download the [Ubuntu 22.04](https://releases.ubuntu.com/22.04/) desktop installer image. This guide uses Ubuntu 22.04 as the guest OS but others may work.

```bash
wget -O 'https://releases.ubuntu.com/22.04/ubuntu-22.04.2-desktop-amd64.iso'
```

Remember to verify the checksum of the downloaded file as described on the download page.

Once downloaded, move the file to the `build` folder.

### 4. Build QEMU

Run the build script to configure and build QEMU:

```bash
./build.sh
cd build
```

You'll only have to run this script once for initial setup. After that, you can run `make` or `make -j8` from the `build` directory to build.

### 5. Set up disk files for the guest

Create a file that will house the guest OS:

```bash
./qemu-img create -f qcow2 ./ubuntu-desktop.qcow 50G
```

Create a file for the NVMe disk we will use to send commands to:

```bash
./qemu-img create -f qcow2 nvm.qcow 1G
```

### 6. Create a startup shortcut script

Create `startup.sh` file to startup QEMU that has the following:

```bash
./qemu-system-x86_64 \
    -M q35 \
    -smp 4 \
    -m 10G \
    -drive file=nvm.qcow,if=none,id=nvm \
    -drive file=ubuntu-desktop.qcow,if=virtio \
    -device nvme,serial=deadbeef,drive=nvm \
    -cpu host \
    -machine pc-q35-3.1 \
    -accel hvf \
    -net user,hostfwd=tcp::2222-:22 \
    -net nic \
    -cdrom ubuntu-22.04.2-desktop-amd64.iso
```

You can adjust `-smp` and `-m` for the number of CPU cores and memory you want the VM to use.

It is important to use the `-accel` and `-machine` flags so hardware acceleration is used. This makes the VM run much faster.

**If your machine does not support the `hvf` accelerator, you can try using `kvm` instead.** Replace `-accel hvf` with `-enable-kvm`. Be sure `kvm` is installed on your system.

### 7. Start the guest for the first time

```bash
chmod +x startup.sh
./startup.sh
```

After starting the script, a window should open that runs the emulator. On Mac, you can use <kbd>Ctrl</kbd> + <kbd>Option</kbd> + <kbd>G</kbd> to escape your mouse out of the window if needed.

### 8. Follow the on-screen instructions to install Ubuntu

- Choose the minimal installation
- Choose the 50G disk as the install target
- Choose a username and password

You can also refer to [these instructions](https://graspingtech.com/ubuntu-desktop-18.04-virtual-machine-macos-qemu/) for more information.

After installation, the emulator will restart the OS. Type <kbd>Ctrl</kbd> + <kbd>C</kbd> to kill the process in the shell where `startup.sh` is running.

### 9. Make the guest boot from the new installation

Once the guest is off, remove the `-cdrom` line from `startup.sh`. This will make the guest start from the disk file where it is installed rather than start the installer again.

The `startup.sh` script should now look like this:

```bash
./qemu-system-x86_64 \
      -M q35 \
      -smp 4 \
      -m 10G \
      -drive file=nvm.qcow,if=none,id=nvm \
      -drive file=ubuntu-desktop.qcow,if=virtio \
      -device nvme,serial=deadbeef,drive=nvm \
      -cpu host \
      -machine pc-q35-3.1 \
      -accel hvf \
      -net user,hostfwd=tcp::2222-:22 \
      -net nic
```

After you've made this change, run `startup.sh` to start the emulator again:

```bash
./startup.sh
```

### 10. Configure the guest

Open a terminal in the emulator desktop and start a root shell with `sudo bash`. Enter these commands in this shell:

- Install `ssh` and `nvme`:

    ```bash
    apt install openssh-server
    apt install nvme-cli
    ```

- (Optional) Turn off the clock seconds display to use less CPU:

    ```bash
    gsettings set org.gnome.desktop.interface clock-show-seconds false
    ```

    If this outputs `Fails to execute child process "dbus-launch" (no such file)`, you may need to install `dbus-x11`.

### 11. Use SSH to connect to the guest

After completing the previous steps, you can SSH directly into the guest over port `2222` and not use the UI. Use the username you chose during installation:

```bash
ssh -p 2222 username@localhost
```

## QEMU Development Workflow

For development and modification of QEMU the steps are:

1. Edit the QEMU source code to add your features

2. In the `build` directory, do

    ```bash
    make
    ./startup.sh
    ```

3. SSH into the VM and run your tests

    ```bash
    ssh -p 2222 username@localhost
    ```

4. Shutdown the VM and repeat from step 1 as you iterate

Restarting the emulator and having the operating system come up takes less than 30 seconds so it is not that slow.

Unit tests can also be added to `./tests/qtest/nvme-test.c` so some testing can be done locally without having to start the emulator each time.

## SPDK Development

In the Ubuntu VM that QEMU starts up, to develop and test SPDK, you need to do these steps to build our SPDK repository:

### Download and build SPDK from the repository

#### Install dependencies

```bash
sudo apt update
sudo apt install gcc pkg-config ninja-build python3-pip uuid-dev libaio-dev libncurses5-dev libncursesw5-dev libcunit1-dev
pip3 install pyelftools
```

#### Build SPDK

```bash
./configure
make
```

Note that the included examples are also built as part of the build process. The binaries are located in `build/examples`.

### Trying out the examples

#### 1. Set up the NVMe drive

To run the SPDK examples that talk to the QEMU NVMe emulator, run the `setup.sh` script:

```bash
cd scripts
sudo ./setup.sh
```

This sets up the NVMe drive and HugePages. It must be run every time you reboot the VM.

This script also outputs a `traddr` such as:

```text
0000:00:03.0 (8086 5845): nvme -> uio_pci_generic
```

Keep this for the next step.

#### 2. Create a configuration file

Create a `bdev.json` file with the following contents:

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
            "trtype": "PCIe",
            "traddr": "00:03.0"
          }
        }
      ]
    }
  ]
}
```

Replace the `traddr` with the one from `setup.sh` if it is different than `0000:00:03.0`.

#### 3. Run the examples

Run the examples located in the `build/examples` directory. Be sure to include the path to the configuration file you created with the `-c` option. The examples must be run as root.

##### Hello world example

```bash
sudo ./build/examples/hello_bdev -c ./examples/bdev/hello_world/bdev.json -b Nvme1n1
```

##### Hello world key-value example

```bash
sudo ./build/examples/hello_kv_bdev -c ./examples/bdev/hello_world_kv/bdev.json -b Nvme1n1
```

### HugePages troubleshooting

If you get a HugePages error, you can try to edit `/etc/default/grub` and set:

```ini
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash hugepagesz=2M hugepages=256"
```

Then run:

```bash
sudo update-grub
sudo reboot
```

Run the steps starting at running SPDK `setup.sh` again.
