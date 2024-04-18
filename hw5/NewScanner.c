#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include "NewScanner.h"

#include <linux/ioctl.h>

#define DEVNAME "scanner_device"
#define CLASS_NAME "scanner_class"

#define SCANNER_MAGIC 'q'


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Scanner Driver");
MODULE_AUTHOR("<abbiesarmento@u.boisestate.edu>");

typedef struct {
    dev_t devno;
    struct cdev cdev;
    char *separators; // Separators used for tokenization
    char *data;       // Data to be tokenized
} ScannerDevice;

static ScannerDevice scanner_device;

typedef struct {
    char *current_token;
    char *next_char;
    char *separators;
} ScannerFile;

static int scanner_open(struct inode *inode, struct file *filp) {
    ScannerFile *scanner_file = kmalloc(sizeof(*scanner_file), GFP_KERNEL);
    if (!scanner_file) {
        printk(KERN_ERR "%s: kmalloc() failed for ScannerFile\n", DEVNAME);
        return -ENOMEM;
    }

    // Initialize the current token and next_char pointers
    scanner_file->current_token = NULL;
    scanner_file->next_char = scanner_device.data;

    // Allocate and set the default separators for this instance
    scanner_file->separators = kmalloc(strlen(scanner_device.separators) + 1, GFP_KERNEL);
    if (!scanner_file->separators) {
        printk(KERN_ERR "%s: kmalloc() failed for separators\n", DEVNAME);
        kfree(scanner_file);
        return -ENOMEM;
    }
    strcpy(scanner_file->separators, scanner_device.separators);

    filp->private_data = scanner_file;
    return 0;
}

static int scanner_release(struct inode *inode, struct file *filp) {
    ScannerFile *scanner_file = filp->private_data;
    if (scanner_file) {
        // Free the memory allocated for the separators
        kfree(scanner_file->separators);
        // Free the memory allocated for the ScannerFile instance
        kfree(scanner_file);
    }
    return 0;
}

static ssize_t scanner_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
    ScannerFile *scanner_file = filp->private_data;
    char *data_end = scanner_device.data + strlen(scanner_device.data);
    char *token_start = scanner_file->current_token;
    char *token_end;
    int token_len;

    // Return 0 if start position is at or beyond the end of data
    if (token_start >= data_end) {
        return 0;
    }

    // Find the end of the next token using the instance-specific separators
    while (token_start < data_end && strchr(scanner_file->separators, *token_start)) {
        token_start++;  // Skip leading separators
    }
    token_end = token_start;
    while (token_end < data_end && !strchr(scanner_file->separators, *token_end)) {
        token_end++;
    }

    // Calculate the length of the token to be read
    token_len = min((int)(token_end - token_start), (int)count);

    // Copy the token to user buffer
    if (copy_to_user(buf, token_start, token_len)) {
        return -EFAULT;  // Failed to copy data to user space
    }

    // Update the current position in the file
    scanner_file->current_token = token_end;

    // Return the number of bytes read
    return token_len;
}

static ssize_t scanner_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
    ScannerFile *scanner_file = filp->private_data;

    // Free the old data if it exists
    if (scanner_device.data) {
        kfree(scanner_device.data);
        scanner_device.data = NULL;
    }

    // Allocate memory for the new data, plus one extra byte for the null terminator
    scanner_device.data = kmalloc(count + 1, GFP_KERNEL);
    if (!scanner_device.data) {
        printk(KERN_ERR "%s: Unable to allocate memory for the data buffer\n", DEVNAME);
        return -ENOMEM;
    }

    // Copy the data from user space; copy_from_user returns the number of bytes that could not be copied
    if (copy_from_user(scanner_device.data, buf, count)) {
        printk(KERN_ERR "%s: Failed to copy data from user space\n", DEVNAME);
        kfree(scanner_device.data);
        scanner_device.data = NULL;
        return -EFAULT;
    }

    // Null-terminate the string
    scanner_device.data[count] = '\0';

    scanner_file->current_token = scanner_device.data;

    // Return the number of bytes written
    return count;
}

static long scanner_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    ScannerFile *scanner_file = filp->private_data;
    char *new_separators;
    int ret = 0;

    // Verify that cmd is for our device and the command number is within our range
    if (_IOC_TYPE(cmd) != SCANNER_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > 1) return -ENOTTY;

    switch (cmd) {
        case SCANNER_SET_SEPARATORS:
            // Allocate memory for new separators
            new_separators = kmalloc(arg + 1, GFP_KERNEL); // arg is the length of separators
            if (!new_separators) {
                return -ENOMEM;
            }

            // Copy the new separators from user space
            if (copy_from_user(new_separators, (char __user *)arg, arg)) {
        ret = -EFAULT;
    } else {
        new_separators[arg] = '\0';  // Ensure null termination

        // Free the old separators
        kfree(scanner_file->separators);
        scanner_file->separators = new_separators;

        printk(KERN_INFO "Separators updated for scanner instance.\n");
    }

            if (ret) {
                // If we failed, free the allocated memory for new separators
                kfree(new_separators);
            }
            return ret;

        default:
            return -ENOTTY;  // Command not supported
    }

}

static struct file_operations scanner_fops = {
        .owner = THIS_MODULE,
        .open = scanner_open,
        .release = scanner_release,
        .read = scanner_read,
        .write = scanner_write,
        .unlocked_ioctl = scanner_ioctl,
};

static int __init scanner_init(void) {
    int err;
    // Allocate memory for default separators
    scanner_device.separators = kmalloc(7, GFP_KERNEL); // 6 separators + null terminator
    if (!scanner_device.separators) {
        printk(KERN_ERR "%s: Unable to allocate memory for the separators\n", DEVNAME);
        return -ENOMEM;
    }
    // Set the default separators: space, tab, newline, carriage return, form feed, vertical tab
    strcpy(scanner_device.separators, " \t\n\r\f\v");

    // Continue with the rest of the initialization...
    err = alloc_chrdev_region(&scanner_device.devno, 0, 1, DEVNAME);
    if (err < 0) {
        printk(KERN_ERR "%s: alloc_chrdev_region() failed\n", DEVNAME);
        kfree(scanner_device.separators); // Free the separators if device registration fails
        return err;
    }

    cdev_init(&scanner_device.cdev, &scanner_fops);
    scanner_device.cdev.owner = THIS_MODULE;
    err = cdev_add(&scanner_device.cdev, scanner_device.devno, 1);
    if (err) {
        printk(KERN_ERR "%s: cdev_add() failed\n", DEVNAME);
        unregister_chrdev_region(scanner_device.devno, 1);
        kfree(scanner_device.separators); // Free the separators if cdev addition fails
        return err;
    }

    printk(KERN_INFO "%s: device initialized\n", DEVNAME);
    return 0;
}

static void __exit scanner_exit(void) {
    cdev_del(&scanner_device.cdev);
    unregister_chrdev_region(scanner_device.devno, 1);
    kfree(scanner_device.separators); // Free the memory allocated for separators
    kfree(scanner_device.data); // Also free the memory allocated for data if any
    printk(KERN_INFO "%s: device removed\n", DEVNAME);
}

module_init(scanner_init);
module_exit(scanner_exit);
