/* vi: set sw=4 ts=4: */
/* agetty.c - another getty program for Linux. By W. Z. Venema 1989
   Ported to Linux by Peter Orbaek <poe@daimi.aau.dk>
   This program is freely distributable. The entire man-page used to
   be here. Now read the real man-page agetty.8 instead.

   -f option added by Eric Rasmussen <ear@usfirst.org> - 12/28/95

   1999-02-22 Arkadiusz Mi�kiewicz <misiek@misiek.eu.org>
   - added Native Language Support

   1999-05-05 Thorsten Kranzkowski <dl8bcu@gmx.net>
   - enable hardware flow control before displaying /etc/issue

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <getopt.h>
#include <termios.h>
#include "busybox.h"

#ifdef CONFIG_FEATURE_UTMP
#include <utmp.h>
#endif

#define _PATH_LOGIN     "/bin/login"

 /* If USE_SYSLOG is undefined all diagnostics go directly to /dev/console. */
#ifdef CONFIG_SYSLOGD
#include <sys/param.h>
#include <syslog.h>
#endif


 /*
  * Some heuristics to find out what environment we are in: if it is not
  * System V, assume it is SunOS 4.
  */

#ifdef LOGIN_PROCESS                    /* defined in System V utmp.h */
#define SYSV_STYLE                      /* select System V style getty */
#ifdef CONFIG_FEATURE_WTMP
extern void updwtmp(const char *filename, const struct utmp *ut);
#endif
#endif  /* LOGIN_PROCESS */

 /*
  * Things you may want to modify.
  *
  * You may disagree with the default line-editing etc. characters defined
  * below. Note, however, that DEL cannot be used for interrupt generation
  * and for line editing at the same time.
  */

#ifdef  SYSV_STYLE
#include <sys/utsname.h>
#include <time.h>
#endif

 /* If ISSUE is not defined, agetty will never display the contents of the
  * /etc/issue file. You will not want to spit out large "issue" files at the
  * wrong baud rate.
  */
#define ISSUE "/etc/issue"              /* displayed before the login prompt */

/* Some shorthands for control characters. */

#define CTL(x)          (x ^ 0100)      /* Assumes ASCII dialect */
#define CR              CTL('M')        /* carriage return */
#define NL              CTL('J')        /* line feed */
#define BS              CTL('H')        /* back space */
#define DEL             CTL('?')        /* delete */

/* Defaults for line-editing etc. characters; you may want to change this. */

#define DEF_ERASE       DEL             /* default erase character */
#define DEF_INTR        CTL('C')        /* default interrupt character */
#define DEF_QUIT        CTL('\\')       /* default quit char */
#define DEF_KILL        CTL('U')        /* default kill char */
#define DEF_EOF         CTL('D')        /* default EOF char */
#define DEF_EOL         '\n'
#define DEF_SWITCH      0               /* default switch char */

 /*
  * SunOS 4.1.1 termio is broken. We must use the termios stuff instead,
  * because the termio -> termios translation does not clear the termios
  * CIBAUD bits. Therefore, the tty driver would sometimes report that input
  * baud rate != output baud rate. I did not notice that problem with SunOS
  * 4.1. We will use termios where available, and termio otherwise.
  */

/* linux 0.12 termio is broken too, if we use it c_cc[VERASE] isn't set
   properly, but all is well if we use termios?! */

#ifdef  TCGETS
#undef  TCGETA
#undef  TCSETA
#undef  TCSETAW
#define termio  termios
#define TCGETA  TCGETS
#define TCSETA  TCSETS
#define TCSETAW TCSETSW
#endif

 /*
  * When multiple baud rates are specified on the command line, the first one
  * we will try is the first one specified.
  */

#define FIRST_SPEED     0

/* Storage for command-line options. */

#define MAX_SPEED       10              /* max. nr. of baud rates */

struct options {
	int flags;                      /* toggle switches, see below */
	int timeout;                    /* time-out period */
	char *login;                    /* login program */
	char *tty;                      /* name of tty */
	char *initstring;               /* modem init string */
	char *issue;                    /* alternative issue file */
	int numspeed;                   /* number of baud rates to try */
	int speeds[MAX_SPEED];          /* baud rates to be tried */
};

