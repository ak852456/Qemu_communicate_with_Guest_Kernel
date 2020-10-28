#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/fs.h>

enum {
	/* KVM Inter-VM shared memory device register offsets */
	IntrMask        = 0x00,    /* Interrupt Mask */
	IntrStatus      = 0x04,    /* Interrupt Status */
	IVPosition      = 0x08,    /* VM ID */
	Doorbell        = 0x0c,    /* Doorbell */
};

typedef struct kvm_ivshmem_device {
	void __iomem *regs;
	void __iomem *base_addr;                                // the mapping virtual address

    unsigned long ioaddr;                                   // the base address register(pci device bus address)
	unsigned long ioaddr_size;
	unsigned long ioaddr_flags;

	unsigned long regaddr;
	unsigned long reg_size;

	struct pci_dev *dev;

}kvm_ivshmem_device;

static kvm_ivshmem_device kvm_ivshmem_dev;
static int major_number = 0, minor_number = 0;
static dev_t devno;  
static struct cdev *cdev;
static struct class *cl; 
static struct device *dev;

static struct pci_device_id kvm_ivshmem_id_table[] = {
	{ 0x1af4, 0x1110, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },    // "qemu-5.0.0/docs/spec/ivshmem-spec.txt"
	{ 0 },                                                  // (vendor, device) (subvendor, subdevice) (class, class_mask, prog-if) (private_data)
};
MODULE_DEVICE_TABLE(pci, kvm_ivshmem_id_table);

static int kvm_ivshmem_open(struct inode *inode, struct file *filp)
{
	printk("Opening kvm_ivshmem device\n");

   	if(MINOR(inode->i_rdev) != minor_number)
    {
	    printk("minor number is %d\n", minor_number);
	  	return -ENODEV;
  	}

  	return 0;
}

static int kvm_ivshmem_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t kvm_ivshmem_read(struct file *filp, char *buffer, size_t len, loff_t *poffset)
{	
	int bytes_read = 0;
	unsigned long offset = *poffset;

	if(!kvm_ivshmem_dev.base_addr)
    {
		printk(KERN_ERR "KVM_IVSHMEM: Cannot read from ioaddr (NULL)\n");
		return 0;
	}

	if(len > kvm_ivshmem_dev.ioaddr_size - offset)
		len = kvm_ivshmem_dev.ioaddr_size - offset;
	
	if(len == 0)
		return 0;

	bytes_read = copy_to_user(buffer, kvm_ivshmem_dev.base_addr + offset, len);
	if(bytes_read > 0)
    {
		return -EFAULT;
	}

	*poffset += len;
	return len;
}

static ssize_t kvm_ivshmem_write(struct file *filp, const char *buffer, size_t len, loff_t *poffset)
{
	int bytes_written = 0;
	unsigned long offset = *poffset;

	if(!kvm_ivshmem_dev.base_addr)
    {
		printk(KERN_ERR "KVM_IVSHMEM: Cannot write to ioaddr (NULL)\n");
		return 0;
	}

	if(len > kvm_ivshmem_dev.ioaddr_size - offset) 
		len = kvm_ivshmem_dev.ioaddr_size - offset;

	if(len == 0) 
		return 0;

	bytes_written = copy_from_user(kvm_ivshmem_dev.base_addr + offset, buffer, len);
	if(bytes_written > 0)
    {
		return -EFAULT;
	}

	*poffset += len;
	return len;
}

static const struct file_operations kvm_ivshmem_ops = {
	.owner   = THIS_MODULE,
	.open	= kvm_ivshmem_open,
	.release = kvm_ivshmem_release,
	.read	= kvm_ivshmem_read,
	.write   = kvm_ivshmem_write,
};

