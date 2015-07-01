#include <esp8266.h>
#include "bot.h"

bot_data *bots[MAX_BOTS];

bridge_data bridge;

#define DEBUG

void ICACHE_FLASH_ATTR
bot_stop_callback(bot_data *bot, bot_stop_reason reason)
{
	if (reason != BOT_STOP_DNSFAIL && reason != BOT_STOP_TERMINATED) {
		bot_connect(bot);
	} else {
		free(bot->host);
		if (bot->autojoin_channels) {
			free(bot->autojoin_channels);
		}
		free(bot);
	}
}

static void ICACHE_FLASH_ATTR __attribute__ ((format (printf, 2, 3)))
irc_send(bot_data *bot, const char *fmt, ...)
{
	va_list ap;
	char buf[IRC_MSGLEN + 2];
	char *p;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, IRC_MSGLEN + 1, fmt, ap);
	va_end(ap);

#ifdef DEBUG
	printf("<< %s\n", buf);
#endif

	p = buf + len;
	*p++ = '\r';
	*p++ = '\n';
	len += 2;

#ifdef USE_SECURE
	if (bot->secure) {
		espconn_secure_sent(&bot->conn, (uint8 *)buf, len);
		return;
	}
#endif

	espconn_sent(&bot->conn, (uint8 *)buf, len);
}

/*****************************************************************************
                                   TRIGGERS
*****************************************************************************/

static void ICACHE_FLASH_ATTR
handle_bot_add_trigger(bot_data *bot, irc_message *msg, char *args)
{
	int i;
	bot_data *other;
	char *p, *host, *autojoin = NULL;
	unsigned short port = 6667;

	p = args;
	host = p;
	while (*p && *p != ':' && *p != ' ') {
		p++;
	}
	if (*p == ':') {
		port = atoi(p + 1);
		while (*p && *p != ' ') {
			p++;
		}
	}

	if (*p) {
		*p++ = '\0';
		autojoin = p;
		while (*p && *p != ' ') {
			p++;
		}
	}

	*p = '\0';

	if (strlen(host) == 0) {
		irc_send(bot, "PRIVMSG %s :Usage: bot add HOST[:PORT] [CHANNELS]",
		         msg->reply);
		return;
	}

	printf("addbot %s:%d %s\n", host, port, autojoin);

	if (autojoin && strlen(autojoin) == 0) {
		autojoin = NULL;
	}

	for (i = 0; i < MAX_BOTS; i++) {
		if (!bots[i]) {
			break;
		}
	}

	if (i == MAX_BOTS) {
		irc_send(bot, "PRIVMSG %s :No more slots available!", msg->reply);
		return;
	}

	other = zalloc(sizeof(bot_data));
	bots[i] = other;
	other->index = i;
	other->host = strdup(host);
	other->tcp.remote_port = 6667;
	other->directed_triggers = true;
	other->autojoin_channels = strdup(autojoin);
	other->stop_callback = bot_stop_callback;
	bot_connect(other);

	irc_send(bot, "PRIVMSG %s :bot %d spawned", msg->reply, i);
}

static void ICACHE_FLASH_ATTR
handle_bot_del_trigger(bot_data *bot, irc_message *msg, char *args)
{
	int i;
	bot_data *other;

	if (!*args) {
		irc_send(bot, "PRIVMSG %s :Usage: bot del SLOT", msg->reply);
		return;
	}

	i = atoi(args);

	if (i < 0 || i >= MAX_BOTS) {
		irc_send(bot, "PRIVMSG %s :bot slot out of range!", msg->reply);
		return;
	}

	if (!bots[i]) {
		irc_send(bot, "PRIVMSG %s :slot %d is already empty!", msg->reply, i);
		return;
	}

	other = bots[i];
	other->state = BOT_STATE_TERMINATING;
	irc_send(other, "QUIT :I've been destroyed!");
	bots[i] = NULL;

	irc_send(bot, "PRIVMSG %s :bot %d destroyed", msg->reply, i);
}

static void ICACHE_FLASH_ATTR
handle_bot_list_trigger(bot_data *bot, irc_message *msg, char *args)
{
	int i;
	bot_data *other;

	for (i = 0; i < MAX_BOTS; i++) {
		other = bots[i];
		if (!other) {
			continue;
		}

		irc_send(bot, "PRIVMSG %s :id: %d, server: %s:%d, nick: %s",
		         msg->reply, i, other->host, other->tcp.remote_port,
		         other->nick.current);
	}
}