static const char opt_string[] = "I:LH:f:hil:mt:wn";
#define F_INITSTRING    (1<<0)          /* initstring is set */
#define F_LOCAL         (1<<1)          /* force local */
#define F_FAKEHOST      (1<<2)          /* force fakehost */
#define F_CUSTISSUE     (1<<3)          /* give alternative issue file */
#define F_RTSCTS        (1<<4)          /* enable RTS/CTS flow control */
#define F_ISSUE         (1<<5)          /* display /etc/issue */
#define F_LOGIN         (1<<6)          /* non-default login program */
#define F_PARSE         (1<<7)          /* process modem status messages */
#define F_TIMEOUT       (1<<8)          /* time out */
#define F_WAITCRLF      (1<<9)          /* wait for CR or LF */
#define F_NOPROMPT      (1<<10)         /* don't ask for login name! */

/* Storage for things detected while the login name was read. */

struct chardata {
	int erase;                      /* erase character */
	int kill;                       /* kill character */
	int eol;                        /* end-of-line character */
	int parity;                     /* what parity did we see */
	int capslock;                   /* upper case without lower case */
};

/* Initial values for the above. */

static struct chardata init_chardata = {
	DEF_ERASE,                              /* default erase character */
	DEF_KILL,                               /* default kill character */
	13,                                     /* default eol char */
	0,                                      /* space parity */
	0,                                      /* no capslock */
};

#if 0
struct Speedtab {
	long speed;
	int code;
};

static struct Speedtab speedtab[] = {
	{50, B50},
	{75, B75},
	{110, B110},
	{134, B134},
	{150, B150},
	{200, B200},
	{300, B300},
	{600, B600},
	{1200, B1200},
	{1800, B1800},
	{2400, B2400},
	{4800, B4800},
	{9600, B9600},
#ifdef  B19200
	{19200, B19200},
#endif
#ifdef  B38400
	{38400, B38400},
#endif
#ifdef  EXTA
	{19200, EXTA},
#endif
#ifdef  EXTB
	{38400, EXTB},
#endif
#ifdef B57600
	{57600, B57600},
#endif
#ifdef B115200
	{115200, B115200},
#endif
#ifdef B230400
	{230400, B230400},
#endif
	{0, 0},
};
#endif


#ifdef  SYSV_STYLE
#ifdef CONFIG_FEATURE_UTMP
static void update_utmp(char *line);
#endif
#endif

/* The following is used for understandable diagnostics. */

/* Fake hostname for ut_host specified on command line. */
static char *fakehost = NULL;

/* ... */
#ifdef DEBUGGING
#define debug(s) fprintf(dbf,s); fflush(dbf)
#define DEBUGTERM "/dev/ttyp0"
FILE *dbf;
#else
#define debug(s)                                /* nothing */
#endif


/*
 * output error messages
 */
static void error(const char *fmt, ...) ATTRIBUTE_NORETURN;
static void error(const char *fmt, ...)
{
	va_list va_alist;
	char buf[256];

#ifdef CONFIG_SYSLOGD
	va_start(va_alist, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va_alist);
	openlog(bb_applet_name, 0, LOG_AUTH);
	syslog(LOG_ERR, "%s", buf);
	closelog();
#else
	int fd;
	size_t l;

	snprintf(buf, sizeof(buf), "%s: ", bb_applet_name);
	l = strlen(buf);
	va_start(va_alist, fmt);
	vsnprintf(buf + l, sizeof(buf) - l, fmt, va_alist);
	l = strlen(buf);
	/* truncate if need */
	if((l + 3) > sizeof(buf))
		l = sizeof(buf) - 3;
	/* add \r\n always */
	buf[l++] = '\r';
	buf[l++] = '\n';
	buf[l] = 0;
	if ((fd = open("/dev/console", 1)) >= 0) {
		write(fd, buf, l);
		close(fd);
	}
#endif

	va_end(va_alist);

	(void) sleep((unsigned) 10);    /* be kind to init(8) */
	exit(1);
}



/* bcode - convert speed string to speed code; return 0 on failure */
static int bcode(const char *s)
{
	int r;
	unsigned long value;
	if (safe_strtoul((char *)s, &value)) {
		return -1;
	}
	if ((r = tty_value_to_baud(value)) > 0) {
		return r;
	}
	return 0;
}


