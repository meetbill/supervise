#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include "sig.h"
#include "strerr.h"
#include "error.h"
#include "fifo.h"
#include "open.h"
#include "lock.h"
#include "wait.h"
#include "coe.h"
#include "ndelay.h"
#include "env.h"
#include "iopause.h1"
#include "iopause.h2"
#include "taia.h"
#include "deepsleep.h"
#define NOTICE 0
#define WARNING 1
#define FATAL 2

int selfpipe[2];
int fdlock;
int fdcontrolwrite;
int fdcontrol;
int fdok;
int fdlog;
int fdlogwf;
int flagexit = 0;
int flagwant = 1;
int flagwantup = 1;
int pid = 0; /* 0 means down */
int flagpaused; /* defined if(pid) */
char status[20];

char status_dir[1024];
char status_files[7][1024];
char service[512];
char *cmdp[512];
char cmd[512][1024];
char restart_sh[1024];
char conf_path[1024] = "supervise.conf";

unsigned int alarm_interval;
char alarm_mail[2048];
char alarm_gsm[10][1024];
unsigned int max_tries; 
unsigned int max_tries_if_coredumped;
unsigned int gsm_list_len;

int closealarm;
int have_tried;
int have_coredumped;

//add for ecom
int time_alarm;
time_t time_old;
time_t time_now;
int num;

static void help(char pro[])
{
	printf("Usage: %s -p STATUS_DIR -f COMMAND_STR [-F CONF_PATH] [-r RESTART_SH] [-t TIME_LIMIT] [-v] [-h]\n", pro);
	printf("or Usage: %s [-F CONF_PATH] [-r RESTART_SH] [-t TIME_LIMIT] -u STATUS_DIR COMMAND_STR\n", pro);
	printf("Example: %s -p status/redis -f \" nohup bin/redis-server conf/redis.conf \"\n", pro);
	printf("or Example: %s -u status/redis bin/reids-server conf/redis.conf\n\n", pro);
	
	printf("  -p/-u, the status directory for supervise to write some status files.\n");
	printf("  -f, the command string for supervise to start up a program as its child progress, usually it is our service.\n");
	printf("  -F, the config file path, if not given, supervise will use the default value \"supervise.conf\"\n");
	printf("  -r, while the service exits, supervise will excute the program RESTART_SH before restart the service,	and the exiting times be passed to RESTART_SH as the only argument.\n");
	printf("  -t, after TIME_LIMIT seconds, the counter for the exiting times will be reset.\n");
	printf("  -v, print the version of supervise and exit.\n");
	printf("  -h, print the help message and exit.\n");
	
	printf("\nReport bugs to <meetbill@163.com>\n");
}

static size_t gettimenow(char *timestr, int len)
{
	time_t now;
	struct tm tim;
	time(&now);
	localtime_r(&now, &tim); 
	return strftime(timestr, len, "%Y-%m-%d %H:%M:%S ", &tim);
}

static void write_log(int fd, int level, const char *info0, const char *info1, const char *info2)
{
	struct stat st;
	int fdx, ret;
	char tmp[2048];
	char timestr[128];
	char loglevel[3][16] = { "NOTICE", "WARNING", "FATAL" };
	if (fd == fdlog)
	{
		if ((ret = stat(status_files[5], &st)) < 0 || st.st_size > 1800000000)
		{
			close(fdlog);
			if (ret == 0)
				unlink(status_files[5]);
			fdlog = open_append(status_files[5]);
			coe(fdlog);
		}
		fdx = fdlog;
	}
	else if (fd == fdlogwf)
	{
		if ((ret = stat(status_files[6], &st)) < 0 || st.st_size > 1800000000)
		{
			close(fdlogwf);
			if (ret == 0)
				unlink(status_files[6]);
			fdlogwf = open_append(status_files[6]);
			coe(fdlogwf);
		}
		fdx = fdlogwf;
	}
	else
		return;
	bzero(timestr, 128);
	gettimenow(timestr, 128);
	snprintf(tmp, 2048, "[%s] %s %s%s%s", level >= 0 && level < 3 ? loglevel[level] : "", timestr, info0, info1, info2);
	write(fdx, tmp, strlen(tmp));
}

