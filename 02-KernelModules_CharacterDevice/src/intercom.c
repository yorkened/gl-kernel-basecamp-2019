#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/string.h>

#define DEV_NAME	"intercom"
#define CLASS_NAME	"GL"
#define ATTR_NAME	clear_buf
#define PROC_FNAME	"bufstate"
#define PROC_BUF_SIZE	32
#define MIN_BUF_SIZE	1024
#define ASCII_MIN	0
#define ASCII_MAX	127

static size_t buf_size = MIN_BUF_SIZE;
module_param(buf_size, int, 0660);

static char *message;
static short msg_size;

static dev_t chrdev;
static struct class *intercom_cls;
static struct cdev intercom_dev;

static ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

static struct file_operations dev_fops = {
	.read = dev_read,
	.write = dev_write
};

static int intercom_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "DEVMODE=%#o", 0666) != 0)
		return -ENOMEM;
	return 0;
}

static int create_buffer(void)
{
	if (buf_size < MIN_BUF_SIZE) {
		pr_warn("Increasing buffer size to %u bytes\n", MIN_BUF_SIZE);
		buf_size = MIN_BUF_SIZE;
	}
	message = kmalloc(buf_size, GFP_KERNEL);
	if (message == NULL)
		return -ENOMEM;
	msg_size = 0;
	return 0;
}

static void cleanup_buffer(void)
{
	if (message) {
		kfree(message);
		message = NULL;
	}
	msg_size = 0;
}

static ssize_t dev_read(struct file *fileptr, char __user *buf,
	size_t len, loff_t *offset)
{
	ssize_t status;

	if (len > msg_size)
		len = msg_size;
	status = simple_read_from_buffer(buf, len, offset, message, msg_size);

	if (status < 0)
		pr_err("Failed to read %u bytes\n", msg_size);
	else if (status > 0) {
		pr_info("Succefully read %d bytes\n", status);
		msg_size -= status;
	}
	return status;
}

static int __init intercom_init(void)
{
	int err;

	pr_info("Intercom: Initializing kernel module\n");
	err = alloc_chrdev_region(&chrdev, 0, 1, DEV_NAME);

	if (err < 0) {
		pr_alert("Failed to register dev major number\n");
		return err;
	}
	pr_info("Major number registered correctly [%d]\n", MAJOR(chrdev));
	intercom_cls = class_create(THIS_MODULE, CLASS_NAME);

	if (IS_ERR(intercom_cls)) {
		unregister_chrdev_region(chrdev, 1);
		pr_alert("Failed to register device class\n");
		return PTR_ERR(intercom_cls);
	}
	intercom_cls->dev_uevent = intercom_uevent;

	if (device_create(intercom_cls, NULL, chrdev, NULL, DEV_NAME) == NULL) {
		class_destroy(intercom_cls);
		unregister_chrdev_region(chrdev, 1);
		pr_alert("Failed to create the device\n");
		return -1;
	}
	cdev_init(&intercom_dev, &dev_fops);
	if (cdev_add(&intercom_dev, chrdev, 1) == -1) {
		device_destroy(intercom_cls, chrdev);
		class_destroy(intercom_cls);
		unregister_chrdev_region(chrdev, 1);
		pr_alert("Failed to add the device\n");
		return -1;
	}
	pr_info("Device created correctly\n");

	err = create_buffer();
	if (err)
		goto error;

	return 0;

error:
	pr_err("Failed to load module\n");
	cleanup_buffer();
	return err;
}

static void __exit intercom_exit(void)
{
	cleanup_buffer();
	device_destroy(intercom_cls, chrdev);
	class_destroy(intercom_cls);
	unregister_chrdev_region(chrdev, 1);
	pr_info("Exiting intercom\n");
}

module_init(intercom_init);
module_exit(intercom_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikita Kvochka <yorkened@gmail.com>");
MODULE_DESCRIPTION("Simple char dev for text messaging between users");
MODULE_VERSION("0.1");
