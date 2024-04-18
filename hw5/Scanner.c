#include <linux/module.h>     // Needed by all modules
#include <linux/kernel.h>     // Needed for KERN_INFO
#include <linux/fs.h>         // Needed for file_operations
#include <linux/slab.h>       // Needed for kmalloc and kfree
#include <linux/uaccess.h>    // Needed for copy_to_user and copy_from_user
#include <linux/types.h>      // Needed for specific types like ssize_t, loff_t
#include <linux/init.h>       // Needed for the macros __init and __exit
#include <linux/device.h>     // Needed for device_create, class_create etc.

#define DEVICE_NAME "scanner"
#define CLASS_NAME "scanner_class"

typedef struct scanner_device {
    char *data_buffer;      // Buffer to store user input
    size_t buffer_size;     // Size of the current buffer
    size_t buffer_position; // Current position in the buffer
    char separators[10];    // Separator characters
} ScannerDevice;

static int majorNumber;
static struct class* scannerClass = NULL;
static struct device* scannerDevice = NULL;

// Function prototypes
static int dev_open(struct inode *inodep, struct file *filep);
static int dev_release(struct inode *inodep, struct file *filep);
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset);
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset);
static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

static struct file_operations fops = {
        .owner = THIS_MODULE,
        .open = dev_open,
        .release = dev_release,
        .read = dev_read,
        .write = dev_write,
        .unlocked_ioctl = dev_ioctl,
};

static int __init scanner_init(void) {
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        printk(KERN_ALERT "Scanner failed to register a major number\n");
        return majorNumber;
    }
    scannerClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(scannerClass)) {
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(scannerClass);
    }
    scannerDevice = device_create(scannerClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(scannerDevice)) {
        class_destroy(scannerClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(scannerDevice);
    }
    printk(KERN_INFO "Scanner: device class created correctly\n");
    return 0;
}

static void __exit scanner_exit(void) {
    device_destroy(scannerClass, MKDEV(majorNumber, 0));
    class_destroy(scannerClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);
    printk(KERN_INFO "Scanner: Goodbye from the LKM!\n");
}

static int dev_open(struct inode *inodep, struct file *filep) {
    ScannerDevice *dev = kmalloc(sizeof(ScannerDevice), GFP_KERNEL);
    if (!dev) {
        printk(KERN_ALERT "Scanner: Failed to allocate memory for device state\n");
        return -ENOMEM;
    }
    dev->data_buffer = NULL;
    dev->buffer_size = 0;
    dev->buffer_position = 0;
    memset(dev->separators, 0, sizeof(dev->separators));
    strcpy(dev->separators, " ");  // Default separator is space
    filep->private_data = dev;
    printk(KERN_INFO "Scanner: Device has been opened\n");
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    struct scanner_device *dev = filep->private_data;
    ssize_t bytes_read = 0;

    // Acquire the lock for this scanner device
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // If we're at the end of the buffer or there's no data to read, release the lock and return 0
    if (dev->buffer_position >= dev->buffer_size || !dev->data_buffer) {
        mutex_unlock(&dev->lock);
        return 0; // End of data
    }

    // Tokenize the input data and copy to user buffer
    while (dev->buffer_position < dev->buffer_size && bytes_read < len) {
        char current_char = dev->data_buffer[dev->buffer_position];

        // Check if the current character is a separator
        if (strchr(dev->separators, current_char)) {
            dev->buffer_position++; // Skip the separator

            // If we've read at least one character, break (end of token)
            if (bytes_read > 0) break;

            // Otherwise, continue reading (consecutive separators)
            continue;
        }

        // Copy the current character to the user buffer
        if (copy_to_user(&buffer[bytes_read], &current_char, 1)) {
            mutex_unlock(&dev->lock);
            return -EFAULT;
        }

        // Increment counters
        bytes_read++;
        dev->buffer_position++;
    }

    // Release the lock
    mutex_unlock(&dev->lock);

    // Return the number of bytes read
    return bytes_read;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    struct scanner_device *dev = filep->private_data;
    ssize_t bytes_written = 0;

    // Acquire the lock for this scanner device
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // Free the old buffer if it exists
    if (dev->data_buffer) {
        kfree(dev->data_buffer);
        dev->data_buffer = NULL;
        dev->buffer_size = 0;
        dev->buffer_position = 0;
    }

    // Allocate a new buffer for the data
    dev->data_buffer = kmalloc(len, GFP_KERNEL);
    if (!dev->data_buffer) {
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }

    // Copy the data from the user to the new buffer
    if (copy_from_user(dev->data_buffer, buffer, len)) {
        kfree(dev->data_buffer);
        dev->data_buffer = NULL;
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }

    // Update buffer size and reset position
    dev->buffer_size = len;
    dev->buffer_position = 0;
    bytes_written = len;

    // Release the lock
    mutex_unlock(&dev->lock);

    // Return the number of bytes written
    return bytes_written;
}

