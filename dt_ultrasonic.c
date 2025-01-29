#include <linux/module.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/timekeeping.h>
#include <linux/math64.h>

#include "dt_ultrasonic.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Trigger an ultrasonic sensor using gpios configured with device tree");

static ktime_t time_start;
static ktime_t time_end;

unsigned int gpio_irq_number;

static unsigned int valid_value;

dev_t dev = 0;
static struct class *dev_class;
static struct cdev usnc_cdev;

// ioctl
int32_t ioctl_global = 0;

// fops structure
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = usnc_read,
    .write = usnc_write,
    .open = usnc_open,
    .release = usnc_release,
    .unlocked_ioctl = usnc_ioctl,
};

static struct of_device_id my_driver_ids[] = {
    {
        .compatible = "me,dt_ultrasonic",
    }, { /* sentinel */ }
};

// assign compatible device list to the module
MODULE_DEVICE_TABLE(of, my_driver_ids);

static struct platform_driver my_driver = {
    .probe = dt_probe,
    .remove = dt_remove,
    .driver = {
        .name = "dt_ultrasonic",
        .owner = THIS_MODULE,
        .of_match_table = my_driver_ids,
    },
};

static struct gpio_desc *usnc_out = NULL;
static struct gpio_desc *usnc_in = NULL;

// handle interrupts from gpio input pin 24
static irqreturn_t handle_gpio_irq(int irq, void *dev_id)
{
    pr_info("dt_ultrasonic: interrupt triggered\n");
    ktime_t ktime_temp;

    if (valid_value == 0) {
        ktime_temp = ktime_get();
        if (gpiod_get_value(usnc_in) == 1) {
            time_start = ktime_temp;
        } else {
            time_end = ktime_temp;
            valid_value = 1;
        }
    }

    return IRQ_HANDLED;
}

static int usnc_open(struct inode *inode, struct file *file)
{
    pr_info("dt_ultrasonic: device file opened\n");
    return 0;
}

static int usnc_release(struct inode *inode, struct file *file)
{
    pr_info("dt_ultrasonic: device file closed\n");
    return 0;
}

static long int usnc_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
    switch (cmd) {
        case IOCTL_WR_VALUE:
            if (copy_from_user(&ioctl_global, (int32_t *) arg, sizeof(ioctl_global))) {
                printk("dt_ultrasonic: error copying bytes FROM user\n");
            } else {
                printk("dt_ultrasonic: ioctl write: copied FROM user\n");
                ioctl_trigger(&arg);
            }
            break;
        case IOCTL_RD_VALUE:
            int len = snprintf(NULL, 0, "%d", ioctl_global);
            char to_copy[100] = { 0 };
            snprintf(to_copy, len + 1, "%d", ioctl_global);
            if (copy_to_user((char __user *) arg, to_copy, len + 1)) {
                printk("dt_ultrasonic: usnc_ioctl: error copying bytes TO user\n");
            } else {
                printk("dt_ultrasonic: ioctl read: copied TO user\n");
            }
            break;
    }
    return 0;
}

void ioctl_trigger(unsigned long *arg)
{
    int counter;
    unsigned long long result;
    int32_t result_cm;

    pr_info("dt_ultrasonic: ioctl: starting trigger\n");

    // trigger ultrasonic pulse
    gpiod_set_value(usnc_out, 1);
    udelay(10);
    gpiod_set_value(usnc_out, 0);

    valid_value = 0;

    counter = 0;
    while (valid_value == 0) {
        // out of range
        if (++counter > 35200) {
            pr_info("dt_ultrasonic: ioctl trigger: counter timeout\n");
        }
        udelay(1);
    }

    result = ktime_to_us(ktime_sub(time_end, time_start));
    pr_info("dt_ultrasonic: ioctl trigger: RESULT: %lld\n", result);
    // calculate measurement in cm:
    result_cm = (int32_t)div_u64(result, 58);
    pr_info("dt_ultrasonic: ioctl trigger: RESULT CM: %d\n", result_cm);
    ioctl_global = result_cm;
    pr_info("dt_ultrasonic: ioctl trigger: ioctl_global: %d\n", ioctl_global);
}

static ssize_t usnc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    uint8_t gpio_state = 0;
    uint8_t gpio_in_state = 0;

    // read gpio value
    gpio_state = gpiod_get_value(usnc_out);
    gpio_in_state = gpiod_get_value(usnc_in);

    // write to user
    len = 1;
    if (copy_to_user(buf, &gpio_in_state, len) > 0)
        pr_err("dt_ultrasonic: error copying bytes TO user\n");

    pr_info(
        "dt_ultrasonic: read: gpio 17 out %d gpio 24 in: %d\n", gpio_state, gpio_in_state
    );

    return 0;
}

static ssize_t usnc_write(
    struct file *filp, const char __user *buf, size_t len, loff_t *off
) {
    uint8_t rec_buf[10] = {0};
    int counter;
    unsigned long long result;

    if (copy_from_user(rec_buf, buf, len) > 0)
        pr_err("dt_ultrasonic: error copying bytes FROM user\n");

    if (rec_buf[0] == '1') {
        pr_info("dt_ultrasonic: write: starting trigger\n");

        // trigger ultrasonic pulse
        gpiod_set_value(usnc_out, 1);
        udelay(10);
        gpiod_set_value(usnc_out, 0);

        valid_value = 0;

        counter = 0;
        while (valid_value == 0) {
            // out of range
            if (++counter > 35200) {
                pr_info("dt_ultrasonic: write: counter timeout\n");
                return len;
            }
            udelay(1);
        }
    } else
        pr_err ("dt_ultrasonic: unknown command, must be 1\n");

    result = ktime_to_us(ktime_sub(time_end, time_start));
    pr_info("dt_ultrasonic: write: RESULT: %lld\n", result);
    // calculate measurement in cm:
    pr_info("dt_ultrasonic: write: RESULT CM: %lld\n", div_u64(result, 58));

    return len;
}

