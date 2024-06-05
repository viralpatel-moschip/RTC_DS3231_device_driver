#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

struct rtc_value {
    unsigned char usr_hour, usr_min, usr_sec;
    unsigned char usr_day, usr_date, usr_month, usr_year;
};

struct alm_value {
    unsigned char alm_hour, alm_min, alm_sec;
};

#define WR_RTC_TIME _IOW('a', 1, struct rtc_value)
#define RD_RTC_TIME _IOR('a', 2, struct rtc_value)
#define WR_ALM1_TIME _IOW('a', 3,struct alm_value)
#define RD_ALM1_TIME _IOR('a', 4, struct alm_value)

// Function to convert BCD to binary
static unsigned char bcd2bin(unsigned char val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

unsigned char bin2bcd(unsigned char bin)
{
    return ((bin / 10) << 4) + (bin % 10);
}


int main() {
    int fd;
    int choice;
    struct rtc_value rtc_data;
    struct alm_value alm_data;

    printf("Opening RTC Driver...\n");
    fd = open("/dev/DS3231", O_RDWR);
    if(fd < 0) {
        perror("Failed to open the device file");
        return errno;
    }

    while(1) {
        // Display menu
        printf("\nSelect an option:\n");
        printf("1. Read RTC Time\n");
        printf("2. Write RTC Time\n");
        printf("3. Read Alarm 1 Time\n");
        printf("4. Write Alarm 1 Time\n");
        printf("5. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);

        switch(choice) {
            case 1: // Read RTC Time
                
		printf("Reading RTC Time...\n");
               
		if(ioctl(fd, RD_RTC_TIME, &rtc_data) < 0) {
                    perror("Failed to read RTC time");
                    break;
                }

		printf("RTC Time: %02x:%02x:%02x\nRTC Date: %02x/%02x/20%02x (Day of week: %02x)\n", 
	        rtc_data.usr_hour, rtc_data.usr_min, rtc_data.usr_sec, 
		  rtc_data.usr_date, rtc_data.usr_month, rtc_data.usr_year, rtc_data.usr_day);
                break;


            case 2: // Write RTC Time
                
		printf("Enter new RTC Time (hh mm ss dd mm yy day): ");
                
		scanf("%hhu %hhu %hhu %hhu %hhu %hhu %hhu", 
			&rtc_data.usr_hour, &rtc_data.usr_min, &rtc_data.usr_sec, 
			  &rtc_data.usr_date, &rtc_data.usr_month, &rtc_data.usr_year, &rtc_data.usr_day);
                
		printf("Writing RTC Time...\n");

		if(ioctl(fd, WR_RTC_TIME, &rtc_data) < 0) {
                    perror("Failed to write RTC time");
                }

                break;
            
	    case 3: // Read Alarm 1 Time
                
		printf("Reading Alarm 1 Time...\n");
		
		if(ioctl(fd, RD_ALM1_TIME, &alm_data) < 0) {
                    perror("Failed to read Alarm 1 time");
                    break;
                }

		printf("Alarm 1 Time: %02x:%02x:%02x\n", bin2bcd(alm_data.alm_hour), bin2bcd(alm_data.alm_min), bin2bcd(alm_data.alm_sec));
                
		break;
            
	    case 4: // Write Alarm 1 Time
                
		printf("Enter time after which you want to set the alarm (hh mm ss): ");

                scanf("%hhu %hhu %hhu", &alm_data.alm_hour, &alm_data.alm_min, &alm_data.alm_sec);
                printf("Setting Alarm 1 Time...\n");

		if(ioctl(fd, WR_ALM1_TIME, &alm_data) < 0) {
                    perror("Failed to write Alarm 1 time");
                }
               
	       	break;
            
	    case 5: // Exit
                printf("Closing RTC Driver\n");
                close(fd);
           	return 0;
           
	    default:
                printf("Invalid choice. Please try again.\n");
        }
    }

    return 0;
}