static int kvm_ivshmem_probe_device(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
    printk("KVM_IVSHMEM: Probing for KVM_IVSHMEM Device\n");

	ret = pci_enable_device(pdev);                                                                      // wake up the pci device
	if(ret)
    {
		printk(KERN_ERR "Unable to probe KVM_IVSHMEM device %s: error %d\n", pci_name(pdev), ret);
		return ret;
	}

	ret = pci_request_regions(pdev, "kvm_ivshmem");                                                     // mark the allocated pci region
	if(ret < 0)
    {
		printk(KERN_ERR "KVM_IVSHMEM: Cannot request regions\n");
		goto pci_disable;
	} 
	
	else 
		printk(KERN_ERR "KVM_IVSHMEM: Result is %d\n", ret);

	kvm_ivshmem_dev.ioaddr = pci_resource_start(pdev, 2);                                               // BAR2 maps the shared memory object
	kvm_ivshmem_dev.ioaddr_size = pci_resource_len(pdev, 2);
	kvm_ivshmem_dev.ioaddr_flags = pci_resource_flags(pdev, 2);

	kvm_ivshmem_dev.base_addr = pci_iomap(pdev, 2, 0);                                                  // get the virtual address, and it is an abstaction layer of mmio and pio
	printk("KVM_IVSHMEM: iomap base = 0x%lu \n", (unsigned long)kvm_ivshmem_dev.base_addr);

	if(!kvm_ivshmem_dev.base_addr)
    {
		printk(KERN_ERR "KVM_IVSHMEM: Unable to iomap region of size %lu\n", kvm_ivshmem_dev.ioaddr_size);
		goto pci_release;
	}

	printk("KVM_IVSHMEM: ioaddr = %lu ioaddr_size = %lu\n", kvm_ivshmem_dev.ioaddr, kvm_ivshmem_dev.ioaddr_size);

	kvm_ivshmem_dev.regaddr =  pci_resource_start(pdev, 0);                                             // BAR0 holds device registers (256 Byte MMIO)
	kvm_ivshmem_dev.reg_size = pci_resource_len(pdev, 0);
	kvm_ivshmem_dev.regs = pci_iomap(pdev, 0, 0x100);
	kvm_ivshmem_dev.dev = pdev;

	if(!kvm_ivshmem_dev.regs)
    {
		printk(KERN_ERR "KVM_IVSHMEM: Cannot ioremap registers of size %lu\n", kvm_ivshmem_dev.reg_size);
		goto reg_release;
	}

	printk("KVM_IVSHMEM: Finishing probing for KVM_IVSHMEM device\n");
	return 0;

reg_release:
	pci_iounmap(pdev, kvm_ivshmem_dev.base_addr);

pci_release:
	pci_release_regions(pdev);

pci_disable:
	pci_disable_device(pdev);
	return -EBUSY;
}

static void kvm_ivshmem_remove_device(struct pci_dev *pdev)
{
	printk("Unregister kvm_ivshmem device.\n");

	pci_iounmap(pdev, kvm_ivshmem_dev.regs);
	pci_iounmap(pdev, kvm_ivshmem_dev.base_addr);
	
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver kvm_ivshmem_pci_driver = {
	.name		= "kvm-shmem",
	.id_table	= kvm_ivshmem_id_table,
	.probe	   = kvm_ivshmem_probe_device,
	.remove	  = kvm_ivshmem_remove_device,
};

static int __init kvm_ivshmem_init_module(void)
{
    int ret;
    
    ret = alloc_chrdev_region(&devno, minor_number, 1, "kvm_ivshmem");
    if(ret < 0)
    {
        printk(KERN_ERR "KVM_IVSHMEM: Unable to register kvm_ivshmem device!!!\n");
		return ret;
    }

    major_number = MAJOR(devno);    
    minor_number = MINOR(devno);    
    printk("KVM_IVSHMEM: Major device number is: %d\n", major_number);

    cdev = cdev_alloc();
    cdev_init(cdev, &kvm_ivshmem_ops);                                                            
    cdev_add(cdev, devno, 1);   
	
    ret = pci_register_driver(&kvm_ivshmem_pci_driver);
	if(ret < 0) 
		goto error;
	
	else
		printk("KVM_IVSHMEM: Success to register pci driver\n");

	cl = class_create(THIS_MODULE, "ivshmem-dev");  
    if(cl == NULL)
    {
     	printk(KERN_ERR "KVM_IVSHMEM: Unable to create dev_class!!!\n");
       	goto class_err;
 	}

	else
		printk("KVM_IVSHMEM: Success to create dev_class\n");

	dev = device_create(cl, NULL, devno, NULL, "ivshmem");  
    if(dev == NULL)
    {
        printk(KERN_ERR "KVM_IVSHMEM: Unable to create device!!!\n");
        return -1;
    }

	return 0;

class_err:
    kfree(cl);

error:
    cdev_del(cdev);
    unregister_chrdev_region(devno, 1);
	return ret;
}

static void __exit kvm_ivshmem_exit_module(void)
{
	pci_unregister_driver(&kvm_ivshmem_pci_driver);
	device_destroy(cl, devno);
    class_destroy(cl);   
    cdev_del(cdev);
    unregister_chrdev_region(devno, 1);
}

module_init(kvm_ivshmem_init_module);
module_exit(kvm_ivshmem_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LEE");