static void do_alarm(const char* warn_message)
{
	int i;
	char buf[4096];

	if (strlen(alarm_mail) > 0)
	{
		bzero(buf, 4096);
		if (snprintf(buf, 4096, "echo \"\" | mail -s \"%s\" %s", warn_message, alarm_mail) > 0)
		{
			system(buf);
			write_log(fdlog, NOTICE, "mail sent: ", buf, "\n");
		}
	}
	for (i = 0; i < gsm_list_len; i++)
	{
		bzero(buf, 4096);
		if (snprintf(buf, 4096, "gsmsend-script %s@\"%s\"", alarm_gsm[i], warn_message) > 0)
		{
			system(buf);
			write_log(fdlog, NOTICE, "gsm sent: ", buf, "\n");
		}
	}
}

static void show_config()
{
	int i, len;
	struct stat st;
	char buf[4096];
	if (stat(status_files[5], &st) < 0)
	{
		close(fdlog);
		fdlog = open_append(status_files[5]);
		coe(fdlog);
	}
	bzero(buf, 4096);
	snprintf(buf, 4096, "alarm_interval: %u\nmax_tries: %d\nmax_tries_if_coredumped: %d\nalarm_mail : %s\ngsm_list_len: %d\n",
			alarm_interval, max_tries, max_tries_if_coredumped, alarm_mail, gsm_list_len);
	for (i = 0; i < gsm_list_len; i++)
	{
		len = strlen(buf);
		snprintf(buf + len, 4096 - len, "alarm_gsm[%d]: %s\n", i, alarm_gsm[i]);
	}
	write(fdlog, buf, strlen(buf));
}