static void ICACHE_FLASH_ATTR
handle_bot_trigger(bot_data *bot, irc_message *msg, const char *name,
                   char *args)
{
	char *p = args;

	while (*p && *p != ' ') {
		p++;
	}
	if (*p == ' ') {
		*p++ = '\0';
	}
	while (*p == ' ') {
		p++;
	}

	if (strcasecmp(args, "add") == 0) {
		handle_bot_add_trigger(bot, msg, p);
	} else if (strcasecmp(args, "del") == 0) {
		handle_bot_del_trigger(bot, msg, p);
	} else if (strcasecmp(args, "list") == 0) {
		handle_bot_list_trigger(bot, msg, p);
	} else {
		irc_send(bot, "PRIVMSG %s :Usage: bot {add|del|list}", msg->reply);
	}
}

static void ICACHE_FLASH_ATTR
handle_bridge_trigger(bot_data *bot, irc_message *msg, const char *name,
                      char *args)
{
	char *p, *channel;

	if (bridge.channel1 || bridge.channel2) {
		if (bridge.channel1) {
			free(bridge.channel1);
			bridge.channel1 = NULL;
		}
		if (bridge.channel2) {
			free(bridge.channel2);
			bridge.channel2 = NULL;
		}
		irc_send(bot, "PRIVMSG %s :bridge removed", msg->reply);
	}

	p = args;
	bridge.bot1 = atoi(p);
	while (*p && *p != ':') {
		p++;
	}
	if (*p == ':') {
		*p++ = '\0';
	}
	channel = p;
	while (*p && *p != ' ') {
		p++;
	}
	if (*p == ' ') {
		*p++ = '\0';
	}
	bridge.channel1 = *channel ? strdup(channel) : NULL;
	bridge.bot2 = atoi(p);
	while (*p && *p != ':') {
		p++;
	}
	if (*p == ':') {
		*p++ = '\0';
	}
	channel = p;
	while (*p && *p != ' ') {
		p++;
	}
	if (*p == ' ') {
		*p++ = '\0';
	}
	bridge.channel2 = *channel ? strdup(channel) : NULL;

	if (bridge.bot1 >= MAX_BOTS || bridge.channel1 == NULL ||
	    bridge.bot2 >= MAX_BOTS || bridge.channel2 == NULL) {
		if (bridge.channel1) {
			free(bridge.channel1);
		}
		if (bridge.channel2) {
			free(bridge.channel2);
		}
		return;
	}

	irc_send(bot, "PRIVMSG %s :bridge created (%d:%s <-> %d:%s)", msg->reply,
	         bridge.bot1, bridge.channel1, bridge.bot2, bridge.channel2);
}

static void ICACHE_FLASH_ATTR
handle_nick_trigger(bot_data *bot, irc_message *msg, const char *name,
                    char *args)
{
	char *p = args;

	while (*p && *p != ' ') {
		p++;
	}
	*p = '\0';

	bot->nick.failures = 0;
	strncpy(bot->nick.desired, args, IRC_NICKLEN);
	irc_send(bot, "NICK %s", args);
}

static void ICACHE_FLASH_ATTR
handle_raw_trigger(bot_data *bot, irc_message *msg, const char *name,
                   char *args)
{
	irc_send(bot, "%s", args);
}

static irc_trigger triggers[] = {
	{"bot",    handle_bot_trigger   },
	{"bridge", handle_bridge_trigger},
	{"nick",   handle_nick_trigger  },
	{"raw",    handle_raw_trigger   },
	{NULL,     NULL                 }
};

static void ICACHE_FLASH_ATTR
handle_trigger(bot_data *bot, irc_message *msg, const char *s)
{
	char buf[IRC_MSGLEN - 9];
	char *args, *p;
	irc_trigger *trigger;

	strcpy(buf, s);
	p = buf;
	while (*p && *p != ' ') {
		if (*p >= 'A' && *p <= 'Z') {
			*p += 32;
		}
		p++;
	}
	args = p;
	while (*args == ' ') {
		args++;
	}
	*p = '\0';

	trigger = triggers;
	while (trigger->name) {
		if (strcasecmp(buf, trigger->name) != 0) {
			trigger++;
			continue;
		}

		trigger->handler(bot, msg, buf, args);
		return;
	}
}

/*****************************************************************************
*****************************************************************************/

static void ICACHE_FLASH_ATTR
snatch_nick(bot_data *bot, const char *seen_nick)
{
	if (strcasecmp(bot->nick.current, bot->nick.desired) != 0) {
		if (strcasecmp(bot->nick.desired, seen_nick) == 0) {
			irc_send(bot, "NICK %s", bot->nick.desired);
		}
	}
}