static int dt_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    int result, status;

    printk("dt_ultrasonic: entered dt_probe function\n");

    if (!device_property_present(dev, "output-gpio")) {
        printk("dt_ultrasonic: device property 'usnc-out-gpio' not found\n");
        return -1;
    }

    if (!device_property_present(dev, "input-gpio")) {
        printk("dt_ultrasonic: device property 'usnc-in-gpio' not found\n");
        return -1;
    }

    status = gpiod_direction_output(usnc_out, 0);
    if (status) {
        pr_err("dt_ultrasonic: error: failed to set output gpio direction\n");
    }
    pr_info("dt_ultrasonic: usnc_out direction output status: %d\n", status);
    usnc_out = gpiod_get(dev, "output", GPIOD_OUT_LOW);
    if (IS_ERR(usnc_out)) {
        pr_err("dt_ultrasonic: Error: could not set up gpio usnc-out\n");
        printk(KERN_ERR "dt_ultrasonic: PTR_ERR: %ld\n", PTR_ERR(usnc_out));
        printk(KERN_ERR "dt_ultrasonic: IS_ERR: %d\n", IS_ERR(usnc_out));
        //goto r_device;
        return -1 * IS_ERR(usnc_out);
    }

    status = gpiod_direction_input(usnc_in);
    if (status) {
        pr_err("dt_ultrasonic: error: failed to set input gpio direction\n");
    }
    pr_info("dt_ultrasonic: usnc_in direction input status: %d\n", status);
    usnc_in = gpiod_get(dev, "input", GPIOD_IN);
    if (IS_ERR(usnc_in)) {
        pr_err("dt_ultrasonic: Error: could not set up gpio usnc-in\n");
        //goto r_gpio_out;
        gpiod_put(usnc_out);
        return -1 * IS_ERR(usnc_in);
    }

    // get irq number for input pin
    gpio_irq_number = gpiod_to_irq(usnc_in);
    if (gpio_irq_number < 0) {
        pr_err("dt_ultrasonic: failed to get irq for input pin\n");
        //goto r_gpio_in;
        gpiod_put(usnc_in);
        gpiod_put(usnc_out);
        return gpio_irq_number;
    }

    // request irq for input pin
    pr_info("dt_ultrasonic: requesting irq...\n");
    result = request_irq(
        gpio_irq_number,
        (void *)handle_gpio_irq,
        IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
        "usnc_device",
        NULL
    );
    pr_info("dt_ultrasonic: irq result: %d\n", result);
    if (result) {
        pr_err("dt_ultrasonic: failed to request irq\n");
        //goto r_gpio_in;
        gpiod_put(usnc_in);
        gpiod_put(usnc_out);
        return result;
    }

    return 0;
}

static int dt_remove(struct platform_device *pdev)
{
    printk("dt_ultrasonic: entered remove function\n");
    free_irq(gpio_irq_number, NULL);
    gpiod_put(usnc_in);
    gpiod_put(usnc_out);
    return 0;
}

static int __init my_init(void)
{
    printk("dt_ultrasonic: loading driver...\n");

    // allocate major number
    if ((alloc_chrdev_region(&dev, 0, 1, "usnc_dev")) < 0) {
        pr_err("dt_ultrasonic: cannot allocate major number\n");
        goto r_unreg;
    }
    printk("dt_ultrasonic: major: %d minor: %d\n", MAJOR(dev), MINOR(dev));

    // create cdev structure
    cdev_init(&usnc_cdev, &fops);

    // add character device to system
    if ((cdev_add(&usnc_cdev, dev, 1)) < 0) {
        pr_err("dt_ultrasonic: cannot add device to system\n");
        goto r_del;
    }

    // create struct class
    if (IS_ERR(dev_class = class_create("usnc_class"))) {
        pr_err("dt_ultrasonic: cannot create struct class\n");
        goto r_class;
    }

    // create device
    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "usnc_device"))) {
        pr_err("dt_ultrasonic: cannot create device\n");
        goto r_device;
    }

    // init gpio and irq
    if (platform_driver_register(&my_driver)) {
        printk("dt_ultrasonic: error, failed to register platform driver\n");
        goto r_device;
        //return -1;
    }

    printk("dt_ultrasonic: successfully loaded driver\n");
    return 0;

//r_gpio_in:
//    gpiod_put(usnc_in);
//r_gpio_out:
//    gpiod_put(usnc_out);
r_device:
    device_destroy(dev_class, dev);
r_class:
    class_destroy(dev_class);
r_del:
    cdev_del(&usnc_cdev);
r_unreg:
    unregister_chrdev_region(dev, 1);

    return -1;
}

static void __exit my_exit(void)
{
    printk("dt_ultrasonic: unloading driver...\n");
    platform_driver_unregister(&my_driver);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&usnc_cdev);
    unregister_chrdev_region(dev, 1);
    printk("dt_ultrasonic: unloaded driver\n");
}

module_init(my_init);
module_exit(my_exit);
