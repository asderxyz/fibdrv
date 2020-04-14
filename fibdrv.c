#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 92

#define MAX_FIB_ALGO 3

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);
static int fib_algo_selection;

static inline long long multiply(long long a, long long b)
{
    long long res = 0;

    /*
     * 1. Find the MSB (Most Significant Bit) position of 'b'.
     * 2. Accumulate the result with (1 << MSB position).
     * 3. Clear the MSB position.
     *
     * FIXME: Implement the negative number multiplication.
     */
    while (b) {
        int msb_pos;

        msb_pos = ilog2(b);
        res += (a << msb_pos);

        /* Clear MSB */
        b &= ~(1ULL << msb_pos);
    }

    return res;
}

static long long fib_sequence_fast_dobuling_optimized(long long k)
{
    int msb_pos = ilog2(k);
    long long a = 0, b = 1;

    if (!k)
        return 0;

    for (int i = msb_pos; i >= 0; i--) {
        long long t1, t2;

        t1 = multiply(a, (b << 1) - a);
        t2 = multiply(b, b) + multiply(a, a);

        a = t1;
        b = t2;

        if (k & (1ULL << i)) {
            t1 = a + b;
            a = b;
            b = t1;
        }
    }

    return a;
}

static long long fib_sequence_fast_dobuling(long long k)
{
    long long a = 0, b = 1;

    if (!k)
        return 0;

    for (int i = ilog2(k); i >= 0; i--) {
        long long t1, t2;

        t1 = a * (b * 2 - a);
        t2 = b * b + a * a;

        a = t1;
        b = t2;

        if (k & (1ULL << i)) {
            t1 = a + b;
            a = b;
            b = t1;
        }
    }

    return a;
}

static long long fib_sequence_orig(long long k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    long long f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }

    return f[k];
}

static long long (*fib_seq_func[])(long long) = {
    fib_sequence_orig,
    fib_sequence_fast_dobuling,
    fib_sequence_fast_dobuling_optimized,
};

char fib_algo_str[MAX_FIB_ALGO][64] = {
    "Original Fibonacci",
    "Fast doubling Fibonacci",
    "Fast doubling Fibonacci by eliminating multiplication",
};

static ktime_t kt[MAX_LENGTH + 1];

static inline long long fib_time_proxy(long long k)
{
    kt[k] = ktime_get();
    long long result = fib_seq_func[fib_algo_selection](k);
    kt[k] = ktime_sub(ktime_get(), kt[k]);

    return result;
}

/* calculate the fibonacci number at given offset */
static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    return (ssize_t) fib_time_proxy(*offset);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static struct kobject *fib_kobj;

static ssize_t fib_time_show(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             char *buf)
{
    char *str = buf;

    for (int i = 0; i <= MAX_LENGTH; i++)
        str += snprintf(str, 80, "offset %d, execution time: %lld\n", i,
                        ktime_to_ns(kt[i]));

    return str - buf;
}

static ssize_t fib_algo_show(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             char *buf)
{
    return snprintf(buf, 80, "%d <%s>\n", fib_algo_selection,
                    fib_algo_str[fib_algo_selection]);
}

static ssize_t fib_algo_store(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              const char *buf,
                              size_t count)
{
    int var, ret;

    ret = kstrtoint(buf, 10, &var);
    if (ret < 0)
        return ret;

    if (var < 0 || var >= MAX_FIB_ALGO)
        return -EINVAL;

    fib_algo_selection = var;

    return count;
}

static struct kobj_attribute fib_time_attr =
    __ATTR(fib_time, 0664, fib_time_show, NULL);
static struct kobj_attribute fib_algo_attr =
    __ATTR(fib_algo, 0664, fib_algo_show, fib_algo_store);

static struct attribute *attrs[] = {
    &fib_time_attr.attr,
    &fib_algo_attr.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }

    fib_kobj = kobject_create_and_add(DEV_FIBONACCI_NAME, kernel_kobj);
    if (!fib_kobj)
        return -ENOMEM;

    rc = sysfs_create_group(fib_kobj, &attr_group);
    if (rc)
        kobject_put(fib_kobj);

    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    kobject_put(fib_kobj);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
