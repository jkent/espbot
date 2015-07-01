#include <esp8266.h>
#include "bot.h"

#ifdef USE_SECURE
unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;
#endif

//#define SHOW_HEAP_USE

#ifdef SHOW_HEAP_USE
static ETSTimer prHeapTimer;

static void ICACHE_FLASH_ATTR
prHeapTimerCb(void *arg)
{
	printf("Heap: %ld\n", (unsigned long)system_get_free_heap_size());
}
#endif

static void ICACHE_FLASH_ATTR
wifiEventHandlerCb(System_Event_t *evt)
{
	int i;
	bot_data *bot;
	bool active;

	if (evt->event == EVENT_STAMODE_GOT_IP) {
		active = false;
		for (i = 0; i < MAX_BOTS; i++) {
			if (bots[i]) {
				bot_connect(bots[i]);
				active = true;
			}
		}

		if (!active) {
			bot = zalloc(sizeof(bot_data));
			bots[0] = bot;
			bot->index = 0;
			bot->host = strdup("irc.jkent.net");
#ifdef USE_SECURE
			bot->tcp.remote_port = 6697;
			bot->secure = true;
#else
			bot->tcp.remote_port = 6667;
#endif
			bot->directed_triggers = true;
			bot->autojoin_channels = strdup("#espbot");
			bot->stop_callback = bot_stop_callback;
			bot_connect(bot);
		}
	}
}

void ICACHE_FLASH_ATTR user_init(void)
{
	stdout_init();
	printf("\n");

	bzero(bots, sizeof(bots));
	bzero(&bridge, sizeof(bridge));

#ifdef USE_SECURE
	//espconn_secure_ca_enable(0x01, 0x3C);
#endif
	wifi_set_event_handler_cb(wifiEventHandlerCb);
#ifdef SHOW_HEAP_USE
	os_timer_disarm(&prHeapTimer);
	os_timer_setfn(&prHeapTimer, prHeapTimerCb, NULL);
	os_timer_arm(&prHeapTimer, 3000, 1);
#endif
	printf("\nReady\n");
}

#ifndef USE_OPENSDK
void ICACHE_FLASH_ATTR
user_rf_pre_init(void)
{
}
#endif
