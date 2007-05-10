/***************************************************************************
 *   Copyright (C) 2004 by Jedi                                            *
 *   huangsong@lingtu.com                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
/**
 *   mlvpn.c
 *   Created:
 *	2004/12/05 Jedi
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#define MLVPN_VERSION "0.2.0-alpha"
#define max(a,b) ((a)>(b) ? (a):(b))


int openlinkpair (const struct sockaddr_in salocal, const struct sockaddr_in saremote)
{
	int	sockfd;
	int	ret;

	sockfd = socket (PF_INET, SOCK_DGRAM, 0);
	ret = bind (sockfd, (const struct sockaddr*)&salocal, sizeof(salocal));
	if (ret < 0) {
		close (sockfd);
		return ret;
	}

	ret = connect (sockfd, (const struct sockaddr*)&saremote, sizeof(saremote));
	if (ret < 0) {
		close (sockfd);
		return ret;
	}
	return sockfd;
}


void mlvpn_print_help (void)
{
	printf ("Usage: mlvpn [OPTIONS]\n");
	printf ("  -F Fixed payload-rate\n");
	printf ("  -h Display this help and exit\n");
	printf ("  -i Specifies tunnel interface-name('tun0' etc.)\n");
	printf ("  -l Load Specifies model as packet-processor\n");
	printf ("  -p Specifies link-pair address as <localip:localport/remoteip:remoteport>\n");
	printf ("  -r Specifies payload-rate as <r1:r2...>\n");
	printf ("  -u Specifies link-up scripts(full-path)\n");
	printf ("  -v Display version information and exit\n");
	fflush (stdout);
}


void mlvpn_print_version (void)
{
        printf ("Multilink vpn Version %s\n", MLVPN_VERSION);
        fflush (stdout);
}


int main (int argc, char *argv[])
{
	int			ch;
	int			fixplrate = 0;
	char*			lus = NULL;
	char**			strlpv = NULL;
	char*			strlp = NULL;
	int			nport = 0;
	int			lpcnt = 0;
	char*			straddrl = NULL;
	char*			straddrr = NULL;
	char*			strip = NULL;
	char*			strport = NULL;
	int*			sockfd = NULL;
	struct sockaddr_in*	salocal = NULL;
	struct sockaddr_in*	saremote = NULL;
	char*			strplrate = NULL;
	int			nplrate = -1;
	int*			splratev = NULL;
	int			splratecnt = 0;
	int			splsum = 0;
	int*			dplratev = NULL;
	int*			drcvcntv = NULL;
	int			dplsum = 0;
	char			dev[14] = "\0";
	int			pid,st,fd;
	char*			cmdargv[4];
	int			i,j;
	int			fdtun;
	struct ifreq		ifr;
	fd_set			fds;
	int			ret;
	char			linkpairs[65536] = "\0";
	char			bufv[2][65536] = {"\0", "\0"};
	int			buflenv[2] = {0, 0};
	int			dest = 0;
	int			fdmax;
	long int		rnd;
	int			dplrcvcnt = 0;
	int			delta = 0;
	char*			modname = NULL;
	int			modcnt = 0;
	struct {
		void*	hmod;
		void*	ctx;
		char*	modname;
		char*	modarg;
		void* (*mod_init) (char* args);
		void (*mod_finit) (void* mc);
		int (*encode) (void* mc, void* dst, unsigned int* pdstlen, const void* src, unsigned int srclen);
		int (*decode) (void* mc, void* dst, unsigned int* pdstlen, const void* src, unsigned int srclen);
	} *modv = NULL;

	if (argc == 1) {
		mlvpn_print_help();
		mlvpn_print_version();
		return 0;
	}

	while ((ch = getopt(argc, argv, "+Fhi:l:p:r:u:v")) && ch != -1) {
		switch (ch) {
			case 'F':
				fixplrate = 1;
				break;
			case 'p':
				// link-parir config
				if (optarg==NULL || (strlp=strtok(optarg,"+"))==NULL) {
					fprintf (stderr, "Bad link-pair format: %s\n", optarg);
					return 1;
				} else {
					do {
						strlpv = realloc(strlpv, (lpcnt+1)*sizeof(char*));
						strlpv[lpcnt] = strdup (strlp);
						lpcnt++;
					} while ((strlp = strtok (NULL, "+")) != NULL);
				}
				break;
			case 'l':
				// preprocess module: /usr/local/lib/zlib.so:level=9,nodebug"
				if (optarg==NULL || (modname=strtok(optarg,"+"))==NULL) {
					fprintf (stderr, "Bad modname-args: %s\n", modname);
					return 1;
				} else {
					do {
						modv = realloc(modv, (modcnt+1)*sizeof(*modv));
						modv[modcnt].modname = strdup (modname);
						modcnt++;
					} while ((modname = strtok (NULL, ":")) != NULL);
				}
				break;
			case 'r':
				// payload config: "128:128"
				if (optarg==NULL || (strplrate=strtok(optarg,":"))==NULL) {
					fprintf (stderr, "Bad payload-rates format: %s\n", optarg);
					return 1;
				} else {
					do {
						if ((nplrate=atoi(strplrate)) <= 0) {
							fprintf (stderr, "Bad payload-rate Number: %s\n", optarg);
							return 1;
						}
						splratev = realloc (splratev, (splratecnt+1)*sizeof(int));
						splratev[splratecnt] = nplrate;
						splratecnt++;
					} while ((strplrate = strtok (NULL, ":")) != NULL);
				}
				break;
			case 'i':
				// tunnel device name.
				if (optarg != NULL) {
					strncpy (dev, optarg, sizeof(dev));
				}
				break;
			case 'u':
				// link-up scripts path
				if (optarg != NULL) {
					lus = strdup(optarg);
				}
				break;
			case 'v':
				mlvpn_print_version();
				return 0;
			case 'h':
			default:
				mlvpn_print_help();
				return 0;
		}
	}

	// at least one link-pair is needed;
	if (lpcnt == 0) {
		fprintf (stderr, "Link-pair(s) address must be specified\n");
		return 1;
	}

	sockfd = malloc (lpcnt*sizeof(int));
	salocal = malloc (lpcnt*sizeof(struct sockaddr_in));
	saremote = malloc (lpcnt*sizeof(struct sockaddr_in));
	memset (salocal, 0, lpcnt*sizeof(struct sockaddr_in));
	memset (saremote, 0, lpcnt*sizeof(struct sockaddr_in));
	for (i=0; i<lpcnt; i++) {
		strlp = strdup(strlpv[i]);
		if ((straddrl = strtok (strlp, "/")) == NULL
			|| (straddrr = strtok (NULL, "/")) == NULL) {
			fprintf (stderr, "Unknown link-pair format: %s.\n", strlpv[i]);
			return 1;
		}
		if ((strip = (strtok (straddrl, ":"))) == NULL
			|| inet_addr(strip) == -1
			|| (strport = strtok (NULL, ":")) == NULL
			|| (nport = atoi(strport)) <= 0) {
			fprintf (stderr, "Unknown local-addr format: %s.\n", straddrl);
			return 1;
		}
		salocal[i].sin_family = AF_INET;
		salocal[i].sin_addr.s_addr = inet_addr(strip);
		salocal[i].sin_port = htons((uint16_t)nport);
		if ((strip = (strtok (straddrr, ":"))) == NULL
			|| inet_addr(strip) == -1
			|| (strport = strtok (NULL, ":")) == NULL
			|| (nport = atoi(strport)) <= 0) {
			fprintf (stderr, "Unknown remote-addr format: %s.\n", straddrr);
			return 1;
		}
		saremote[i].sin_family = AF_INET;
		saremote[i].sin_addr.s_addr = inet_addr(strip);
		saremote[i].sin_port = htons((uint16_t)nport);
		sockfd[i] = openlinkpair (salocal[i], saremote[i]);
		if (sockfd[i] < 0) {
			perror ("openlinkpair failed");
			return 1;
		}
		free (strlp);
	}

	for (i=0; i<modcnt; i++) {
		modv[i].modname = strtok (modv[i].modname, ":");
		modv[i].modarg = strdup(strtok(NULL,":"));
		modv[i].hmod = dlopen (modv[i].modname, RTLD_NOW);
		if (!modv[i].hmod) {
			fprintf (stderr, "%s\n", dlerror());
			return 1;
		}
		dlerror();
		modv[i].mod_init = dlsym (modv[i].hmod, "mod_init");
		modv[i].mod_finit = dlsym (modv[i].hmod, "mod_finit");
		modv[i].encode = dlsym (modv[i].hmod, "encode");
		modv[i].decode = dlsym (modv[i].hmod, "decode");
		if (!modv[i].mod_init || !modv[i].mod_finit || !modv[i].encode || !modv[i].decode) {
			fprintf (stderr, "%s\n", dlerror());
			return 1;
		}
		fprintf (stdout, "ready\n");
		modv[i].ctx = modv[i].mod_init (NULL);
		if (modv[i].ctx == NULL) {
			perror ("mod_init");
			return 1;
		}
	}

	// use default payload-rate if no payload connfig exist;
	// 128 is the default payload-count;
	if (splratecnt==0 || splratecnt!=lpcnt) {
		splratev = malloc(lpcnt*sizeof(int));
		for (i=0; i<lpcnt; i++) {
			splratev[i] = 128/lpcnt;
		}
		for (i=0; i<(128%lpcnt); i++) {
			splratev[i]++;
		}
	}

	dplratev = malloc(sizeof(int)*lpcnt);
	
	drcvcntv = malloc(sizeof(int)*lpcnt);

	for (i=0,splsum=0; i<lpcnt; i++) {
		if(splratev[i] <= 0) {
			fprintf (stderr, "Bad payload-rate Number: %d.\n", splratev[i]);
			return 1;
		}
		dplratev[i] = splratev[i];
		drcvcntv[i] = 0;
		splsum += splratev[i];
	}

	openlog ("mlvpn", LOG_PID|LOG_CONS, LOG_DAEMON);
	strncat (linkpairs, strlpv[0], sizeof(linkpairs));
	for (i=1; i<lpcnt; i++) {
		strncat (linkpairs, "+", sizeof(linkpairs));
		strncat (linkpairs, strlpv[i], sizeof(linkpairs));
	}
	setenv ("LINKPAIRS", linkpairs, 1);
	fprintf (stderr, "LINKPAIRS is %s\n", linkpairs);

	fdtun = open("/dev/net/tun", O_RDWR);
	if (fdtun < 0) {
		syslog (LOG_ERR, "Cann't open '/dev/net/tun': %s\n", strerror(errno));
		perror ("open failed");
		return 2;
	}
	memset (&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	if (*dev) {
		strncpy (ifr.ifr_name, dev, IFNAMSIZ);
	}
	if (ioctl(fdtun, TUNSETIFF, (void *) &ifr) < 0) {
		syslog (LOG_ERR, "Create tunnel device failed");
		perror ("ioctl failed");
		return 2;
	}
	strcpy (dev, ifr.ifr_name);
	setenv ("IFNAME", dev, 1);
	fprintf (stderr, "IFNAME: %s\n", dev);

	if (lus != NULL) {
		if ((pid = fork()) == 0) {
			close (fdtun);
			for (i=0; i<lpcnt; i++) {
				close (sockfd[i]);
			}
			fd = open("/dev/null", O_RDWR);
			close(0); dup(fd);
			close(1); dup(fd);
			close(2); dup(fd);
			close(fd);
			setsid();
			cmdargv[0] = "sh";
			cmdargv[1] = "-c";
			cmdargv[2] = lus;
			cmdargv[3] = NULL;
			execv ("/bin/sh", cmdargv);
			syslog (LOG_ERR, "Couldn't exec program: %s(%s)", lus, strerror(errno));
			return (1);
		} else {
			if( waitpid(pid,&st,0) > 0 && (WIFEXITED(st)) && WEXITSTATUS(st)) {
				syslog (LOG_ERR, "Command [%s] return error %d", lus, WEXITSTATUS(st));
			}
		}
	}

	while (1) {
		FD_ZERO (&fds);
		FD_SET(fdtun, &fds);
		fdmax = fdtun;
		for (i=0; i<lpcnt; i++) {
			FD_SET(sockfd[i], &fds);
			fdmax = (fdmax > sockfd[i] ? fdmax : sockfd[i]);
		}

		ret = select (fdmax+1, &fds, NULL, NULL, NULL);

		if (FD_ISSET(fdtun, &fds)) {
			// MLP protocol-head (4 byte)
			buflenv[0] = read (fdtun, bufv[0]+4, sizeof(bufv[0])-4);
			if (buflenv[0] < 0) {
				perror ("Read fdtun failed.\n");
				continue;
			}
			fprintf (stderr, "got %d bytes from  fdtun\n", buflenv[0]);
			// i = open("/tmp/ping.ip", O_CREAT|O_WRONLY);
			// ret = write(i, bufv[0]+4, buflenv[0]);
			// ret = close(i);
			dest = 0;
			for (i=0; i<modcnt; i++) {
				// switch to next buffer every time.
				dest = !((i+2)%2);
				buflenv[dest] = sizeof(bufv[dest])-4;
				ret = modv[i].encode (modv[i].ctx, bufv[dest]+4, (unsigned int*)(&(buflenv[dest])), bufv[i]+4, buflenv[i]);
				if (ret) {
					fprintf (stderr, "packet-processor %s encode() failed %d\n", modv[i].modname, ret);
					continue;
				}
			}
			// payload-rate determine the sending chance;
			rnd = random();
			for (i=0; i<(lpcnt-1); i++) {
				rnd -= (RAND_MAX/splsum)*dplratev[i];
				if (rnd < 0) break;
			}
			bufv[dest][0] = 0x11; // MLP version 1, head length 1x32bits
			bufv[dest][1] = 0x01; // Packet type is payload-rate control packet.
			bufv[dest][2] = 0;
			bufv[dest][3] = 0;
			ret = write (sockfd[i], bufv[dest], buflenv[dest]+4);
			fprintf (stderr, "write %d bytes to sockfd[%d]\n", buflenv[dest], i);
			continue;
		}

		for (i=0; i <lpcnt; i++) {
			if (FD_ISSET(sockfd[i], &fds)) {
				buflenv[0] = read (sockfd[i], bufv[0], sizeof(bufv[0]));
				if (buflenv[0] < 0) {
					perror ("Read sockfd failed.\n");
					continue;
				}
				if (bufv[0][0] != 0x11) continue;
				switch (bufv[0][1]) {
					// Packet type is payload-rate count packet.
					case 0x01:
						fprintf (stderr, "got %d bytes from sockfd[%d]\n", buflenv[0], i);
						dest = 0;
						buflenv[dest] -= 4;
						for (j=0; j<modcnt; j++) {
							// switch to next buffer every time.
							dest = !((j+2)%2);
							buflenv[dest] = sizeof(bufv[dest])-4;
							ret = modv[modcnt-1-j].decode (modv[modcnt-1-j].ctx,
												bufv[dest]+4,
												(unsigned int*)(&(buflenv[dest])),
												bufv[j]+4,
												buflenv[j]);
							if (ret) {
								fprintf (stderr,
									"packet-processor %s decode() failed %d\n",
									modv[modcnt-1-j].modname,
									ret);
								continue;
							}
						}
						ret = write (fdtun, bufv[dest]+4, buflenv[dest]);
						fprintf (stderr, "write %d bytes to fdtun\n", buflenv[dest]);
						drcvcntv[i]++;
						dplrcvcnt++;
						// send-count is enough, send payload-rate count packet.
						if (!fixplrate && dplrcvcnt >= splsum) {
							bufv[0][0] = 0x11;
							bufv[0][1] = 0x11;
							bufv[0][2] = 0;
							bufv[0][3] = 0;
							for (i=0; i<lpcnt; i++) {
								nplrate = htonl(drcvcntv[i]);
								memcpy (bufv[0]+4+sizeof(int)*i, &nplrate, sizeof(int));
								drcvcntv[i] = 0;
							}
							// send payload-rate count packet to all linkpair
							for (i=0; i<lpcnt; i++) {
								write (sockfd[i], bufv[0], sizeof(int)*lpcnt+4);
							}
							dplrcvcnt = 0;
						}
						break;
					// Packet type is payload-rate control packet.
					case 0x11:
						if (fixplrate) break;
						if((buflenv[0]-4)/sizeof(int) != lpcnt) break;
						for (i=0,dplsum=0; i<lpcnt; i++) {
							memcpy (&nplrate, bufv[0]+4+sizeof(int)*i, sizeof(int));
							nplrate = ntohl(nplrate);
							if (nplrate<0) continue;
							dplsum += nplrate;
						}
						if (dplsum != splsum) continue;
						fprintf (stdout, "payload-rate:");
						for (i=0; i<lpcnt; i++) {
							memcpy (&dplratev[i], bufv[0]+4+sizeof(int)*i, sizeof(int));
							dplratev[i] = ntohl(dplratev[i]);
							fprintf (stdout, "%d", dplratev[i]);
							if (i == lpcnt-1) {
								dplratev[i] = dplsum;
								fprintf (stdout, "(%d)\n", dplratev[i]);
							} else {
								// delta is a tip to avoid long-time payload-skew
								delta = splsum/lpcnt/16 != 0 ? splsum/lpcnt/16 : 1;
								if (abs(splratev[i]-dplratev[i]) > delta) {
									if (dplratev[i]>splratev[i]) {
										dplratev[i] = dplratev[i] - delta;
									} else if (dplratev[i]<splratev[i]) {
										dplratev[i] = dplratev[i] + delta;
									}
								} else {
									dplratev[i] = splratev[i];
								}
								fprintf (stdout, "(%d):", dplratev[i]);
								dplsum -= dplratev[i];
							}
						}
						fflush (stdout);
						break;
					default:
						fprintf (stderr, "Unknown packet-type:0x%X\n", bufv[0][1]);
				}
				continue;
			}
		}
	}
}