static void parse_conf()
{
	FILE *fp;
	int len;
	unsigned int tmp = 0;
	int finished = 0;
	char conf_line[2048] = "";
	char service_tag[1024];
	char *p, *q;

	if (snprintf(service_tag, 1024, "[%s]", service) < 0)
		return;

	if ((fp = fopen(conf_path, "r")) == NULL)
	{
		write_log(fdlogwf, WARNING, "Cannot open config file. ", "Old configurations unchanged.", "\n");
		return;
	}

	alarm_interval = 2;
	bzero(alarm_mail, sizeof(alarm_mail));
	bzero(alarm_gsm, sizeof(alarm_gsm));
	max_tries = 20; 
	max_tries_if_coredumped = 5;
	gsm_list_len = 0;

	while (fgets(conf_line, 2048, fp) != NULL)
	{
		if (strncmp(conf_line, "[global]", strlen("[global]")) == 0)
			break; 
		if (strncmp(conf_line, service_tag, strlen(service_tag)) == 0)
		{
			finished = 1;
			break;
		}
	}

	while (fgets(conf_line, 2048, fp) != NULL && conf_line[0] != '[')
	{
		p = conf_line + strlen(conf_line) - 1;
		while (*p == '\n' || isspace(*p)) 
		{
			*p = '\0';
			p--;
		}

		if (strncmp(conf_line, "alarm_mail", strlen("alarm_mail")) == 0)
		{
			p = conf_line + strlen("alarm_mail");
			while (*p && isspace(*p))
				p++;
			if (*p++ != ':')
				continue;
			while (*p && isspace(*p))
				p++;
			if (*p)
				strncpy(alarm_mail, p, 1024);
			continue;
		} 
		if (strncmp(conf_line, "alarm_gsm", strlen("alarm_gsm")) == 0)
		{
			p = conf_line + strlen("alarm_gsm");
			while (*p && isspace(*p))
				p++;
			if (*p++ != ':')
				continue;
			while (*p && isspace(*p))
				p++;
			if (!p)
				continue;
			len = strlen(p);
			q = p;
			while (*q)
			{
				if (isspace(*q))
					*q = '\0';
				q++;
			}
			while (len > 0 && gsm_list_len < 10)
			{
				if (!(*p)) 
				{
					p++;
					len--;
					continue;
				}
				strncpy(alarm_gsm[gsm_list_len], p, 1024 - 1);
				len -= strlen(alarm_gsm[gsm_list_len]);
				p += strlen(alarm_gsm[gsm_list_len]);
				gsm_list_len++;
			}
			continue;
		}   
		if (strncmp(conf_line, "max_tries_if_coredumped", strlen("max_tries_if_coredumped")) == 0)
		{
			sscanf(conf_line, "max_tries_if_coredumped : %u", &max_tries_if_coredumped);
			continue;
		} 
		if (strncmp(conf_line, "max_tries", strlen("max_tries")) == 0)
		{
			sscanf(conf_line, "max_tries : %u", &max_tries);
			continue;
		} 
		if (strncmp(conf_line, "alarm_interval", strlen("alarm_interval")) == 0)
		{
			sscanf(conf_line, "alarm_interval : %u", &alarm_interval);
			if (alarm_interval > 0 && alarm_interval < 60)
				alarm_interval = 60;
			continue;
		} 
	}
	if (finished)
	{
		fclose(fp);
		write_log(fdlog, NOTICE, "Finished loading configurations!\n", "", ""); 
		show_config();
		return;
	}

	while (strncmp(conf_line, service_tag, strlen(service_tag)) != 0 && fgets(conf_line, 2048, fp) != NULL)
		;

	while (fgets(conf_line, 2048, fp) != NULL && conf_line[0] != '[')
	{
		p = conf_line + strlen(conf_line) - 1;
		while (*p == '\n' || isspace(*p)) 
		{
			*p = 0;
			p--;
		}
		if (strncmp(conf_line, "alarm_mail", strlen("alarm_mail")) == 0)
		{
			p = conf_line + strlen("alarm_mail");
			while (*p && isspace(*p))
				p++;
			if (*p++ != ':')
				continue;
			while (*p && isspace(*p))
				p++;
			if (*p)
				snprintf(alarm_mail + strlen(alarm_mail), 1024 - strlen(alarm_mail), " %s", p);
			continue;
		} 

		if (strncmp(conf_line, "alarm_gsm", strlen("alarm_gsm")) == 0)
		{
			p = conf_line + strlen("alarm_gsm");
			while (*p && isspace(*p))
				p++;
			if (*p++ != ':')
				continue;
			while (*p && isspace(*p))
				p++;
			if (!p)
				continue;
			len = strlen(p);
			q = p;
			while (*q)
			{
				if (isspace(*q))
					*q = '\0';
				q++;
			}
			while (len > 0 && gsm_list_len < 10)
			{
				if (!(*p)) 
				{
					p++;
					len--;
					continue;
				}
				strncpy(alarm_gsm[gsm_list_len], p, 1024 - 1);
				len -= strlen(alarm_gsm[gsm_list_len]);
				p += strlen(alarm_gsm[gsm_list_len]);
				gsm_list_len++;
			}
			continue;
		}   
		tmp = 0;
		if (strncmp(conf_line, "max_tries_if_coredumped", strlen("max_tries_if_coredumped")) == 0)
		{
			sscanf(conf_line, "max_tries_if_coredumped : %u", &tmp);
			if (tmp > 0)
				max_tries_if_coredumped = tmp;
			continue;
		} 
		if (strncmp(conf_line, "max_tries", strlen("max_tries")) == 0)
		{
			sscanf(conf_line, "max_tries : %u", &tmp);
			if (tmp > 0)
				max_tries = tmp;
			continue;
		} 
		if (strncmp(conf_line, "alarm_interval", strlen("alarm_interval")) == 0)
		{
			sscanf(conf_line, "alarm_interval : %u", &tmp);
			if (tmp > 0)
				alarm_interval = tmp;
			if (alarm_interval > 0 && alarm_interval < 60)
				alarm_interval = 60;
			continue;
		} 
	}
	fclose(fp);
	write_log(fdlog, NOTICE, "Finished loading configurations!\n", "", ""); 
	show_config();
}

static void pidchange()
{
	struct taia now;
	unsigned long u;

	taia_now(&now);
	taia_pack(status, &now);

	u = (unsigned long) pid;
	status[16] = u; u >>= 8;
	status[17] = u; u >>= 8;
	status[18] = u; u >>= 8;
	status[19] = u;
}

static void announce()
{
	int fd;
	int r;

	status[12] = (pid ? flagpaused : 0);
	status[13] = (flagwant ? (flagwantup ? 'u' : 'd') : 0);
	status[14] = (closealarm ? 'x' : 'o');
	status[15] = 0;

	fd = open_trunc(status_files[4]);
	if (fd == -1)
	{
		write_log(fdlogwf, WARNING, "unable to open ", status_dir, "/status.new\n");
		return;
	}

	r = write(fd, status, sizeof status);
	if (r == -1)
	{
		write_log(fdlogwf, WARNING, "unable to write ", status_dir, "/status.new\n");
		close(fd);
		return;
	}
	close(fd);
	if (r < sizeof status)
	{
		write_log(fdlogwf, WARNING, "unable to write ", status_dir, "/status.new: partial write\n");
		return;
	}

	if (rename(status_files[4], status_files[3]) == -1)
		write_log(fdlogwf, WARNING, "unable to rename ", status_dir, "/status.new to status\n");
}