static void ICACHE_FLASH_ATTR
handle_bridge(bot_data *bot, irc_message *msg)
{
	if (!bots[bridge.bot1] || !bridge.channel1 || !bots[bridge.bot2] ||
	    !bridge.channel2 ) {
		if (bridge.channel1) {
			free(bridge.channel1);
			bridge.channel1 = NULL;
		}
		if (bridge.channel2) {
			free(bridge.channel2);
			bridge.channel2 = NULL;
		}
		return;
	}

	if ((bot->index == bridge.bot1) &&
	    strcasecmp(msg->param[0], bridge.channel1) == 0) {
		irc_send(bots[bridge.bot2], "%s %s :<%s:%s> %s", msg->command,
		         bridge.channel2, msg->param[0], msg->source, msg->param[1]);
	} else if ((bot->index == bridge.bot2) &&
	           strcasecmp(msg->param[0], bridge.channel2) == 0) {
		irc_send(bots[bridge.bot1], "%s %s :<%s:%s> %s", msg->command,
		         bridge.channel1, msg->param[0], msg->source, msg->param[1]);
	}
}

/*****************************************************************************
                                   COMMANDS
*****************************************************************************/

static void ICACHE_FLASH_ATTR
handle_001_command(bot_data *bot, irc_message *msg)
{
	if (!msg->source) {
		return;
	}

	bot->state = BOT_STATE_REGISTERED;

	strncpy(bot->server, msg->source, IRC_HOSTLEN);
	bot->server[IRC_HOSTLEN] = '\0';

	strncpy(bot->nick.current, msg->param[0], IRC_NICKLEN);
	bot->nick.current[IRC_NICKLEN] = '\0';

	if (bot->autojoin_channels) {
		irc_send(bot, "JOIN %s", bot->autojoin_channels);
	}
}

static void ICACHE_FLASH_ATTR
handle_433_command(bot_data *bot, irc_message *msg)
{
	int n, digits;
	char nick[IRC_NICKLEN + 1];

	if (bot->nick.ignore_433) {
		bot->nick.ignore_433 = false;
		return;
	}

	n = bot->nick.failures;
	digits = 1;
	while (n > 9) {
		n /= 10;
		digits++;
	}

	strncpy(nick, bot->nick.desired, IRC_NICKLEN - digits);
	sprintf(nick + strlen(nick), "%d", bot->nick.failures++);
	irc_send(bot, "NICK %s", nick);
}

static void ICACHE_FLASH_ATTR
handle_error_command(bot_data *bot, irc_message *msg)
{
	if (bot->state != BOT_STATE_KILLED &&
	    bot->state != BOT_STATE_TERMINATING) {
		bot->state = BOT_STATE_QUIT;
	}
}

static void ICACHE_FLASH_ATTR
handle_kill_command(bot_data *bot, irc_message *msg)
{
	snatch_nick(bot, msg->param[0]);

	if (strncasecmp(bot->nick.current, msg->param[0], IRC_NICKLEN) == 0) {
		bot->state = BOT_STATE_KILLED;
	}
}		 

static void ICACHE_FLASH_ATTR
handle_nick_command(bot_data *bot, irc_message *msg)
{
	if (!msg->source) {
		return;
	}

	if (strncasecmp(bot->nick.current, msg->source, IRC_NICKLEN) == 0) {
		strncpy(bot->nick.current, msg->param[0], IRC_NICKLEN);
		bot->nick.current[IRC_NICKLEN] = '\0';

		if (strcasecmp(bot->nick.desired, msg->param[0]) == 0) {
			bot->nick.failures = 0;
		}
		bot->nick.snatch_timer = 0;
		bot->nick.ignore_433 = false;

	} else {
		snatch_nick(bot, msg->source);
	}
}		 

static void ICACHE_FLASH_ATTR
handle_ping_command(bot_data *bot, irc_message *msg)
{
	irc_send(bot, "PONG :%s", msg->param[0]);
}

static void ICACHE_FLASH_ATTR
handle_privmsg_command(bot_data *bot, irc_message *msg)
{
	int nicklen;
	char *p = msg->param[1];

	if (bot->directed_triggers) {
		if (strcasecmp(msg->param[0], bot->nick.current) == 0) {
			handle_trigger(bot, msg, p);
		} else {
			nicklen = strlen(bot->nick.current);
			if (strncasecmp(p, bot->nick.current, nicklen) == 0) {
				p += nicklen;
				if (*p == ':' || *p == ',') {
					while (*++p == ' ');
					handle_trigger(bot, msg, p);
				}
			}
		}
	} else {
		if (*p == '!') {
			handle_trigger(bot, msg, p + 1);
		} else if (strcasecmp(msg->param[0], bot->nick.current) == 0) {
			handle_trigger(bot, msg, p);
		}
	}

	handle_bridge(bot, msg);
}