/* parse_speeds - parse alternate baud rates */
static void parse_speeds(struct options *op, char *arg)
{
	char *cp;

	debug("entered parse_speeds\n");
	for (cp = strtok(arg, ","); cp != 0; cp = strtok((char *) 0, ",")) {
		if ((op->speeds[op->numspeed++] = bcode(cp)) <= 0)
			error("bad speed: %s", cp);
		if (op->numspeed > MAX_SPEED)
			error("too many alternate speeds");
	}
	debug("exiting parsespeeds\n");
}


/* parse-args - parse command-line arguments */
static void parse_args(int argc, char **argv, struct options *op)
{
	char *ts;

	op->flags = bb_getopt_ulflags(argc, argv, opt_string,
		&(op->initstring), &fakehost, &(op->issue),
		&(op->login), &ts);
	if(op->flags & F_INITSTRING) {
		const char *p = op->initstring;
		char *q;

		q = op->initstring = bb_xstrdup(op->initstring);
		/* copy optarg into op->initstring decoding \ddd
		   octal codes into chars */
		while (*p) {
			if (*p == '\\') {
				p++;
				*q++ = bb_process_escape_sequence(&p);
			} else {
				*q++ = *p++;
			}
		}
		*q = '\0';
	}
	op->flags ^= F_ISSUE;           /* revert flag show /etc/issue */
	if(op->flags & F_TIMEOUT) {
		if ((op->timeout = atoi(ts)) <= 0)
			error("bad timeout value: %s", ts);
	}
	debug("after getopt loop\n");
	if (argc < optind + 2)          /* check parameter count */
		bb_show_usage();

	/* we loosen up a bit and accept both "baudrate tty" and "tty baudrate" */
	if ('0' <= argv[optind][0] && argv[optind][0] <= '9') {
		/* a number first, assume it's a speed (BSD style) */
		parse_speeds(op, argv[optind++]);       /* baud rate(s) */
		op->tty = argv[optind]; /* tty name */
	} else {
		op->tty = argv[optind++];       /* tty name */
		parse_speeds(op, argv[optind]); /* baud rate(s) */
	}

	optind++;
	if (argc > optind && argv[optind])
		setenv("TERM", argv[optind], 1);

	debug("exiting parseargs\n");
}

/* open_tty - set up tty as standard { input, output, error } */
static void open_tty(char *tty, struct termio *tp, int local)
{
	int chdir_to_root = 0;

	/* Set up new standard input, unless we are given an already opened port. */

	if (strcmp(tty, "-")) {
		struct stat st;
		int fd;

		/* Sanity checks... */

		if (chdir("/dev"))
			error("/dev: chdir() failed: %m");
		chdir_to_root = 1;
		if (stat(tty, &st) < 0)
			error("/dev/%s: %m", tty);
		if ((st.st_mode & S_IFMT) != S_IFCHR)
			error("/dev/%s: not a character device", tty);

		/* Open the tty as standard input. */

		close(0);
		debug("open(2)\n");
		fd = open(tty, O_RDWR | O_NONBLOCK, 0);
		if (fd != 0)
			error("/dev/%s: cannot open as standard input: %m", tty);
	} else {

		/*
		 * Standard input should already be connected to an open port. Make
		 * sure it is open for read/write.
		 */

		if ((fcntl(0, F_GETFL, 0) & O_RDWR) != O_RDWR)
			error("%s: not open for read/write", tty);
	}

	/* Replace current standard output/error fd's with new ones */
	debug("duping\n");
	if (dup2(STDIN_FILENO, STDOUT_FILENO) == -1 ||
	    dup2(STDIN_FILENO, STDERR_FILENO) == -1)
		error("%s: dup problem: %m", tty);      /* we have a problem */

	/*
	 * The following ioctl will fail if stdin is not a tty, but also when
	 * there is noise on the modem control lines. In the latter case, the
	 * common course of action is (1) fix your cables (2) give the modem more
	 * time to properly reset after hanging up. SunOS users can achieve (2)
	 * by patching the SunOS kernel variable "zsadtrlow" to a larger value;
	 * 5 seconds seems to be a good value.
	 */

	if (ioctl(0, TCGETA, tp) < 0)
		error("%s: ioctl: %m", tty);

	/*
	 * It seems to be a terminal. Set proper protections and ownership. Mode
	 * 0622 is suitable for SYSV <4 because /bin/login does not change
	 * protections. SunOS 4 login will change the protections to 0620 (write
	 * access for group tty) after the login has succeeded.
	 */

#ifdef DEBIAN
	{
		/* tty to root.dialout 660 */
		struct group *gr;
		int id;

		id = (gr = getgrnam("dialout")) ? gr->gr_gid : 0;
		chown(tty, 0, id);
		chmod(tty, 0660);

		/* vcs,vcsa to root.sys 600 */
		if (!strncmp(tty, "tty", 3) && isdigit(tty[3])) {
			char *vcs, *vcsa;

			if (!(vcs = strdup(tty)))
				error("Can't malloc for vcs");
			if (!(vcsa = malloc(strlen(tty) + 2)))
				error("Can't malloc for vcsa");
			strcpy(vcs, "vcs");
			strcpy(vcs + 3, tty + 3);
			strcpy(vcsa, "vcsa");
			strcpy(vcsa + 4, tty + 3);

			id = (gr = getgrnam("sys")) ? gr->gr_gid : 0;
			chown(vcs, 0, id);
			chmod(vcs, 0600);
			chown(vcsa, 0, id);
			chmod(vcs, 0600);

			free(vcs);
			free(vcsa);
		}
	}
#else
	(void) chown(tty, 0, 0);        /* root, sys */
	(void) chmod(tty, 0622);        /* crw--w--w- */
#endif
	if(chdir_to_root && chdir("/"))
		error("chdir to / failed: %m");
}

