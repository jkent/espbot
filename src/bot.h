#ifndef BOT_H
#define BOT_H

#include <stdbool.h>

#define MAX_BOTS 5

#define IRC_NICKLEN 9
#define IRC_HOSTLEN 63
#define IRC_MSGLEN 510
#define IRC_MAX_PARAM 15

#define BOT_PING_TIME 120
#define BOT_PING_TIMEOUT 180

typedef enum bot_stop_reason bot_stop_reason;
typedef enum bot_state bot_state;
typedef struct bot_data bot_data;
typedef void (*bot_stop_cb_t)(bot_data *bot, bot_stop_reason reason);
typedef struct irc_message irc_message;
typedef struct irc_command irc_command;
typedef struct irc_trigger irc_trigger;

enum bot_stop_reason {
	BOT_STOP_DNSFAIL = 0,
	BOT_STOP_TIMEOUT,
	BOT_STOP_DISCONNECT,
	BOT_STOP_KILLED,
	BOT_STOP_QUIT,
	BOT_STOP_TERMINATED,
};

enum bot_state {
	BOT_STATE_DISCONNECTED = 0,
	BOT_STATE_REGISTERING,
	BOT_STATE_REGISTERED,
	BOT_STATE_KILLED,
	BOT_STATE_QUIT,
	BOT_STATE_TIMEOUT,
	BOT_STATE_TERMINATING,
};

struct bot_data {
	unsigned char index;
	struct espconn conn;
	esp_tcp tcp;
	ETSTimer timer;
	char *host;
#ifdef USE_SECURE
	bool secure;
#endif
	struct {
		char desired[IRC_NICKLEN + 1];
		char current[IRC_NICKLEN + 1];
		unsigned char failures;
		unsigned char snatch_timer;
		bool ignore_433;
	} nick;
	bool directed_triggers;
	char *autojoin_channels;
	unsigned char last_recv;
	char server[IRC_HOSTLEN + 1];
	bot_state state;
	bot_stop_cb_t stop_callback;
	char msgbuf[IRC_MSGLEN + 1];
};

struct irc_message {
	char *source;
	char *user;
	char *host;
	char *command;
	int params;
	char *param[IRC_MAX_PARAM];
	char *reply;
};

struct irc_command {
	const char *name;
	unsigned char min_params;
	void (*handler)(bot_data *bot, irc_message *msg);
};

struct irc_trigger {
	const char *name;
	void (*handler)(bot_data *bot, irc_message *msg, const char *name, char *arg);
};

extern bot_data *bots[MAX_BOTS];

void ICACHE_FLASH_ATTR bot_connect(bot_data *bot);
void ICACHE_FLASH_ATTR bot_stop_callback(bot_data *bot, bot_stop_reason reason);
#endif /* BOT_H */
