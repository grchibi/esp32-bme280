/**
 * bme280.h
 */

int get_sec_for_alarm_00(void);
void init_sntp(void);
void sleep_deeply(void);
void time_sync_notification_cb(struct timeval*);


// end of bme280.h