static void trigger()
{
	write(selfpipe[1], "", 1);
}

static void timer_handler()
{
	have_tried = 0;
	have_coredumped = 0;
}

static void trystart()
{
	int f;
	switch(f = fork())
	{
		case -1:
			write_log(fdlogwf, WARNING, "unable to fork for ", service, ", sleeping 60 seconds\n");
			deepsleep(60);
			trigger();
			return;
		case 0:
			sig_uncatch(sig_child);
			sig_unblock(sig_child);
			execvp(cmd[0], cmdp);
			write_log(fdlogwf, FATAL, "unable to start ", cmd[0], "\n");
			_exit(1);
	}
	flagpaused = 0;
	pid = f;
	pidchange();
	announce();
	deepsleep(1);
}

static void  create_warn_message(char *warn_message, int coredumped)
{
	char *tmp;
	int len;
	char hostname[128];
	
	bzero(hostname, 128);
	if (gethostname(hostname, 128) < 0)
	{
		write_log(fdlogwf, WARNING, "unable to gethostname\n", "", "");
	}
	if ((tmp = strstr(hostname, ".baidu.com")) != NULL)
		*tmp = '\0';

	len = gettimenow(warn_message, 128);
	snprintf(warn_message + len, 2048 - len , "[Alarm: %s] Service %s exited%s!", 
			hostname, service, (coredumped ? " with coredump" : ""));   
}

static void doit()
{
	iopause_fd x[2];
	struct taia deadline;
	struct taia stamp;
	int wstat;
	long int time_cmp = 0;
	int r;
	char ch;
	char warn_message[2048];
	int coredumped = 0;
	char restart_cmd[2048];

	announce();
	x[0].fd = selfpipe[0];
	x[0].events = IOPAUSE_READ;
	x[1].fd = fdcontrol;
	x[1].events = IOPAUSE_READ;

	while (1)
	{
		if (flagexit && !pid)
			break;

		taia_now(&stamp);
		taia_uint(&deadline, 3600);
		taia_add(&deadline, &stamp, &deadline);
		sig_unblock(sig_child);
		iopause(x, 2, &deadline, &stamp);
		sig_block(sig_child);

		while (read(selfpipe[0], &ch, 1) == 1);

		while (1)
		{
			//waitpid(-1,&wstat,WNOHANG);WNOHANG : return immediately if no child has exited
			r = wait_nohang(&wstat);
			//r==0 if one or more child(ren) exit(s) but have not yet changed state
			if (!r)
				break;
			//r == -1 && errno == error_intr means waitpid is interrupted by a signal, we should call waitpid again.
			//here is not necessary cause we call waitpid with a WNOHANG argument.
			if (r == -1 && errno != error_intr)
				break;
			if (r == pid)
			{
				pid = 0;
				pidchange();
				announce();
				time_now = time((time_t *)0);
				if(time_old)
				{
					time_cmp = time_now - time_old;
					if(time_cmp >= time_alarm) //cmp
					{
						num = 0;
					}
					time_old = time_now;
				}
				else
				{
					time_cmp = 0;
					time_old = time_now;
				}
				if (0 != restart_sh[0])
				{
					if (num == INT_MAX)
						num = 0;
					num++;
					
					if (snprintf(restart_cmd, sizeof(restart_cmd), "%s %d", restart_sh, num) > 1)
					{
						system(restart_cmd);
						write_log(fdlog, NOTICE, "restart_cmd: ", restart_cmd, " is called.\n");
					}
				}
				else
				{

					if (WCOREDUMP(wstat))
					{
						have_coredumped++;
						coredumped = 1;
					}

					bzero(warn_message, 2048);
					create_warn_message(warn_message, coredumped);      
					write_log(fdlog, NOTICE, "service exited", coredumped ? " with coredump" : "", "!\n");
					coredumped = 0;

					if (!closealarm && alarm_interval > 0 && have_tried++ == 0)
					{
						alarm(alarm_interval);
						do_alarm(warn_message);
					}

					if (flagexit || (alarm_interval > 0 && have_tried > max_tries && max_tries > 0) ||
					(alarm_interval > 0 && have_coredumped > max_tries_if_coredumped && max_tries_if_coredumped > 0))
					{
						write_log(fdlog, NOTICE, "supervise refused to restart ", service, 
								" any more and exited itself!\n");
						alarm(0);
						return;
					}
				}

				if (flagwant && flagwantup)
				{
					write_log(fdlog, NOTICE, "supervise is trying to restart ", service, "...\n");
					trystart();
				}
				break;
			}
		}

		if (read(fdcontrol, &ch, 1) != 1)
			continue;

		switch(ch)
		{
			// -s: Silent. Do not alarm any more.
			case 's':
				closealarm = 1;
				announce();
				break;
			case 'n':
				closealarm = 0;
				announce();
				break;
			case 'r':
				parse_conf();
				break;
			// -d: Down. If the service is running, send it a TERM signal and then a CONT signal. 
			// After it stops, do not restart it.
			case 'd':
				flagwant = 1;
				flagwantup = 0;
				if (pid) { kill(pid, SIGTERM); kill(pid, SIGCONT); flagpaused = 0; }
				announce();
				break;
			// -u: Up. If the service is not running, start it. If the service stops, restart it.
			case 'u':
				flagwant = 1;
				flagwantup = 1;
				announce();
				if (!pid) trystart();
				break;
			// -o: Once. If the service is not running, start it. Do not restart it if it stops.
			case 'o':
				flagwant = 0;
				announce();
				if (!pid) trystart();
				break;
			// -a: Alarm. Send the service an ALRM signal.
			case 'a':
				if (pid) kill(pid, SIGALRM);
				break;
			// -h: Hangup. Send the service a HUP signal.
			case 'h':
				if (pid) kill(pid, SIGHUP);
				break;
			// -k: Kill. Send the service a KILL signal.
			case 'k':
				if (pid) kill(pid, SIGKILL);
				break;
			//* -t: Terminate. Send the service a TERM signal.
			case 't':
				if (pid) kill(pid, SIGTERM);
				break;
			// -i: Interrupt. Send the service an INT signal.
			case 'i':
				if (pid) kill(pid, SIGINT);
				break;
			// -p: Pause. Send the service a STOP signal.
			case 'p':
				flagpaused = 1;
				announce();
				if (pid) kill(pid, SIGSTOP);
				break;
			// -c: Continue. Send the service a CONT signal.
			case 'c':
				flagpaused = 0;
				announce();
				if (pid) kill(pid, SIGCONT);
				break;
			// -x: Exit. supervise will exit as soon as the service is down. If you use this option on a 
			//stable system, you're doing something wrong; supervise is designed to run forever. 
			case 'x':
				flagexit = 1;
				announce();
				break;
		} //end switch
	} //end while
}

