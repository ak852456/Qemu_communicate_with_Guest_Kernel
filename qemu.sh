#!/bin/bash 
#qemu version 5.0.0

#install disk drive

sudo qemu-img create -f raw mini.img 20G	# put mini.img in QEMU top directory 

sudo qemu-system-x86_64 -m 1G -hda mini.img -cdrom mini.iso -boot d -enable-kvm	# install mini.iso on mini.img

# (compile QEMU)

cd qemu-5.0.0/

sudo ./configure --target-list=x86_64-softmmu --enable-debug

make -j 4

# run QEMU 

sudo qemu-system-x86_64 -m 1G -hda mini.img -cdrom mini.iso -enable-kvm # basic boot up

# create a shared memory file region with guest kerenl on /dev/shm/ivshemem
sudo x86_64-softmmu/qemu-system-x86_64 -cpu host -m 1G -hda mini.img -cdrom mini.iso -device ivshmem-plain,memdev=hostmem -object memory-backend-file,size=1M,share,mem-path=/dev/shm/ivshmem,id=hostmem -enable-kvm

(Prefered) sudo rm -rf /dev/shm/*

# (mount guest kernel image to mnt directory on host system)

sudo losetup /dev/loop0 mini.img 

sudo kpartx -a /dev/loop0

sudo mount /dev/mapper/loop0p1 /mnt/

sudo umount /mnt
