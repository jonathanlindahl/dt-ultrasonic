#ifndef DT_ULTRASONIC_H
#define DT_ULTRASONIC_H

static int dt_probe(struct platform_device *pdev);
static int dt_remove(struct platform_device *pdev);

static int usnc_open(struct inode *inode, struct file *file);
static int usnc_release(struct inode *inode, struct file *file);
static ssize_t usnc_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t usnc_write(struct file *filp, const char *buf, size_t len, loff_t *off);
static long int usnc_ioctl(struct file *file, unsigned cmd, unsigned long arg);

static irqreturn_t handle_gpio_irq(int irq, void *dev_id);

// userspace registration
#define REGISTER_UAPP _IO('R', 'g')

// signals
#define SIGNR 44
void usnc_send_signal(void);

#define MAJOR_NUM 236
#define IOCTL_WR_VALUE _IOW(MAJOR_NUM, 0, char *)
#define IOCTL_RD_VALUE _IOR(MAJOR_NUM, 1, char *)
void ioctl_trigger(unsigned long *arg);

#endif // DT_ULTRASONIC_H
