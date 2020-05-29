/**
 * bme280.h
 */

uint64_t get_usec_for_alarm(void);
void init_sntp(void);
void time_sync_notification_cb(struct timeval*);


// end of bme280.h