static void ICACHE_FLASH_ATTR
handle_quit_command(bot_data *bot, irc_message *msg)
{
	if (!msg->source) {
		return;
	}

	snatch_nick(bot, msg->source);
}

static irc_command irc_commands[] = {
	{"001",     2, handle_001_command    },
	{"433",     3, handle_433_command    },
	{"ERROR",   1, handle_error_command  },
	{"KILL",    2, handle_kill_command   },
	{"NICK",    1, handle_nick_command   },
	{"PING",    1, handle_ping_command   },
	{"PRIVMSG", 2, handle_privmsg_command},
	{"QUIT",    1, handle_quit_command   },
	{NULL,      0, NULL                  }
};

static void ICACHE_FLASH_ATTR
handle_command(bot_data *bot, irc_message *msg)
{
	irc_command *command = irc_commands;

	while (command->name) {
		if (strcmp(msg->command, command->name) != 0) {
			command++;
			continue;
		}

		if (msg->params < command->min_params) {
			command++;
			continue;
		}

		command->handler(bot, msg);
		return;
	}
}

/*****************************************************************************
*****************************************************************************/

static void ICACHE_FLASH_ATTR
bot_stop(bot_data *bot, bot_stop_reason reason)
{
	os_timer_disarm(&bot->timer);
	if (bot->stop_callback) {
		bot->stop_callback(bot, reason);
	}
}

static bool ICACHE_FLASH_ATTR
parse_message(char *s, irc_message *msg)
{
	char *p = s;

	os_bzero(msg, sizeof(irc_message));

	if (*p == ':') {
		msg->source = ++p;
	} else if (*p) {
		msg->command = p;
		if (*p >= 'a' && *p <= 'z') {
			*p -= 32;
		}
		p++;
	}

	while (*p) {
		if (*p == ' ') {
			*p++ = '\0';
			continue;
		}

		if (*(p - 1) == '\0') {
			if (!msg->command) {
				msg->command = p;
			} else {
				if (*p == ':') {
					msg->param[msg->params++] = p + 1;
					break;
				} else {
					msg->param[msg->params++] = p;
					if (msg->params >= IRC_MAX_PARAM) {
						break;
					}
				}
			}
		}

		if (!msg->command) {
			if (!msg->host) {
				if (*p == '!' && !msg->user) {
					msg->user = p + 1;
				} else if (*p == '@') {
					msg->host = p + 1;
				}
			}
		} else if (msg->params == 0) {
			if (*p >= 'a' && *p <= 'z') {
				*p -= 32;
			}
		}
		p++;
	}

	if (msg->user) {
		*(msg->user - 1) = '\0';
	}

	if (msg->host) {
		*(msg->host - 1) = '\0';
	}

	if (msg->command && msg->params > 0 &&
	    (strcmp(msg->command, "PRIVMSG") == 0 ||
	    strcmp(msg->command, "NOTICE") == 0)) {
		msg->reply = (msg->param[0][0] == '#') ? msg->param[0] : msg->source;
	}

	return !!msg->command;
}

/*****************************************************************************
                                  CONNECTION
*****************************************************************************/

static void ICACHE_FLASH_ATTR
recv_callback(void *arg, char *data, unsigned short len)
{
	struct espconn *conn = (struct espconn *)arg;
	bot_data *bot = (bot_data *)conn->reverse;
	irc_message msg;
	char *src, *dst;
	char c;

	bot->last_recv = 0;

	src = data;
	dst = bot->msgbuf + strlen(bot->msgbuf);

	while (src < data + len) {
		c = *src++;
		if (c == 0 || c == '\r') {
			continue;
		}

		if (c == '\n') {
			*dst = 0;
			dst = bot->msgbuf;

#ifdef DEBUG
			printf(">> %s\n", bot->msgbuf);
#endif

			if (parse_message(bot->msgbuf, &msg)) {
				handle_command(bot, &msg);
			}
			continue;
		}

		if (dst < bot->msgbuf + IRC_MSGLEN) {
			*dst++ = c;
		}
	}
	*dst = 0;
}

