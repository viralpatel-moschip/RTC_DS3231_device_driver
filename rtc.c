#include <linux/module.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/gpio.h>     
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/proc_fs.h>

#define CLASS_NAME "rtc_class"

#define I2C_BUS_AVAILABLE   (2)   // I2C Bus available in our Beaglebone black
#define SLAVE_DEVICE_NAME   ("DS3231")   // Device and Driver Name
#define DS3231_SLAVE_ADDR   (0x68)   // DS3231 RTC Slave Address

#define RTC_SEC_REG_ADDR    (0x00)
#define RTC_MIN_REG_ADDR    (0x01)
#define RTC_HR_REG_ADDR     (0x02)
#define RTC_DAY_REG_ADDR    (0x03)
#define RTC_DATE_REG_ADDR   (0x04)
#define RTC_MON_REG_ADDR    (0x05)
#define RTC_YR_REG_ADDR     (0x06)
#define RTC_ALM1_REG_ADDR   (0x07)
#define RTC_ALM2_REG_ADDR   (0x0B)
#define RTC_CTL_REG_ADDR    (0x0E)
#define RTC_STAT_REG_ADDR   (0x0F)

#define RTC_A1M1            (0x80)
#define RTC_A1M2            (0x80)
#define RTC_A1M4            (0x80)
#define RTC_A1M3            (0x80)

#define RTC_CTL_BIT_A1IE    (0x01)
#define RTC_CTL_BIT_A2IE    (0x02)
#define RTC_CTL_BIT_INTCN   (0x04)
#define RTC_CTL_BIT_RS1     (0x08)
#define RTC_CTL_BIT_RS2     (0x10)
#define RTC_CTL_BIT_DOSC    (0x80)

#define RTC_STAT_BIT_A1F    (0x01)
#define RTC_STAT_BIT_A2F    (0x02)
#define RTC_STAT_BIT_OSF    (0x80)
#define DS3231_ALARM_GPIO_PIN (20) // GPIO pin number connected to DS3231 SQW pin

unsigned int GPIO_irqNumber;
static struct kobject *kobj_ref;

static struct workqueue_struct *ds3231_wq;
static struct work_struct ds3231_work;

static void ds3231_work_handler(struct work_struct *work);

static bool alarm1_status = false;

unsigned char current_hour, current_min, current_sec;
unsigned char current_day, current_date, current_month, current_year;

static struct i2c_adapter *rtc_i2c_adapter = NULL;
static struct i2c_client *rtc_i2c_client = NULL;

// Function prototypes
static int DS3231_GetTime(unsigned char *hour, unsigned char *min, unsigned char *sec);
static int DS3231_GetDate(unsigned char *day, unsigned char *date, unsigned char *month, unsigned char *year);

static int DS3231_SetTime(unsigned char hour, unsigned char min, unsigned char sec);
static int DS3231_SetDate(unsigned char day, unsigned char date, unsigned char month, unsigned char year);

// Declare system time and date functions
static void get_system_time(unsigned char *hour, unsigned char *min, unsigned char *sec);
static void get_system_date(unsigned char *day, unsigned char *date, unsigned char *month, unsigned char *year);

static void DS3231_SetAlarm1After(unsigned char hour_add, unsigned char min_add, unsigned char sec_add);
static void DS3231_SetAlarm1(unsigned char hour, unsigned char min, unsigned char sec);

//Function to convert binary to BCD
unsigned char bin2bcd(unsigned char bin)
{
    return ((bin / 10) << 4) + (bin % 10);
}