static int dev_release(struct inode *inodep, struct file *filep) {
    ScannerDevice *dev = filep->private_data;
    if (dev->data_buffer)
        kfree(dev->data_buffer);
    kfree(dev);
    printk(KERN_INFO "Scanner: Device successfully closed\n");
    return 0;
}

static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
    // dev_ioctl implementation...
}

module_init(scanner_init);
module_exit(scanner_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A Linux character device driver for a scanner");
MODULE_VERSION("0.1");

//
//#include <linux/module.h>  // Needed by all modules
//#include <linux/kernel.h>  // Needed for KERN_INFO
//#include <linux/fs.h>      // Needed for file_operations
//#include <fcntl.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <string.h>
//
//#define DEVICE_NAME "scanner"
//#define CLASS_NAME "scanner_class"
//
//typedef struct scanner_device {
//    char *data_buffer;      // Buffer to store user input
//    size_t buffer_size;     // Size of the current buffer
//    size_t buffer_position; // Current position in the buffer
//    char separators[10];    // Separator characters
//} ScannerDevice;
//
//static int majorNumber;
//static struct class* scannerClass = NULL;
//static struct device* scannerDevice = NULL;
//
//// Function prototypes
//static int dev_open(struct inode *inodep, struct file *filep);
//static int dev_release(struct inode *inodep, struct file *filep);
//static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset);
//static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset);
//static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
//
//static struct file_operations {
//        .owner = THIS_MODULE,
//        .open = dev_open,
//        .release = dev_release,
//        .read = dev_read,
//        .write = dev_write,
//        .unlocked_ioctl = dev_ioctl,
//}file;
//
//static int __init scanner_init(void) {
//    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
//    if (majorNumber < 0) {
//        printk(KERN_ALERT "Scanner failed to register a major number\n");
//        return majorNumber;
//    }
//    scannerClass = class_create(THIS_MODULE, CLASS_NAME);
//    if (IS_ERR(scannerClass)) {
//        unregister_chrdev(majorNumber, DEVICE_NAME);
//        printk(KERN_ALERT "Failed to register device class\n");
//        return PTR_ERR(scannerClass);
//    }
//    scannerDevice = device_create(scannerClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
//    if (IS_ERR(scannerDevice)) {
//        class_destroy(scannerClass);
//        unregister_chrdev(majorNumber, DEVICE_NAME);
//        printk(KERN_ALERT "Failed to create the device\n");
//        return PTR_ERR(scannerDevice);
//    }
//    printk(KERN_INFO "Scanner: device class created correctly\n");
//    return 0;
//}
//
//static void __exit scanner_exit(void) {
//    device_destroy(scannerClass, MKDEV(majorNumber, 0));
//    class_destroy(scannerClass);
//    unregister_chrdev(majorNumber, DEVICE_NAME);
//    printk(KERN_INFO "Scanner: Goodbye from the LKM!\n");
//}
//
//static int dev_open(struct inode *inodep, struct file *filep) {
//    ScannerDevice *dev = kmalloc(sizeof(ScannerDevice), GFP_KERNEL);
//    if (!dev) {
//        printk(KERN_ALERT "Scanner: Failed to allocate memory for device state\n");
//        return -ENOMEM;
//    }
//    dev->data_buffer = NULL;
//    dev->buffer_size = 0;
//    dev->buffer_position = 0;
//    memset(dev->separators, 0, sizeof(dev->separators));
//    strcpy(dev->separators, " ");  // Default separator is space
//    filep->private_data = dev;
//    printk(KERN_INFO "Scanner: Device has been opened\n");
//    return 0;
//}
//
//static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
//    struct scanner_device *dev = filep->private_data;
//    ssize_t bytes_read = 0;
//
//    // Acquire the lock for this scanner device
//    if (mutex_lock_interruptible(&dev->lock)) {
//        return -ERESTARTSYS;
//    }
//
//    // If we're at the end of the buffer or there's no data to read, release the lock and return 0
//    if (dev->buffer_position >= dev->buffer_size || !dev->data_buffer) {
//        mutex_unlock(&dev->lock);
//        return 0; // End of data
//    }
//
//    // Tokenize the input data and copy to user buffer
//    while (dev->buffer_position < dev->buffer_size && bytes_read < len) {
//        char current_char = dev->data_buffer[dev->buffer_position];
//
//        // Check if the current character is a separator
//        if (strchr(dev->separators, current_char)) {
//            dev->buffer_position++; // Skip the separator
//
//            // If we've read at least one character, break (end of token)
//            if (bytes_read > 0) break;
//
//            // Otherwise, continue reading (consecutive separators)
//            continue;
//        }
//
//        // Copy the current character to the user buffer
//        if (copy_to_user(&buffer[bytes_read], &current_char, 1)) {
//            mutex_unlock(&dev->lock);
//            return -EFAULT;
//        }
//
//        // Increment counters
//        bytes_read++;
//        dev->buffer_position++;
//    }
//
//    // Release the lock
//    mutex_unlock(&dev->lock);
//
//    // Return the number of bytes read
//    return bytes_read;
//}
//
//static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
//    struct scanner_device *dev = filep->private_data;
//    ssize_t bytes_written = 0;
//
//    // Acquire the lock for this scanner device
//    if (mutex_lock_interruptible(&dev->lock)) {
//        return -ERESTARTSYS;
//    }
//
//    // Free the old buffer if it exists
//    if (dev->data_buffer) {
//        kfree(dev->data_buffer);
//        dev->data_buffer = NULL;
//        dev->buffer_size = 0;
//        dev->buffer_position = 0;
//    }
//
//    // Allocate a new buffer for the data
//    dev->data_buffer = kmalloc(len, GFP_KERNEL);
//    if (!dev->data_buffer) {
//        mutex_unlock(&dev->lock);
//        return -ENOMEM;
//    }
//
//    // Copy the data from the user to the new buffer
//    if (copy_from_user(dev->data_buffer, buffer, len)) {
//        kfree(dev->data_buffer);
//        dev->data_buffer = NULL;
//        mutex_unlock(&dev->lock);
//        return -EFAULT;
//    }
//
//    // Update buffer size and reset position
//    dev->buffer_size = len;
//    dev->buffer_position = 0;
//    bytes_written = len;
//
//    // Release the lock
//    mutex_unlock(&dev->lock);
//
//    // Return the number of bytes written
//    return bytes_written;
//}
//
//static int dev_release(struct inode *inodep, struct file *filep) {
//    ScannerDevice *dev = filep->private_data;
//
//    // Free the data buffer if it exists
//    if (dev->data_buffer)
//        kfree(dev->data_buffer);
//
//    // Destroy the mutex and free the device state
//    mutex_destroy(&dev->lock);
//    kfree(dev);
//
//    printk(KERN_INFO, "Scanner: Device successfully closed\n");
//    return 0;
//}
//
//static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
//    // Implementation needed
//    return 0;
//}
//
//module_init(scanner_init);
//module_exit(scanner_exit);
//
