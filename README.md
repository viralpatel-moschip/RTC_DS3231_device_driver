## RTC Driver for DS3231

### Overview
This driver provides functionalities to interface with the DS3231 RTC module connected to the BeagleBone Black board. It allows reading and setting the current time and date, setting alarms, and handling interrupts.

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
  - [Sysfs Interface](#sysfs-interface)
  - [Procfs Interface](#procfs-interface)
  - [IOCTL Interface](#ioctl-interface)
- [License](#license)

### Features
- Read and set the current time and date
- Set alarms for specific time
- Handle alarms and interrupt events

### Requirements
- BeagleBone Black running a Linux distribution (tested with Debian).
- I2C enabled in the device tree.
- DS3231 module connected to I2C bus 2. 

### Installation
1. Clone this repository to your BeagleBone Black:
    
    ```bash
    git clone https://github.com/viralpatel-moschip/RTC_DS3231_device_driver.git
    cd RTC_DS3231_device_driver
    ```

2. Build the driver:

    ```bash
    make
    ```

3. Insert the driver module:

    ```bash
    sudo insmod rtc.ko
    ```
    
4. Verify that the device nodes and sysfs entries are created:

    ```bash
    ls /dev/DS3231
    ls /sys/kernel/rtc_sysfs
    ```
5. Properly removes the module after it is no longer needed, ensuring clean and efficient system management.
    ```bash
    sudo rmmod rtc.ko
    ```

## Usage

### Sysfs Interface
The sysfs interface allows you to read and write the RTC time and the alarm time which is stored in device.
- Read the current time of RTC: 
    ```bash
    sudo cat /sys/kernel/rtc_sysfs/rtc_time
    ```

- Write the current time of RTC: 
    ```bash
    sudo su
    echo "set time: <hour>:<min>:<sec>, set date: <dd>/<mm>/<yy>, day of week: <day>" > /sys/kernel/rtc_sysfs/rtc_time
    ```

- Read the current time of Alarm: 
    ```bash
    sudo cat /sys/kernel/rtc_sysfs/alarm_time
    ```

- Write the current time of Alarm: 
    ```bash
    sudo su
    echo "set alarm1 after: <hour>:<min>:<sec>" > /sys/kernel/rtc_sysfs/alarm_time
    ```

### Procfs Interface
The `/proc/rtc_time` interface provides a read-only file that combines the alarm time, RTC time, and the status of the alarm. It offers a convenient way to access this information from the RTC (Real-Time Clock) module.

- Read the current time, date, and alarm status:
 
    ```bash
    cat /proc/rtc_time
    ```

### IOCTL Interface

The ioctl interface allows more advanced control, such as setting and getting the RTC time and Alarm time on the RTC module from a user-space application.

- Example usage in a C program:

    ```bash
    cd app
    sudo ./rtc_test_app
    ```

- Upon running the application, you will be presented with a menu to choose an operation:
    - Read RTC Time
    - Write RTC Time
    - Read Alarm 1 Time
    - Write Alarm 1 Time
    - Exit

- Follow the on-screen prompts to perform the desired operation.

- Ensure proper permissions to access the `/dev/DS3231` file.

##  License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for more details.

---

**Author**: Viral Patel

**Contact**: viral.patel@moschip.com

**Tested with**: Linux BeagleBone Black

---

For any questions or issues, please open an issue in the repository or contact the author.