// Function to convert BCD to binary
static unsigned char bcd2bin(unsigned char val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

//Function to print data
static void DS3231_PrintTimeDate(void)
{

    DS3231_GetTime(&current_hour, &current_min, &current_sec);
    DS3231_GetDate(&current_day, &current_date, &current_month, &current_year);

    pr_info("Current Time: %02x:%02x:%02x\n", current_hour, current_min, current_sec);
    pr_info("Current Date: %02x/%02x/20%02x (Day of week: %02x)\n", current_date, current_month, current_year, current_day);
}

static int I2C_Write(unsigned char *buf, unsigned int len)
{
    int ret = i2c_master_send(rtc_i2c_client, buf, len);
    return ret;
}

static int I2C_Read(unsigned char *out_buf, unsigned int len)
{
    int ret = i2c_master_recv(rtc_i2c_client, out_buf, len);
    return ret;
}

//Write to ds3231 register
static void DS3231_Write(unsigned char reg_addr, unsigned char data)
{
    unsigned char buf[2] = {0};
    int ret;
    
    buf[0] = reg_addr;
    buf[1] = data;
    ret = I2C_Write(buf, 2);
}

//Read from ds3231 register
static unsigned char DS3231_Read(unsigned char reg_addr)
{
    unsigned char data = 0;
    int ret;
    
    ret = I2C_Write(&reg_addr, 1);
    if (ret < 0) {
        pr_err("I2C write error: %d\n", ret);
	return 0;
    }
    
    ret = I2C_Read(&data, 1);
    if (ret < 0) {
        pr_err("I2C read error: %d\n", ret);
        return 0;
    }
    return data;
}

//Initialization of ds3231
static int DS3231_Init(void)
{
    int ret = 0;

    // Get system time and date
    unsigned char sec;

    pr_info("DS3231_Init - Initializes the DS3231 RTC with default settings");

    // Clear control register
    DS3231_Write(RTC_CTL_REG_ADDR, 0x00);

    // Clear status register
    DS3231_Write(RTC_STAT_REG_ADDR, 0x00);

    // Enable oscillator without clearing the seconds register
    sec = DS3231_Read(RTC_SEC_REG_ADDR);
    if (sec < 0) {
        pr_err("Failed to read RTC_SEC_REG_ADDR\n");
        return sec;  // Return error code
    }
    DS3231_Write(RTC_SEC_REG_ADDR, sec & ~RTC_STAT_BIT_OSF);

    // Set control register: Enable Battery-Backed Square-Wave Output
    DS3231_Write(RTC_CTL_REG_ADDR, RTC_CTL_BIT_INTCN | RTC_CTL_BIT_RS1 | RTC_CTL_BIT_RS2);

    get_system_time(&current_hour, &current_min, &current_sec);
    get_system_date(&current_day, &current_date, &current_month, &current_year);

    // Set DS3231 time and date to match the system time and date
    ret = DS3231_SetTime(current_hour, current_min, current_sec);
    if (ret < 0) {
        pr_err("Failed to set time\n");
        return ret; 
    }
    
    ret = DS3231_SetDate(current_day, current_date, current_month, current_year);
    if (ret < 0) {
        pr_err("Failed to set date\n");
        return ret; 
    }

    return ret;
}

// Function to get the current time
static int DS3231_GetTime(unsigned char *hour, unsigned char *min, unsigned char *sec)
{
    pr_info("DS3231_GetTime - Gets the current time from the DS3231 RTC");
    *sec = DS3231_Read(RTC_SEC_REG_ADDR);
    *min = DS3231_Read(RTC_MIN_REG_ADDR);
    *hour = DS3231_Read(RTC_HR_REG_ADDR);
    
    return 0;
}

// Function to set the time
static int DS3231_SetTime(unsigned char hour, unsigned char min, unsigned char sec)
{
    pr_info(" DS3231_SetTime - Sets the time on the DS3231 RTC");
    DS3231_Write(RTC_SEC_REG_ADDR, bin2bcd(sec));
    DS3231_Write(RTC_MIN_REG_ADDR, bin2bcd(min));
    DS3231_Write(RTC_HR_REG_ADDR, bin2bcd(hour));
    
    return 0;
}

// Function to set the date
static int DS3231_SetDate(unsigned char day, unsigned char date, unsigned char month, unsigned char year)
{
    pr_info("DS3231_SetDate - Sets the date on the DS3231 RTC");
    DS3231_Write(RTC_DAY_REG_ADDR, bin2bcd(day));
    DS3231_Write(RTC_DATE_REG_ADDR, bin2bcd(date));
    DS3231_Write(RTC_MON_REG_ADDR, bin2bcd(month));
    DS3231_Write(RTC_YR_REG_ADDR, bin2bcd(year));
    
    return 0;
}

// Function to get the current date and day
static int DS3231_GetDate(unsigned char *day, unsigned char *date, unsigned char *month, unsigned char *year)
{
    pr_info("DS3231_GetDate - get the date on the DS3231 RTC");
    *day = DS3231_Read(RTC_DAY_REG_ADDR);
    *date = DS3231_Read(RTC_DATE_REG_ADDR);
    *month = DS3231_Read(RTC_MON_REG_ADDR);
    *year = DS3231_Read(RTC_YR_REG_ADDR);
    
    return 0;
}

// Function to set the alarm on the DS3231 RTC
static void DS3231_SetAlarm1(unsigned char hour, unsigned char min, unsigned char sec)
{
    unsigned char ctl, status, match;
    unsigned char set_sec, set_hour, set_min;

    //pr_info("DS3231_SetAlarm1 - Sets Alarm 1 on the DS3231 RTC");

    // Set the alarm time
    DS3231_Write(RTC_ALM1_REG_ADDR + 0, sec); // Set seconds
    DS3231_Write(RTC_ALM1_REG_ADDR + 1, min); // Set minutes
    DS3231_Write(RTC_ALM1_REG_ADDR + 2, hour); // Set hours
    DS3231_Write(RTC_ALM1_REG_ADDR + 3, 0x80); // Set date to don't care

    // Match conditions for alarm
    match = DS3231_Read(RTC_ALM1_REG_ADDR);
    DS3231_Write(RTC_ALM1_REG_ADDR + 0, match & ~RTC_A1M1);

    match = DS3231_Read(RTC_ALM1_REG_ADDR + 1);
    DS3231_Write(RTC_ALM1_REG_ADDR + 1, match & ~RTC_A1M2);

    match = DS3231_Read(RTC_ALM1_REG_ADDR + 2);
    DS3231_Write(RTC_ALM1_REG_ADDR + 2, match & ~RTC_A1M3);

    match = DS3231_Read(RTC_ALM1_REG_ADDR + 3);
    DS3231_Write(RTC_ALM1_REG_ADDR + 3, match & RTC_A1M4);

    // Enable Alarm 1 interrupt
    ctl = DS3231_Read(RTC_CTL_REG_ADDR);
    //pr_info("Control register before setting alarm: 0x%02x\n", ctl);
    ctl |= RTC_CTL_BIT_A1IE | RTC_CTL_BIT_INTCN;
    DS3231_Write(RTC_CTL_REG_ADDR, ctl);
    //pr_info("Control register after setting alarm: 0x%02x\n", ctl);

    // Clear the A1F bit in the status register if it is set
    status = DS3231_Read(RTC_STAT_REG_ADDR);
    //pr_info("Status register before: 0x%02x\n", status);
    if (status & RTC_STAT_BIT_A1F) {
        DS3231_Write(RTC_STAT_REG_ADDR, status & ~RTC_STAT_BIT_A1F);
    }
    status = DS3231_Read(RTC_STAT_REG_ADDR);
    //pr_info("Status register after: 0x%02x\n", status);

    // Print the set alarm time
    set_sec = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 0));
    set_min = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 1));
    set_hour = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 2));
    pr_info("Alarm 1 set for: %02x:%02x:%02x\n", bin2bcd(set_hour), bin2bcd(set_min), bin2bcd(set_sec));
    
    // indicate the status of alarm
    alarm1_status = true;
}