static int parse_argv(int argc, char **argv)
{

	int c, i, j;
	int uflag = 0;
	char *p;
	while ((!uflag) && (c = getopt(argc, argv, "t:r:f:p:u:F:hv?")) != -1)
	{
		switch (c)
		{
			case 't':
				time_alarm = atoi(optarg);
				break;
			case 'u':
				uflag = 1;
			case 'p':
				strncpy(status_dir, optarg, sizeof(status_dir) - 1);
				if (strlen(status_dir) < 1)
				{
					printf("bad status_dir: sub-string \"status/module_name\" is required.\n");
					return(-1);
				}
				p = &status_dir[strlen(status_dir) - 1];
				while(p - status_dir >= 0 &&  (*p == '/' || isblank((int)(*p))))
				{
					*p = '\0';
					p--;
				}
				if (NULL == (p = strstr(status_dir, "status/")))
				{
					printf("bad status_dir: sub-string \"status/module_name\" is required!\n");
					return(-1);
				}
				p += 7;
				while(*p && *p == '/')
					p++;
				if (*p == '\0' || snprintf(service, sizeof(service), "%s", p) < 1)
				{
					printf("bad status_dir: sub-string \"status/module_name\" is required!\n");
					return(-1);
				}
				break;
			case 'f':
				p = optarg;
				i = 0;
				while(*p && i < 512 - 1)
				{
					while (*p && isblank((int)(*p)))
					{
						p++;
					}
					j = 0;
					while (*p && !isblank((int)(*p)) && j < 1024 - 1)
					{
						cmd[i][j++] = *p++;
					}
					if (*p && !isblank((int)*p))
					{
						printf("arg is too long: %s\n", optarg);
						return(-1);
					}
					if (!cmd[i][0])
						break;
					cmdp[i] = cmd[i];
					i++;
				}
				break;
			case 'r':
				strncpy(restart_sh, optarg, sizeof(restart_sh) - 1);
				break;
			case 'F':
				bzero(conf_path, sizeof(conf_path));
				strncpy(conf_path, optarg, sizeof(conf_path) - 1);
				break;
			case 'v':
				printf("v 1.0.0\n");
				return(-1);
			case 'h':
			case '?':
			default:
				help(argv[0]);
				return(-1);
		}
	}
	if (!cmdp[0] && optind < argc)
	{
		i = 0;
		while (optind < argc &&  i < 512 - 1)
		{
			strncpy(cmd[i], argv[optind++], sizeof(cmd[i]) -1);
			cmdp[i] = cmd[i];
			i++;
		}	
	}
	
	if (!cmdp[0] || !status_dir[0])
	{
		printf("bad arg:\n");
		help(argv[0]);
		return(-1);
	}
	return(0);
}