/* termio_init - initialize termio settings */
static void termio_init(struct termio *tp, int speed, struct options *op)
{
	/*
	 * Initial termio settings: 8-bit characters, raw-mode, blocking i/o.
	 * Special characters are set after we have read the login name; all
	 * reads will be done in raw mode anyway. Errors will be dealt with
	 * lateron.
	 */
#ifdef __linux__
	/* flush input and output queues, important for modems! */
	(void) ioctl(0, TCFLSH, TCIOFLUSH);
#endif

	tp->c_cflag = CS8 | HUPCL | CREAD | speed;
	if (op->flags & F_LOCAL) {
		tp->c_cflag |= CLOCAL;
	}

	tp->c_iflag = tp->c_lflag = tp->c_line = 0;
	tp->c_oflag = OPOST | ONLCR;
	tp->c_cc[VMIN] = 1;
	tp->c_cc[VTIME] = 0;

	/* Optionally enable hardware flow control */

#ifdef  CRTSCTS
	if (op->flags & F_RTSCTS)
		tp->c_cflag |= CRTSCTS;
#endif

	(void) ioctl(0, TCSETA, tp);

	/* go to blocking input even in local mode */
	fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) & ~O_NONBLOCK);

	debug("term_io 2\n");
}

/* auto_baud - extract baud rate from modem status message */
static void auto_baud(struct termio *tp)
{
	int speed;
	int vmin;
	unsigned iflag;
	char buf[BUFSIZ];
	char *bp;
	int nread;

	/*
	 * This works only if the modem produces its status code AFTER raising
	 * the DCD line, and if the computer is fast enough to set the proper
	 * baud rate before the message has gone by. We expect a message of the
	 * following format:
	 *
	 * <junk><number><junk>
	 *
	 * The number is interpreted as the baud rate of the incoming call. If the
	 * modem does not tell us the baud rate within one second, we will keep
	 * using the current baud rate. It is advisable to enable BREAK
	 * processing (comma-separated list of baud rates) if the processing of
	 * modem status messages is enabled.
	 */

	/*
	 * Use 7-bit characters, don't block if input queue is empty. Errors will
	 * be dealt with lateron.
	 */

	iflag = tp->c_iflag;
	tp->c_iflag |= ISTRIP;          /* enable 8th-bit stripping */
	vmin = tp->c_cc[VMIN];
	tp->c_cc[VMIN] = 0;                     /* don't block if queue empty */
	(void) ioctl(0, TCSETA, tp);

	/*
	 * Wait for a while, then read everything the modem has said so far and
	 * try to extract the speed of the dial-in call.
	 */

	(void) sleep(1);
	if ((nread = read(0, buf, sizeof(buf) - 1)) > 0) {
		buf[nread] = '\0';
		for (bp = buf; bp < buf + nread; bp++) {
			if (isascii(*bp) && isdigit(*bp)) {
				if ((speed = bcode(bp))) {
					tp->c_cflag &= ~CBAUD;
					tp->c_cflag |= speed;
				}
				break;
			}
		}
	}
	/* Restore terminal settings. Errors will be dealt with lateron. */

	tp->c_iflag = iflag;
	tp->c_cc[VMIN] = vmin;
	(void) ioctl(0, TCSETA, tp);
}