// Function to set the alarm on the DS3231 RTC after a specified duration
static void DS3231_SetAlarm1After(unsigned char hour_add, unsigned char min_add, unsigned char sec_add)
{
    unsigned char curr_hour_bcd = DS3231_Read(RTC_HR_REG_ADDR);
    unsigned char curr_min_bcd = DS3231_Read(RTC_MIN_REG_ADDR);
    unsigned char curr_sec_bcd = DS3231_Read(RTC_SEC_REG_ADDR);

    unsigned char curr_hour = bcd2bin(curr_hour_bcd);
    unsigned char curr_min = bcd2bin(curr_min_bcd);
    unsigned char curr_sec = bcd2bin(curr_sec_bcd);

    unsigned char new_sec = curr_sec + sec_add;
    unsigned char new_min = curr_min + min_add + (new_sec / 60);
    unsigned char new_hour = curr_hour + hour_add + (new_min / 60);

    new_sec %= 60;
    new_min %= 60;
    new_hour %= 24;

    pr_info("DS3231_SetAlarm1After - Sets Alarm 1 on the DS3231 RTC after %u hours, %u minutes, %u seconds\n", hour_add, min_add, sec_add);

    DS3231_SetAlarm1(bin2bcd(new_hour), bin2bcd(new_min), bin2bcd(new_sec));
}

