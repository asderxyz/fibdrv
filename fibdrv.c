#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

#define MAX_FIB_ALGO 4

/******************************************************************************/
#define MAX_STR_LEN_BITS (54)
#define MAX_STR_LEN ((1UL << MAX_STR_LEN_BITS) - 1)
#define LARGE_STRING_LEN 256

#define xs_literal_empty() \
    (xs) { .space_left = 15 }

#define xs_tmp(x) xs_new(&xs_literal_empty(), x)

typedef union {
    char data[16];

    struct {
        uint8_t filler[15],
        space_left : 4,
        is_ptr : 1, is_large_string : 1, flag2 : 1, flag3 : 1;
    };

    struct {
        char *ptr;
        size_t size : MAX_STR_LEN_BITS,
               capacity : 6;
    };
} xs;

static inline bool xs_is_ptr(const xs *x)
{
    return x->is_ptr;
}

static inline bool xs_is_large_string(const xs *x)
{
    return x->is_large_string;
}

static inline size_t xs_size(const xs *x)
{
    return xs_is_ptr(x) ? x->size : 15 - x->space_left;
}

static inline char *xs_data(const xs *x)
{
    if (!xs_is_ptr(x))
        return (char *) x->data;

    if (xs_is_large_string(x))
        return (char *) (x->ptr + 4);

    return (char *)x->ptr;
}

static inline void xs_set_ref_count(const xs *x, int val)
{
    *((int *)((size_t)x->ptr)) = val;
}

static inline int xs_dec_ref_count(const xs *x)
{
    if (!xs_is_large_string(x))
        return 0;
    return --(*(int *)((size_t)x->ptr));
}

static inline xs *xs_newempty(xs *x)
{
    *x = xs_literal_empty();
    return x;
}

static inline xs *xs_free(xs *x)
{
    if (xs_is_ptr(x) && xs_dec_ref_count(x) <= 0)
        kfree(x->ptr);
    return xs_newempty(x);
}

xs *xs_new(xs *x, const void *p);
/******************************************************************************/
static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);
static int fib_algo_selection;
static void string_number_add(xs *a, xs *b, xs *out);

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

static long long fib_sequence_fast_doubling_clz_no_multiply(long long k, char *buf)
{
    long long a = 0, b = 1;

    if (!k)
        return 0;

    for (int i = ilog2(k); i >= 0; i--) {
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

static long long fib_sequence_fast_doubling_clz(long long k, char *buf)
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

static long long fib_sequence_fast_doubling(long long k, char *buf)
{
    long long a = 0, b = 1;

    if (!k)
        return 0;

    for (int i = 31; i >= 0; i--) {
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

static long long fib_sequence_orig(long long k, char *buf)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    xs f[k + 2];
    int i, n;

    f[0] = *xs_tmp("0");
    f[1] = *xs_tmp("1");

    for (i = 2; i <= k; i++)
        string_number_add(&f[i - 1], &f[i - 2], &f[i]);

    n = xs_size(&f[k]);
    if (copy_to_user(buf, xs_data(&f[k]), n))
            return -EFAULT;

    for (i = 0; i <= k; i++)
        xs_free(&f[i]);

    return n;
    /*
    long long f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }

    return f[k];
    */
}

static long long (*fib_seq_func[])(long long, char *) = {
    fib_sequence_orig,
    fib_sequence_fast_doubling,
    fib_sequence_fast_doubling_clz,
    fib_sequence_fast_doubling_clz_no_multiply,
};

char fib_algo_str[MAX_FIB_ALGO][64] = {
    "Original Fibonacci",
    "Fast doubling Fibonacci",
    "Fast doubling Fibonacci with clz",
    "Fast doubling Fibonacci with clz + 'No multiply'",
};

static ktime_t kt[MAX_LENGTH + 1];

static inline long long fib_time_proxy(long long k, char *buf)
{
    kt[k] = ktime_get();
    long long result = fib_seq_func[fib_algo_selection](k, buf);
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
    return (ssize_t) fib_time_proxy(*offset, buf);
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

/******************************************************************************/
#define SWAP(a, b, type) \
    do {                 \
        type *__c = (a); \
        type *__d = (b); \
        *__c ^= *__d;    \
        *__d ^= *__c;    \
        *__c ^= *__d;    \
    } while(0)

static void _swap(void *a, void *b, size_t size) 
{
    if (a == b)
        return;

    switch (size) 
    {
        case 1:
            SWAP(a, b, char);
            break;
        case 2:
            SWAP(a, b, short);
            break;
        case 4:
            SWAP(a, b, unsigned int);
            break;
        case 8:
            SWAP(a, b, unsigned long);
            break;
        default:
            break;
    }
}

static void reverse_str(char *str, size_t n)
{
    for (int i = 0; i < (n >> 1); i++)
        _swap(&str[i], &str[n - i - 1], sizeof(char));
}

static void xs_allocate_data(xs *x, size_t len, bool reallocate)
{
    size_t n = 1 << x->capacity;
    if (len < LARGE_STRING_LEN) {
        x->ptr = reallocate ? krealloc(x->ptr, n, GFP_KERNEL)
            : kmalloc(n, GFP_KERNEL);
    return;
    }

    x->is_large_string = 1;
    x->ptr = reallocate ? krealloc(x->ptr, n + 4, GFP_KERNEL)
        : kmalloc(n + 4, GFP_KERNEL);
    xs_set_ref_count(x, 1);
}

xs *xs_new(xs *x, const void *p)
{
    *x = xs_literal_empty();
    //value from strlen() returned do not include terminate null character.
    //len is include terminating null character.
    size_t len = strlen(p) + 1;
    if (len > 16) {
        x->capacity = ilog2(len) + 1;
        //x->size is not include terminating null character.
        x->size = len - 1;
        x->is_ptr = true;
        xs_allocate_data(x, x->size, 0);
        memcpy(xs_data(x), p, len);
    } else {
        memcpy(x->data, p, len);
        x->space_left = 15 - (len - 1);
    }
    return x;
}

/******************************************************************************/
static void string_number_add(xs *a, xs *b, xs *out)
{
    char *data_a, *data_b;
    size_t size_a, size_b;
    int i, carry = 0;
    int sum;

    if (xs_size(a) < xs_size(b)) {
        xs *p = a;
        a = b;
        b = p;
        //_swap((void *)&a, (void *)&b, sizeof(void *));
    }

    data_a = xs_data(a);
    data_b = xs_data(b);

    size_a = xs_size(a);
    size_b = xs_size(b);

    reverse_str(data_a, size_a);
    reverse_str(data_b, size_b);
    
    //here has size problem to cause segemtn fault.
    char buf[size_a + 2];

    for (i = 0; i < size_b; i++) {
        sum = (data_a[i] - '0') + (data_b[i] - '0') + carry;
        buf[i] = '0' + sum % 10;
        carry = sum / 10;
    }

    for (i = size_b; i < size_a; i++) {
        sum = (data_a[i] - '0') + carry;
        buf[i] = '0' + sum % 10;
        carry = sum / 10;
    }

    if (carry)
        buf[i++] = '0' + carry;

    buf[i] = 0;

    reverse_str(buf, i);

    reverse_str(data_a, size_a);
    reverse_str(data_b, size_b);

    if (out)
        *out = *xs_tmp(buf);
}
