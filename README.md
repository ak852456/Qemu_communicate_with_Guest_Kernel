# Qemu_communicate_with_Guest_Kernel

Communicating QEMU with the running guest kernel.

Using QEMU ivshmem dev to create a shared memory file on host.

make kvm_ivshmem.c and insmod kvm_ivshmem.ko

using the pci device on guest kernel to simulate the created shered memory file. 

the running guest kernel version is "Ubuntu 14.04 LTS "Trusty Tahr".

https://help.ubuntu.com/community/Installation/MinimalCD

QEMU version is 5.0.0