// Functions to get system time and date
static void get_system_time(unsigned char *hour, unsigned char *min, unsigned char *sec)
{
    struct timespec64 ts;
    struct tm tm;

    ktime_get_real_ts64(&ts);
    time64_to_tm(ts.tv_sec, 0, &tm);

    *hour = tm.tm_hour;
    *min = tm.tm_min;
    *sec = tm.tm_sec;
}

static void get_system_date(unsigned char *day, unsigned char *date, unsigned char *month, unsigned char *year)
{
    struct timespec64 ts;
    struct tm tm;

    ktime_get_real_ts64(&ts);
    time64_to_tm(ts.tv_sec, 0, &tm);

    *day = tm.tm_wday + 1;         // Day of the week (0 = Sunday, 1 = Monday, ...)
    *date = tm.tm_mday;            // Day of the month (1 - 31)
    *month = tm.tm_mon + 1;        // Month (0 - 11), so add 1 to match standard (1 - 12)
    *year = tm.tm_year - 100;      // Years since 1900, so subtract 100 to get years since 2000
}

static int ds3231_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    rtc_i2c_client = client;

    DS3231_Init();
    DS3231_PrintTimeDate();

    // Set alarm for given seconds from now
    //DS3231_SetAlarm1After(0, 0, 10);

    return 0;
}


static int ds3231_remove(struct i2c_client *client)
{
    if (!rtc_i2c_client) {
        pr_info("DS3231: Already removed!\n");
        return 0;
    }
    
    return 0;
}