static void ICACHE_FLASH_ATTR
timer_callback(void *arg)
{
	bot_data *bot = (bot_data *)arg;

	if (bot->state == BOT_STATE_DISCONNECTED) {
		return;
	}

	if (bot->last_recv == BOT_PING_TIME) {
		irc_send(bot, "PING :%s", bot->server);
	}

	if (bot->last_recv == BOT_PING_TIMEOUT) {
#ifdef USE_SECURE
		if (bot->secure) {
			espconn_secure_disconnect(&bot->conn);
		} else {
#endif
			espconn_disconnect(&bot->conn);
#ifdef USE_SECURE
		}
#endif

		bot->state = BOT_STATE_DISCONNECTED;
		bot_stop(bot, BOT_STOP_TIMEOUT);
	}

	bot->last_recv++;

	if (strcasecmp(bot->nick.current, bot->nick.desired) != 0) {
		bot->nick.snatch_timer++;
		if (bot->nick.snatch_timer == 120) {
			bot->nick.snatch_timer = 0;
			bot->nick.ignore_433 = true;
			irc_send(bot, "NICK %s", bot->nick.desired);
		}
	}
}

static void ICACHE_FLASH_ATTR
connect_callback(void *arg)
{
	struct espconn *conn = (struct espconn *)arg;
	bot_data *bot = (bot_data *)conn->reverse;

	bot->state = BOT_STATE_REGISTERING;

	irc_send(bot, "USER espbot 8 * :ESP8266 IRC bot");
	irc_send(bot, "NICK %s", bot->nick.desired);

	os_timer_disarm(&bot->timer);
	os_timer_setfn(&bot->timer, timer_callback, bot);
	os_timer_arm(&bot->timer, 1000, 1);
}

static void ICACHE_FLASH_ATTR
disconnect_callback(void *arg)
{
	struct espconn *conn = (struct espconn *)arg;
	bot_data *bot = (bot_data *)conn->reverse;
	bot_stop_reason reason;

	switch (bot->state) {
	case BOT_STATE_KILLED:
		reason = BOT_STOP_KILLED;
		break;
	case BOT_STATE_QUIT:
		reason = BOT_STOP_QUIT;
		break;
	case BOT_STATE_TIMEOUT:
		reason = BOT_STOP_TIMEOUT;
		break;
	case BOT_STATE_TERMINATING:
		reason = BOT_STOP_TERMINATED;
		break;
	default:
		reason = BOT_STOP_DISCONNECT;
		break;
	}

	bot->state = BOT_STATE_DISCONNECTED;
	
	bot_stop(bot, reason);
}

static void ICACHE_FLASH_ATTR
error_callback(void *arg, sint8 err)
{
	struct espconn *conn = (struct espconn *)arg;
	bot_data *bot = (bot_data *)conn->reverse;

	if (err == ESPCONN_TIMEOUT) {
		bot->state = BOT_STATE_TIMEOUT;
	} else {
		printf("error: %p, %d\n", conn, err);
	}

	disconnect_callback(arg);
}

static void ICACHE_FLASH_ATTR
dns_callback(const char *hostname, ip_addr_t *ip, void *arg)
{
	bot_data *bot = (bot_data *)arg;

	if (ip == NULL) {
		bot_stop(bot, BOT_STOP_DNSFAIL);
		return;
	}

	bot->conn.type = ESPCONN_TCP;
	bot->conn.state = ESPCONN_NONE;
	bot->conn.proto.tcp = &bot->tcp;
	bot->conn.reverse = bot;

	bot->tcp.local_port = espconn_port();
	if ((void *)&bot->tcp.remote_ip != (void *)ip) {
		memcpy(&bot->tcp.remote_ip, ip, 4);
	}

	espconn_regist_connectcb(&bot->conn, connect_callback);
	espconn_regist_disconcb(&bot->conn, disconnect_callback);
	espconn_regist_reconcb(&bot->conn, error_callback);
	espconn_regist_recvcb(&bot->conn, recv_callback);

#ifdef USE_SECURE
	if (bot->secure) {
		espconn_secure_set_size(ESPCONN_CLIENT, 5120);
		espconn_secure_connect(&bot->conn);
		return;
	}
#endif

	espconn_connect(&bot->conn);
}

void ICACHE_FLASH_ATTR
bot_connect(bot_data *bot)
{
	bot->state = BOT_STATE_DISCONNECTED;

	if (!bot->nick.desired[0]) {
		strcpy(bot->nick.desired, "espbot");
	}
	bot->nick.failures = 0;

	err_t error = espconn_gethostbyname((struct espconn *)bot, bot->host,
	                                    (ip_addr_t *)&bot->tcp.remote_ip,
	                                    dns_callback);

	if (error == ESPCONN_INPROGRESS) {
		return;
	}

	if (error == ESPCONN_OK) {
		dns_callback(bot->host, (ip_addr_t *)&bot->tcp.remote_ip, bot);
		return;
	}

	dns_callback(bot->host, NULL, bot);
}