/* next_speed - select next baud rate */
static void next_speed(struct termio *tp, struct options *op)
{
	static int baud_index = FIRST_SPEED;    /* current speed index */

	baud_index = (baud_index + 1) % op->numspeed;
	tp->c_cflag &= ~CBAUD;
	tp->c_cflag |= op->speeds[baud_index];
	(void) ioctl(0, TCSETA, tp);
}


/* do_prompt - show login prompt, optionally preceded by /etc/issue contents */
static void do_prompt(struct options *op, struct termio *tp)
{
#ifdef  ISSUE                                   /* optional: show /etc/issue */
	print_login_issue(op->issue, op->tty);
#endif
	print_login_prompt();
}

/* caps_lock - string contains upper case without lower case */
/* returns 1 if true, 0 if false */
static int caps_lock(const char *s)
{
	int capslock;

	for (capslock = 0; *s; s++) {
		if (islower(*s))
			return (0);
		if (capslock == 0)
			capslock = isupper(*s);
	}
	return (capslock);
}

#define logname bb_common_bufsiz1
/* get_logname - get user name, establish parity, speed, erase, kill, eol */
/* return NULL on failure, logname on success */
static char *get_logname(struct options *op, struct chardata *cp, struct termio *tp)
{
	char *bp;
	char c;				/* input character, full eight bits */
	char ascval;                    /* low 7 bits of input character */
	int bits;                       /* # of "1" bits per character */
	int mask;                       /* mask with 1 bit up */
	static char *erase[] = {        /* backspace-space-backspace */
		"\010\040\010",                 /* space parity */
		"\010\040\010",                 /* odd parity */
		"\210\240\210",                 /* even parity */
		"\210\240\210",                 /* no parity */
	};

	/* Initialize kill, erase, parity etc. (also after switching speeds). */

	*cp = init_chardata;

	/* Flush pending input (esp. after parsing or switching the baud rate). */

	(void) sleep(1);
	(void) ioctl(0, TCFLSH, TCIFLUSH);

	/* Prompt for and read a login name. */

	for (*logname = 0; *logname == 0; /* void */ ) {

		/* Write issue file and prompt, with "parity" bit == 0. */

		do_prompt(op, tp);

		/* Read name, watch for break, parity, erase, kill, end-of-line. */

		for (bp = logname, cp->eol = 0; cp->eol == 0; /* void */ ) {

			/* Do not report trivial EINTR/EIO errors. */

			if (read(0, &c, 1) < 1) {
				if (errno == EINTR || errno == EIO)
					exit(0);
				error("%s: read: %m", op->tty);
			}
			/* Do BREAK handling elsewhere. */

			if ((c == 0) && op->numspeed > 1)
				/* return (0); */
				return NULL;

			/* Do parity bit handling. */

			if (c != (ascval = (c & 0177))) {       /* "parity" bit on ? */
				for (bits = 1, mask = 1; mask & 0177; mask <<= 1)
					if (mask & ascval)
						bits++; /* count "1" bits */
				cp->parity |= ((bits & 1) ? 1 : 2);
			}
			/* Do erase, kill and end-of-line processing. */

			switch (ascval) {
			case CR:
			case NL:
				*bp = 0;                /* terminate logname */
				cp->eol = ascval;       /* set end-of-line char */
				break;
			case BS:
			case DEL:
			case '#':
				cp->erase = ascval;     /* set erase character */
				if (bp > logname) {
					(void) write(1, erase[cp->parity], 3);
					bp--;
				}
				break;
			case CTL('U'):
			case '@':
				cp->kill = ascval;      /* set kill character */
				while (bp > logname) {
					(void) write(1, erase[cp->parity], 3);
					bp--;
				}
				break;
			case CTL('D'):
				exit(0);
			default:
				if (!isascii(ascval) || !isprint(ascval)) {
					/* ignore garbage characters */ ;
				} else if (bp - logname >= sizeof(logname) - 1) {
					error("%s: input overrun", op->tty);
				} else {
					(void) write(1, &c, 1); /* echo the character */
					*bp++ = ascval; /* and store it */
				}
				break;
			}
		}
	}
	/* Handle names with upper case and no lower case. */

	if ((cp->capslock = caps_lock(logname))) {
		for (bp = logname; *bp; bp++)
			if (isupper(*bp))
				*bp = tolower(*bp);     /* map name to lower case */
	}
	return (logname);
}