static const struct i2c_device_id ds3231_id[] = {
    { SLAVE_DEVICE_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ds3231_id);

static struct i2c_driver ds3231_driver = {
    .driver = {
        .name   = SLAVE_DEVICE_NAME,
        .owner  = THIS_MODULE,
    },
    .probe          = ds3231_probe,
    .remove         = ds3231_remove,
    .id_table       = ds3231_id,
};

static struct i2c_board_info rtc_i2c_board_info = {

    I2C_BOARD_INFO(SLAVE_DEVICE_NAME, DS3231_SLAVE_ADDR)
};

// Interrupt handler
static irqreturn_t ds3231_irq_handler(int irq, void *dev_id)
{
    // Schedule the work to be handled in process context
    queue_work(ds3231_wq, &ds3231_work);
    return IRQ_HANDLED;
}

static void ds3231_work_handler(struct work_struct *work) {
   
        unsigned char status;
         // Read the status register
    	status = DS3231_Read(RTC_STAT_REG_ADDR);
 
    	// Check if the alarm flag is set
    	if (status & RTC_STAT_BIT_A1F) {
		// Handle the alarm event here
    		//pr_info("Alarm Ringing, Status register: 0x%02x\n", status);
    		pr_info("Alarm 1 is Ringing :)\n");
 
        	// Clear the alarm flag by writing back to the status register
        	DS3231_Write(RTC_STAT_REG_ADDR, status & ~RTC_STAT_BIT_A1F);
		// indicate the status of alarm
    		alarm1_status = false;
        }
}

/* procfs start */

#define PROCFS_MAX_SIZE 1024

static struct proc_dir_entry *rtc_proc_file;

static ssize_t rtc_proc_read(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    
    char *proc_buf;
    int proc_buf_len;
    ssize_t ret;

    //Indicate the file has already been read
    if (*offset > 0) {
        return 0;
    }
    
    //Allocate memory to buffer of size 1024,GFP-get free pages kernel (flag indicating memory allocation to kernel)
    proc_buf = kmalloc(PROCFS_MAX_SIZE, GFP_KERNEL);
    if (!proc_buf) {
        return -ENOMEM;
    }

    //Print current time and date along with status of alarm on or off
    proc_buf_len = snprintf(proc_buf, PROCFS_MAX_SIZE,
      "Current RTC Time: %02x:%02x:%02x\nCurrent RTC Date: %02x/%02x/20%02x (Day of Week: %02x)\nAlarm1 status: %s\n",
       DS3231_Read(RTC_HR_REG_ADDR), DS3231_Read(RTC_MIN_REG_ADDR), DS3231_Read(RTC_SEC_REG_ADDR),
       DS3231_Read(RTC_DATE_REG_ADDR), DS3231_Read(RTC_MON_REG_ADDR), DS3231_Read(RTC_YR_REG_ADDR), DS3231_Read(RTC_DAY_REG_ADDR), alarm1_status ? "Enable" : "Disable");

    if (proc_buf_len < 0) {
        kfree(proc_buf);
        return -EFAULT;
    }

    //Copies data from kernel space to user space 
    ret = simple_read_from_buffer(buffer, len, offset, proc_buf, proc_buf_len);

    kfree(proc_buf);
    return ret;
}

static const struct file_operations rtc_proc_fops = {
    .owner = THIS_MODULE,
    .read = rtc_proc_read,
};

/* procfs end */

/* sysfs start */
// Function to handle reading from the RTC through sysfs
static ssize_t rtc_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    printk(KERN_INFO "Sysfs - RTC Read!!!\n");

    return sprintf(buf,"Current RTC Time: %02x:%02x:%02x\nCurrent RTC Date: %02x/%02x/20%02x (Day of Week: %02x)\n",
       DS3231_Read(RTC_HR_REG_ADDR), DS3231_Read(RTC_MIN_REG_ADDR), DS3231_Read(RTC_SEC_REG_ADDR),
       DS3231_Read(RTC_DATE_REG_ADDR), DS3231_Read(RTC_MON_REG_ADDR), DS3231_Read(RTC_YR_REG_ADDR), DS3231_Read(RTC_DAY_REG_ADDR));

}

// Function to handle writing to the RTC through sysfs
static ssize_t rtc_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    unsigned int hour, min, sec;
    unsigned int date, month, year, day;

    printk(KERN_INFO "Sysfs - RTC Write!!!\n");
    
    // Parse the input buffer to extract the new time and date values
    ret = sscanf(buf, "set time: %u:%u:%u, set date: %u/%u/%u, day of week: %u", &hour, &min, &sec, &date, &month, &year, &day);

    // Check if the input format is correct
    if (ret != 7) {
        printk(KERN_ERR "Invalid time/date format\n");
        return -EINVAL;
    }

    DS3231_SetTime(hour, min, sec);
    DS3231_SetDate(day, date, month, year);

    return count;
}


// Define the RTC attribute for sysfs
static struct kobj_attribute rtc_attr = __ATTR(rtc_time, 0660, rtc_sysfs_show, rtc_sysfs_store);

// Function to handle reading from the RTC alarm through sysfs
static ssize_t alarm_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    
    unsigned char set_sec, set_hour, set_min;
    
    printk(KERN_INFO "Sysfs - Alarm Read!!!\n");
    
    // Print the set alarm time
    set_sec = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 0));
    set_min = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 1));
    set_hour = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 2));

    return sprintf(buf, "Alarm1 set for: %02x:%02x:%02x\n", bin2bcd(set_hour), bin2bcd(set_min), bin2bcd(set_sec));

}

