# Data analytics at the storage layer using an NVMe object-based CSD

The NVMe-Object SSD computational storage device (CSD) introduces key-value (KV) focused storage and processing capabilities using an emulated NVMe device.

This repository contains modifications to QEMU and SPDK to support the CSD.

## Documentation

### Design documents

- [Project Overview and Diagram](/doc/design_overview.md)
- [SPDK Device Driver](/doc/design_spdk_driver.md)
- [KV Store Functions and Query Engine in QEMU](/doc/design_kv_store_functions_query_engine_qemu.md)

### How-to Guides

- [How to set up a QEMU development environment for NVMe emulation](/doc/setup_qemu_nvme.md)
- [How to Set Up a QEMU Development Environment to Use NVMe-oF](/doc/setup_nvmeof.md)
- [KV List Commands in QEMU NVMe Emulator](/doc/kv_list_command_reference.md)
- [SPDK device driver testing](/doc/spdk_driver_testing.md)

## License

See [LICENSE](/LICENSE) for more information.