/* termio_final - set the final tty mode bits */
static void termio_final(struct options *op, struct termio *tp, struct chardata *cp)
{
	/* General terminal-independent stuff. */

	tp->c_iflag |= IXON | IXOFF;    /* 2-way flow control */
	tp->c_lflag |= ICANON | ISIG | ECHO | ECHOE | ECHOK | ECHOKE;
	/* no longer| ECHOCTL | ECHOPRT */
	tp->c_oflag |= OPOST;
	/* tp->c_cflag = 0; */
	tp->c_cc[VINTR] = DEF_INTR;     /* default interrupt */
	tp->c_cc[VQUIT] = DEF_QUIT;     /* default quit */
	tp->c_cc[VEOF] = DEF_EOF;       /* default EOF character */
	tp->c_cc[VEOL] = DEF_EOL;
	tp->c_cc[VSWTC] = DEF_SWITCH;   /* default switch character */

	/* Account for special characters seen in input. */

	if (cp->eol == CR) {
		tp->c_iflag |= ICRNL;   /* map CR in input to NL */
		tp->c_oflag |= ONLCR;   /* map NL in output to CR-NL */
	}
	tp->c_cc[VERASE] = cp->erase;   /* set erase character */
	tp->c_cc[VKILL] = cp->kill;     /* set kill character */

	/* Account for the presence or absence of parity bits in input. */

	switch (cp->parity) {
	case 0:                                 /* space (always 0) parity */
		break;
	case 1:                                 /* odd parity */
		tp->c_cflag |= PARODD;
		/* FALLTHROUGH */
	case 2:                                 /* even parity */
		tp->c_cflag |= PARENB;
		tp->c_iflag |= INPCK | ISTRIP;
		/* FALLTHROUGH */
	case (1 | 2):                           /* no parity bit */
		tp->c_cflag &= ~CSIZE;
		tp->c_cflag |= CS7;
		break;
	}
	/* Account for upper case without lower case. */

	if (cp->capslock) {
		tp->c_iflag |= IUCLC;
		tp->c_lflag |= XCASE;
		tp->c_oflag |= OLCUC;
	}
	/* Optionally enable hardware flow control */

#ifdef  CRTSCTS
	if (op->flags & F_RTSCTS)
		tp->c_cflag |= CRTSCTS;
#endif

	/* Finally, make the new settings effective */

	if (ioctl(0, TCSETA, tp) < 0)
		error("%s: ioctl: TCSETA: %m", op->tty);
}


#ifdef  SYSV_STYLE
#ifdef CONFIG_FEATURE_UTMP
/* update_utmp - update our utmp entry */
static void update_utmp(char *line)
{
	struct utmp ut;
	struct utmp *utp;
	time_t t;
	int mypid = getpid();
#if ! (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 1))
	struct flock lock;
#endif

	/*
	 * The utmp file holds miscellaneous information about things started by
	 * /sbin/init and other system-related events. Our purpose is to update
	 * the utmp entry for the current process, in particular the process type
	 * and the tty line we are listening to. Return successfully only if the
	 * utmp file can be opened for update, and if we are able to find our
	 * entry in the utmp file.
	 */
	if (access(_PATH_UTMP, R_OK|W_OK) == -1) {
		close(creat(_PATH_UTMP, 0664));
	}
	utmpname(_PATH_UTMP);
	setutent();
	while ((utp = getutent())
		   && !(utp->ut_type == INIT_PROCESS && utp->ut_pid == mypid))  /* nothing */
		;

	if (utp) {
		memcpy(&ut, utp, sizeof(ut));
	} else {
		/* some inits don't initialize utmp... */
		memset(&ut, 0, sizeof(ut));
		safe_strncpy(ut.ut_id, line + 3, sizeof(ut.ut_id));
	}
	/*endutent(); */

	strcpy(ut.ut_user, "LOGIN");
	safe_strncpy(ut.ut_line, line, sizeof(ut.ut_line));
	if (fakehost)
		safe_strncpy(ut.ut_host, fakehost, sizeof(ut.ut_host));
	time(&t);
	ut.ut_time = t;
	ut.ut_type = LOGIN_PROCESS;
	ut.ut_pid = mypid;

	pututline(&ut);
	endutent();

#ifdef CONFIG_FEATURE_WTMP
	if (access(bb_path_wtmp_file, R_OK|W_OK) == -1)
		close(creat(bb_path_wtmp_file, 0664));
	updwtmp(bb_path_wtmp_file, &ut);
#endif
}