// Function to handle setting the RTC alarm through sysfs
static ssize_t alarm_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    unsigned int hour, min, sec;

    printk(KERN_INFO "Sysfs - Alarm Write!!!\n");

    ret = sscanf(buf, "set alarm1 after: %u:%u:%u", &hour, &min, &sec);
    
    // Define the RTC alarm attribute for sysfs
    if (ret != 3) {
        printk(KERN_ERR "Invalid time/date format\n");
        return -EINVAL;
    }
    
    DS3231_SetAlarm1After(bcd2bin(hour), bcd2bin(min), bcd2bin(sec));

    return count;
}

static struct kobj_attribute alarm_attr = __ATTR(alarm_time, 0660, alarm_sysfs_show, alarm_sysfs_store);
/* sysfs end */

/* IOCTL start*/

// Structure to hold RTC time and date values
struct rtc_value {
	unsigned char usr_hour, usr_min, usr_sec;
	unsigned char usr_day, usr_date, usr_month, usr_year;
};

// Structure to hold alarm time values
struct alm_value {
	unsigned char alm_hour, alm_min, alm_sec;
};

// IOCTL commands for RTC operations
#define WR_RTC_TIME _IOW('a', 1, struct rtc_value)
#define RD_RTC_TIME _IOR('a', 2, struct rtc_value)
#define WR_ALM1_TIME _IOW('a', 3,struct alm_value)
#define RD_ALM1_TIME _IOR('a', 4, struct alm_value)

// Device number and class
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

// Function prototypes for file operations
static int rtc_open(struct inode *inode, struct file *file);
static int rtc_release(struct inode *inode, struct file *file);
static ssize_t rtc_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t rtc_write(struct file *filp, const char *buf, size_t len, loff_t * off);
static long rtc_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

// File operations structure
static struct file_operations fops =
{
	.owner          = THIS_MODULE,
	.read           = rtc_read,
	.write          = rtc_write,
	.open           = rtc_open,
	.unlocked_ioctl = rtc_ioctl,
	.release        = rtc_release,
};

// Open function for the device file
static int rtc_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "Device File Opened...!!!\n");
	return 0;
}

// Release function for the device file
static int rtc_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "Device File Closed...!!!\n");
	return 0;
}

// Read function for the device file
static ssize_t rtc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "Read Function\n");
	return 0;
}

// Write function for the device file
static ssize_t rtc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "Write function\n");
	return 0;
}

// IOCTL function for handling IOCTL commands
static long rtc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    printk(KERN_INFO "IOCTL function\n");
    switch (cmd) {

	case WR_RTC_TIME:
	{ 
            struct rtc_value data;
	    
	    // Copy RTC time and date values from user space
	    if (copy_from_user(&data, (struct rtc_value *)arg, sizeof(struct rtc_value))) {
                return -EFAULT;
            }
            
	    current_hour = data.usr_hour;
	    current_min = data.usr_min;
	    current_sec = data.usr_sec;
	    current_day = data.usr_day;
	    current_date = data.usr_date;
	    current_month = data.usr_month;
	    current_year = data.usr_year;
    
	    // Set DS3231 time and date to match the system time and date
    	    if (DS3231_SetTime(current_hour, current_min, current_sec) < 0) {
        	pr_err("Failed to set time\n");
                return -EFAULT;
    	    }
    
    	    if (DS3231_SetDate(current_day, current_date, current_month, current_year) < 0) {
            	pr_err("Failed to set date\n");
                return -EFAULT;
    	    }

	    pr_info("Current time is updated");
        
	}
	    break;

	case RD_RTC_TIME:
	{   
            struct rtc_value data;
	    
	    //read RTC time and date values
    	    DS3231_GetTime(&current_hour, &current_min, &current_sec);
    	    DS3231_GetDate(&current_day, &current_date, &current_month, &current_year);
	   
	    data.usr_hour = current_hour;
	    data.usr_min = current_min;
	    data.usr_sec = current_sec;
	    data.usr_day = current_day;
	    data.usr_date = current_date;
	    data.usr_month = current_month;
	    data.usr_year = current_year;

	    // Copy RTC time and date values to user space
    	    if (copy_to_user((struct rtc_value *)arg, &data, sizeof(struct rtc_value))) {
                return -EFAULT;
    	    }
	    pr_info("Time Read by user"); 
    	}
	    break;
	
	case WR_ALM1_TIME:
	{ 
            struct alm_value data;
	    unsigned char alm_hour, alm_min, alm_sec;
	    
	    // Copy alarm time values from user space
	    if (copy_from_user(&data, (struct alm_value *)arg, sizeof(struct alm_value))) {
                return -EFAULT;
            }
            
	    alm_hour = data.alm_hour;
	    alm_min = data.alm_min;
	    alm_sec = data.alm_sec;
    
	    // Set alarm from
    	    DS3231_SetAlarm1After(alm_hour, alm_min, alm_sec);
    
	    pr_info("Alarm1 is set");
        
	}
	    break;

	case RD_ALM1_TIME:
	{   
            struct alm_value data;
    
	    // Read alarm time values
	    data.alm_sec = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 0));
    	    data.alm_min  = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 1));
      	    data.alm_hour  = bcd2bin(DS3231_Read(RTC_ALM1_REG_ADDR + 2));
    	    
    	    pr_info("Alarm 1 set for: %02x:%02x:%02x\n", bin2bcd(data.alm_hour), bin2bcd(data.alm_min), bin2bcd(data.alm_sec));

	    // Copy alarm time values to user space
    	    if (copy_to_user((struct alm_value *)arg, &data, sizeof(struct alm_value))) {
                return -EFAULT;
    	    }
	    pr_info("Alarm1 Read"); 
    	}
	    break;

        default:
	    pr_info("invalid IOCTL command from user");
            return -ENOTTY;
    }
	return 0;
}

