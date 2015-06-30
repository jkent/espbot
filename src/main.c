#include <esp8266.h>
#include "bot.h"

#ifdef USE_SECURE
unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;
#endif

static bot_data bot;

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
bot_stop_callback(bot_data *bot, bot_stop_reason reason)
{
	if (reason != BOT_STOP_DNSFAIL) {
		bot_connect(bot);
	}
}

static void ICACHE_FLASH_ATTR
wifiEventHandlerCb(System_Event_t *evt)
{
	if (evt->event == EVENT_STAMODE_GOT_IP) {
		os_bzero(&bot, sizeof(bot));
		bot.host = "jkent.net";
#ifdef USE_SECURE
		bot.tcp.remote_port = 6697;
		bot.secure = true;
#else
		bot.tcp.remote_port = 6667;
#endif
		bot.directed_triggers = true;
		bot.autojoin_channels = "#espbot";
		bot.stop_callback = bot_stop_callback;
		bot_connect(&bot);
	}
}

void ICACHE_FLASH_ATTR user_init(void)
{
	stdout_init();
	printf("\n");

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