#endif /* CONFIG_FEATURE_UTMP */
#endif /* SYSV_STYLE */


#undef logname
int getty_main(int argc, char **argv)
{
	char *logname = NULL;           /* login name, given to /bin/login */
	struct chardata chardata;       /* set by get_logname() */
	struct termio termio;           /* terminal mode bits */
	static struct options options = {
		0,                      /* show /etc/issue (SYSV_STYLE) */
		0,                      /* no timeout */
		_PATH_LOGIN,            /* default login program */
		"tty1",                 /* default tty line */
		"",                     /* modem init string */
#ifdef ISSUE
		ISSUE,                  /* default issue file */
#else
		NULL,
#endif
		0,                      /* no baud rates known yet */
	};

#ifdef DEBUGGING
	dbf = bb_xfopen(DEBUGTERM, "w");

	{
		int i;

		for (i = 1; i < argc; i++) {
			debug(argv[i]);
			debug("\n");
		}
	}
#endif

	/* Parse command-line arguments. */

	parse_args(argc, argv, &options);

#ifdef __linux__
	setsid();
#endif

	/* Update the utmp file. */


#ifdef  SYSV_STYLE
#ifdef CONFIG_FEATURE_UTMP
	update_utmp(options.tty);
#endif
#endif

	debug("calling open_tty\n");
	/* Open the tty as standard { input, output, error }. */
	open_tty(options.tty, &termio, options.flags & F_LOCAL);

#ifdef __linux__
	{
		int iv;

		iv = getpid();
		ioctl(0, TIOCSPGRP, &iv);
	}
#endif
	/* Initialize the termio settings (raw mode, eight-bit, blocking i/o). */
	debug("calling termio_init\n");
	termio_init(&termio, options.speeds[FIRST_SPEED], &options);

	/* write the modem init string and DON'T flush the buffers */
	if (options.flags & F_INITSTRING) {
		debug("writing init string\n");
		write(1, options.initstring, strlen(options.initstring));
	}

	if (!(options.flags & F_LOCAL)) {
		/* go to blocking write mode unless -L is specified */
		fcntl(1, F_SETFL, fcntl(1, F_GETFL, 0) & ~O_NONBLOCK);
	}

	/* Optionally detect the baud rate from the modem status message. */
	debug("before autobaud\n");
	if (options.flags & F_PARSE)
		auto_baud(&termio);

	/* Set the optional timer. */
	if (options.timeout)
		(void) alarm((unsigned) options.timeout);

	/* optionally wait for CR or LF before writing /etc/issue */
	if (options.flags & F_WAITCRLF) {
		char ch;

		debug("waiting for cr-lf\n");
		while (read(0, &ch, 1) == 1) {
			ch &= 0x7f;                     /* strip "parity bit" */
#ifdef DEBUGGING
			fprintf(dbf, "read %c\n", ch);
#endif
			if (ch == '\n' || ch == '\r')
				break;
		}
	}

	chardata = init_chardata;
	if (!(options.flags & F_NOPROMPT)) {
		/* Read the login name. */
		debug("reading login name\n");
		/* while ((logname = get_logname(&options, &chardata, &termio)) == 0) */
		while ((logname = get_logname(&options, &chardata, &termio)) ==
			   NULL) next_speed(&termio, &options);
	}

	/* Disable timer. */

	if (options.timeout)
		(void) alarm(0);

	/* Finalize the termio settings. */

	termio_final(&options, &termio, &chardata);

	/* Now the newline character should be properly written. */

	(void) write(1, "\n", 1);

	/* Let the login program take care of password validation. */

	(void) execl(options.login, options.login, "--", logname, (char *) 0);
	error("%s: can't exec %s: %m", options.tty, options.login);
}