/* IOCTL end */

static int __init ds3231_init(void)
{
    int ret = -1;
    unsigned char status;
        //IOCTL init start
        if((alloc_chrdev_region(&dev, 0, 1, SLAVE_DEVICE_NAME)) <0) {
		printk(KERN_INFO "Cannot allocate major number\n");
		return -1;
	}
	printk(KERN_INFO "Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));

	/* Creating cdev structure */
	cdev_init(&etx_cdev, &fops);

	/* Adding character device to the system */
	if((cdev_add(&etx_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system\n");
		goto r_cdev;
	}

	/* Creating struct class */
	if((dev_class = class_create(THIS_MODULE, CLASS_NAME)) == NULL) {
		printk(KERN_INFO "Cannot create the struct class\n");
		goto r_class;
	}

	/* Creating device */
	if((device_create(dev_class, NULL, dev, NULL, SLAVE_DEVICE_NAME)) == NULL) {
		printk(KERN_INFO "Cannot create the Device 1\n");
		goto r_device;
	}
        //IOCTL init end

    //sysfs init starit-Creating a directory in /sys/kernel/ 
    kobj_ref = kobject_create_and_add("rtc_sysfs", kernel_kobj);
    if (!kobj_ref) {
        printk(KERN_ERR "Failed to create kobject\n");
        return -ENOMEM;
    }
    //Creating sysfs file
    ret = sysfs_create_file(kobj_ref, &rtc_attr.attr);
    if (ret) {
        printk(KERN_ERR "Failed to create rtc_time sysfs file\n");
        kobject_put(kobj_ref);
        return ret;
    }

    ret = sysfs_create_file(kobj_ref, &alarm_attr.attr);
    if (ret) {
        printk(KERN_ERR "Failed to create alarm_time sysfs file\n");
        sysfs_remove_file(kobj_ref, &rtc_attr.attr);
        kobject_put(kobj_ref);
        return ret;
    }
    //sysfs init end

    // procfs init start
    rtc_proc_file = proc_create("rtc_time", 0444, NULL, &rtc_proc_fops);
    if (!rtc_proc_file) {
        return -ENOMEM;
    }
    //procfs init end
    
    rtc_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
    pr_info("Init Started");
    if (rtc_i2c_adapter != NULL) {
        rtc_i2c_client = i2c_new_device(rtc_i2c_adapter, &rtc_i2c_board_info);
        
        if (rtc_i2c_client != NULL) {
            i2c_add_driver(&ds3231_driver);
            ret = 0;
        }
        
        i2c_put_adapter(rtc_i2c_adapter);
    }

     // Initialize the workqueue
    ds3231_wq = create_singlethread_workqueue("ds3231_wq");
    if (!ds3231_wq) {
        pr_err("Failed to create workqueue\n");
        return -ENOMEM;
    }
    INIT_WORK(&ds3231_work, ds3231_work_handler);

     //Input GPIO configuration
   //Checking the GPIO is valid or not
   if(gpio_is_valid(DS3231_ALARM_GPIO_PIN) == false){
     pr_err("GPIO %d is not valid\n", DS3231_ALARM_GPIO_PIN);
     goto r_gpio_in;
   }
   pr_info("GPIO valid");

   //Requesting the GPIO
   if(gpio_request(DS3231_ALARM_GPIO_PIN,"GPIO_INT_PIN") < 0){
     pr_err("ERROR: GPIO %d request\n",DS3231_ALARM_GPIO_PIN);
     goto r_gpio_in;
   }
   pr_info("IRQ request accept");

   //configure the GPIO as input
   gpio_direction_input(DS3231_ALARM_GPIO_PIN);

   //Get the IRQ number for our GPIO
   GPIO_irqNumber = gpio_to_irq(DS3231_ALARM_GPIO_PIN);
   pr_info("GPIO_irqNumber = %d\n", GPIO_irqNumber);

   status = DS3231_Read(RTC_STAT_REG_ADDR);
   //pr_info("Status register: 0x%02x\n", status);

   if (request_irq(GPIO_irqNumber,                     //IRQ number
                   ds3231_irq_handler,                 //IRQ handler
                   IRQF_TRIGGER_FALLING,               //Handler will be called in raising edge
                   "ds3231_int",                       //used to identify the device name using this IRQ
                   NULL)) {                            //device id for shared IRQ
     pr_err("my_device: cannot register IRQ ");
     goto r_gpio_in;
   }
    pr_info("GPIO IRQ set");

    // pr_info("Init Over");
    pr_info("DS3231 Driver Added!!!\n");
    
    return ret;
r_gpio_in:
         gpio_free(DS3231_ALARM_GPIO_PIN);

r_device:
	class_destroy(dev_class);
r_class:
	cdev_del(&etx_cdev);
r_cdev:
	unregister_chrdev_region(dev,1);
	return ret;

}

static void __exit ds3231_exit(void)
{
    // Free the IRQ associated with the GPIO pin used for DS3231 alarm
    free_irq(GPIO_irqNumber, NULL);

    // Free the GPIO pin used for DS3231 alarm
    gpio_free(DS3231_ALARM_GPIO_PIN);
    
    // Destroy the workqueue created for DS3231 operations
    destroy_workqueue(ds3231_wq);    
    
    // Unregister the I2C device driver for DS3231
    i2c_unregister_device(rtc_i2c_client);
    
    // Delete the I2C driver structure for DS3231
    i2c_del_driver(&ds3231_driver);
    
    // Remove the proc file entry created for RTC
    proc_remove(rtc_proc_file);
    
    // Remove the sysfs files associated with the RTC and alarm attributes
    sysfs_remove_file(kobj_ref, &rtc_attr.attr);
    sysfs_remove_file(kobj_ref, &alarm_attr.attr);
    
    // Decrement the reference count of the kobject and possibly free it
    kobject_put(kobj_ref);
    
    // Destroy the device node associated with the class and the device class itself
    device_destroy(dev_class,dev);
    class_destroy(dev_class);
    
    // Delete the character device structure
    cdev_del(&etx_cdev);
    
    // Unregister the allocated character device region
    unregister_chrdev_region(dev, 1);
    pr_info("DS3231 Driver Removed!!!\n");
}


module_init(ds3231_init);
module_exit(ds3231_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Viral patel");
MODULE_DESCRIPTION("DS3231-RTC Driver");
MODULE_VERSION("1.0");