int main(int argc, char **argv)
{
	struct sigaction sa;

	if (parse_argv(argc, argv) < 0)
		_exit(100);

	snprintf(status_files[0], 1024, "%s/lock", status_dir);
	snprintf(status_files[1], 1024, "%s/control", status_dir);
	snprintf(status_files[2], 1024, "%s/ok", status_dir);
	snprintf(status_files[3], 1024, "%s/status", status_dir);
	snprintf(status_files[4], 1024, "%s/status.new", status_dir);
	snprintf(status_files[5], 1024, "%s/supervise.log", status_dir);
	snprintf(status_files[6], 1024, "%s/supervise.log.wf", status_dir);

	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGHUP, &sa, NULL) < 0)
	{
		printf("unable to ignore SIGHUP for %s\n", service);  
		_exit(110);
	}

	if (mkdir(status_dir, 0700) < 0 && errno !=  EEXIST)
	{
		printf("unable to create dir: %s\n", status_dir);  
		_exit(110);
	}

	fdlog = open_append(status_files[5]);
	if (fdlog == -1)
	{
		printf("unable to open %s%s", status_dir, "/supervise.log");
		_exit(111);
	}
	coe(fdlog);

	fdlogwf = open_append(status_files[6]);
	if (fdlogwf == -1)
	{
		printf("unable to open %s%s", status_dir, "/supervise.log.wf");
		_exit(1);
	}
	coe(fdlogwf);

	if (daemon(1, 0) < 0)
	{
		printf("failed to daemonize supervise!\n");
		_exit(111);
	}

	if (pipe(selfpipe) == -1)
	{
		write_log(fdlogwf, FATAL, "unable to create pipe for ", service, "\n");
		_exit(111);
	}
	coe(selfpipe[0]);
	coe(selfpipe[1]);
	ndelay_on(selfpipe[0]);
	ndelay_on(selfpipe[1]);

	sig_block(sig_child);
	sig_catch(sig_child, trigger);

	sig_block(sig_alarm);
	sig_catch(sig_alarm, timer_handler);
	sig_unblock(sig_alarm);

	fdlock = open_append(status_files[0]);
	if ((fdlock == -1) || (lock_exnb(fdlock) == -1))
	{
		write_log(fdlogwf, FATAL, "Unable to acquier ", status_dir, "/lock\n");
		_exit(111);
	}
	coe(fdlock);

	fifo_make(status_files[1], 0600);
	fdcontrol = open_read(status_files[1]);
	if (fdcontrol == -1)
	{
		write_log(fdlogwf, FATAL, "unable to read ", status_dir, "/control\n");
		_exit(1);
	}
	coe(fdcontrol);
	ndelay_on(fdcontrol);

	fdcontrolwrite = open_write(status_files[1]);
	if (fdcontrolwrite == -1)
	{
		write_log(fdlogwf, FATAL, "unable to write ", status_dir, "/control\n");
		_exit(1);
	}
	coe(fdcontrolwrite);

	fifo_make(status_files[2], 0600);
	fdok = open_read(status_files[2]);
	if (fdok == -1)
	{
		write_log(fdlogwf, FATAL, "unable to read ", status_dir, "/ok\n");
		_exit(1);
	}
	coe(fdok);
	
	if (!restart_sh[0])
	{
		parse_conf(); 
	}
	pidchange();
	announce();

	if (!flagwant || flagwantup)
		trystart();
	doit();
	announce();

	_exit(0);
}

