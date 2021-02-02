/*
 * oregon_read.cpp
 *
 *  Created on: 13Apr.,2020
 *      Author: Ivaylo Haratcherev
 */

#include "cc1101_oregon.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <string.h>

#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <linux/limits.h>

#include <getopt.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>

#define SHM_DEBUG	0
#define PARANOID_NEEDS_BOTH_MESSAGES	0

#define PACKAGE    "CC1101 Oregon read utility"
#define VERSION_SW "1.51"

#define OPTCHARS		"d::hobVr::Ktn:"
#define ARG_o			1
#define ARG_b			(1<<1)
#define ARG_t			(1<<2)
#define ARG_V			(1<<3)
#define ARG_K			(1<<4)
#define ARG_r			(1<<5)
#define ARG_n			(1<<6)

#define SKIP_LOG_COUNT	50  // determines frequency of log updates
#define ADDITIONAL_DELAY_MS	100
#define SHORT_DELAY_MS	5
#define MSG_TIMEOUT_MS	1000
#define OREGON_DATA_TIMEOUT_S	300
#define OREGON_DATA_MIN_TIMEOUT_S	60
#define LINELEN 	        256
#define SUCCESS                  1
#define FATALERR		-1
#define OREAD_KEY		0x8f2a474c
#define SHMEM_SIZE		(sizeof(struct INSTANCE))


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define ABS(a) ( (a) < 0 ? (-(a)) : (a) )



//--------------------------[Global CC1101 variables]--------------------------
uint8_t rx_fifo1[FIFOBUFFER], rx_fifo2[FIFOBUFFER];
uint8_t pktlen1, pktlen2;
uint8_t lqi1,lqi2;
int8_t rssi_dbm1,rssi_dbm2;
unsigned int uCurrTime, uOldTime, uPrevTime, uIntvl_s;
double last_temp_reading;


CC1101_Oregon cc1101_oregon;

int	debug_level			= 	0;
int	log2syslog		= 	0;
int	kill_proc		=	0;
int bare_temp		=	0;
int show_data		=	0;
int	show_verbose		=	0;
int	test_mode		=	0;
int	clear_stats		=	0;
int	reset_stats		=	0;
long	reset_flags		=	0xff;
int data_invalid_timeout = OREGON_DATA_TIMEOUT_S;

int	keep_running		=	1;
void	*shmaddr		=	NULL;
char	*program		=	NULL;
int	shmid			=	0;

struct	sigaction sig;
char    strbuf[LINELEN];

//-------------------------- [End] --------------------------
///////////////////////////////////////////////////////////////////////////

struct INSTANCE {
	int	pid;
	int data_invalid_timeout;
	oregon_data_t oregon_data;
	time_t	last_upd_time; // last time data has been received from oregon sensor
	// statistics
	unsigned long total_reads;
	unsigned long good_reads;
	unsigned int  min_intvl, max_intvl;
	unsigned int brst1_errors, brst2_errors, mbrst_errors, pktlen_errors, buffmatch_errors, chksum_errors;
	double max_temp_diff;
	long	rssi_sum;
	unsigned long	lqi_sum;
	uint8_t lqi_max, lqi_min;
	int8_t rssi_max, rssi_min;
	long	reset_flags;
} *my_instance = NULL;

void    update_global_stats(struct INSTANCE *is);
void    do_main_cycle();
void	process_options(int argc, char *argv[]);
void    interact_with_daemon();
void	sigchld_handler(int signum);
void	exit_handler(int signum);
void	resetstats_handler(int signum);
#if SHM_DEBUG
void	dump_shm(struct shmid_ds *d);
#endif
void    disp_rx_stats(struct INSTANCE *is);
void	disp_oregon_data(struct INSTANCE *is, int disp_time);
void    init_inst_struct(struct INSTANCE *is, int clear_all);
void    init_HW();
int		get_shm_info();
int		run_as_background();
void	Msg(const char *fmt, ...);
char   *nol_ctime(const time_t *timep);


void Usage()
{
	fprintf(stderr,  "\nUSAGE: %s ", program);
	fprintf(stderr, "[ -o][ -b][ -V][ -r[flags]]");
	fprintf(stderr, "[ -K][ -t [ -d[num]]][ -n[num]][ -h]");
	fprintf(stderr, "\n\n%s, Version %s by Ivaylo Haratcherev\n", PACKAGE, VERSION_SW);
	fprintf(stderr, "Run without options or with -n (as root) to start the listening daemon.\n");
	fprintf(stderr, "Options: \n");
	fprintf(stderr, "         -o               show last Oregon data collected from daemon\n");
	fprintf(stderr, "         -b               show only temperature in bare format\n");
	fprintf(stderr, "         -V               show daemon info + verbose Rx stats and Oregon data\n");
	fprintf(stderr, "         -r[flags]        reset daemon statistics counters (needs root)\n");
	fprintf(stderr, "                          optional flags in binary form indicate \n");
	fprintf(stderr, "                          stats to clear (LSB to MSB):\n");
	fprintf(stderr, "                             bit 0 - bad packets and errors count + all RSSI and LQI stats\n");
	fprintf(stderr, "                             bit 1 - min/max time between good packets\n");
	fprintf(stderr, "                             bit 2 - min/max RSSI\n");
	fprintf(stderr, "                             bit 3 - min/max LQI\n");
	fprintf(stderr, "                             bit 4 - max Temperature value variation\n");
	fprintf(stderr, "         -K               terminate daemon instance (needs root)\n");
	fprintf(stderr, "         -t               test mode - Rx Oregon data is displayed as received (needs root)\n");
    fprintf(stderr, "         -d[num]          optional debug level num (default 1) for test mode\n");
    fprintf(stderr, "         -n[num]          optional data invalid timeout (default %d) - daemon only\n", OREGON_DATA_TIMEOUT_S);
	fprintf(stderr, "         -h               help (this text)\n");
}

int main(int argc, char *argv[])
{
	char *p;
	struct passwd *nobody;

	sig.sa_flags = 0;
	sigemptyset(&sig.sa_mask);
	sig.sa_handler = SIG_IGN;
	sigaction(SIGHUP,&sig,NULL);
	sigaction(SIGPIPE,&sig,NULL);
	sigaction(SIGUSR1,&sig,NULL);
	sigaction(SIGUSR2,&sig,NULL);
	p = strrchr(argv[0], '/');
	if (p)
		program = ++p;
	else
		program = argv[0];
	openlog(program, LOG_PID, LOG_DAEMON);

	process_options(argc, argv);

	if (show_verbose || bare_temp || show_data || kill_proc || reset_stats) {
	    interact_with_daemon();
	    exit(0);
	}
	if (test_mode)
	{
		fprintf(stderr, "Test mode ");
		if (debug_level)
			fprintf(stderr, "with debug level set to %d\n", debug_level);
		else
			fprintf(stderr, "\n");
	}

	sig.sa_handler = exit_handler;
	sigaction(SIGINT,&sig,NULL);
	sigaction(SIGTERM,&sig,NULL);
	sigaction(SIGQUIT,&sig,NULL);


	if (test_mode == 0) {
	    sig.sa_handler = resetstats_handler;
	    sigaction(SIGUSR1,&sig,NULL);
		sig.sa_handler = sigchld_handler;
		sigaction(SIGCHLD, &sig, NULL);
		if (run_as_background() == FATALERR) {
			return FATALERR;
		}
	} else {
		if (get_shm_info() == FATALERR)
			return FATALERR;
		init_HW();
		do_main_cycle();
		cc1101_oregon.end();
		shmdt(shmaddr);
		if (shmctl(shmid, IPC_RMID, NULL) != 0) {
		    Msg("Cannot remove shared memory (%s)!", strerror(errno));
		}
	}
	return 0;
}

void update_global_stats(struct INSTANCE *is)
{
	  unsigned int uDiffTime;
	  if (uCurrTime < uPrevTime)
		  uDiffTime = uCurrTime + ~uPrevTime + 1;
	  else
		  uDiffTime = uCurrTime - uPrevTime;
	  uIntvl_s = uDiffTime/1000 + (uDiffTime % 1000)/500;
	  uPrevTime = uCurrTime;
	  // update time intervals only after the second reception
	  if (is->good_reads > 1) {
		  is->min_intvl = MIN(is->min_intvl, uIntvl_s);
		  is->max_intvl = MAX(is->max_intvl, uIntvl_s);
		  is->max_temp_diff = MAX(ABS(is->oregon_data.temperature-last_temp_reading), is->max_temp_diff);
	  }
	  is->rssi_sum += (rssi_dbm1 + rssi_dbm2)/2;
	  is->lqi_sum += (lqi1 + lqi2)/2;
	  is->rssi_min = MIN(MIN(rssi_dbm1, rssi_dbm2), is->rssi_min);
	  is->rssi_max = MAX(MAX(rssi_dbm1, rssi_dbm2), is->rssi_max);
	  is->lqi_max = MAX(MAX(lqi1, lqi2), is->lqi_max);
	  is->lqi_min = MIN(MIN(lqi1, lqi2), is->lqi_min);
	  last_temp_reading = is->oregon_data.temperature;

}

void do_main_cycle()
{
	int add_delay, first_iter, buffdiff;
	uint8_t res1, res2, pktlen, burst_mnum;
	uint8_t *rx_fifo;
	unsigned int uDiffTime;

	uPrevTime = uOldTime = millis();
	add_delay = ADDITIONAL_DELAY_MS;
	first_iter = 1;
	burst_mnum = 0;

	if (test_mode)
		Msg("");

	// main loop
	while (keep_running) {
		delay(SHORT_DELAY_MS+add_delay);                            //delay to reduce system load
		if (cc1101_oregon.packet_available())		 //checks if a packet is available
		{
		  uCurrTime = millis();
		  if (uCurrTime < uOldTime)
			  uDiffTime = uCurrTime + ~uOldTime + 1;
		  else
			  uDiffTime = uCurrTime - uOldTime;
		  if ( uDiffTime > MSG_TIMEOUT_MS )
		  {
			  uOldTime = uCurrTime;
			  add_delay = 0;
			  burst_mnum = 0;
		  } else {
			  add_delay = ADDITIONAL_DELAY_MS;
			  my_instance->total_reads++;
			  burst_mnum++;
		  }

		  if ((burst_mnum != 1) || first_iter) {
			  // get first message of a burst into the first rx buffer
			  // or get any 3rd and + spurious message of a burst, just to clear cc1101 buffer
			  res1 = cc1101_oregon.get_oregon_raw(rx_fifo1, pktlen1, rssi_dbm1, lqi1);
			  if (burst_mnum > 1)
				  my_instance->mbrst_errors++;
		  } else {
			  // receive the second message of a burst into the second rx buffer
			  res2 = cc1101_oregon.get_oregon_raw(rx_fifo2, pktlen2, rssi_dbm2, lqi2);
			  if (test_mode)
				  Msg("Rx @ %ld.%d s:", uCurrTime/1000,uCurrTime % 1000);
			  if (debug_level > 1)
				  Msg("res1 %d  res2 %d  pktlen1 %u  pktlen2 %u", res1, res2, pktlen1, pktlen2);
			  // pre-set checksum flag for the counters
			  my_instance->oregon_data.cksum_ok = 1;
			  // sometimes decoded data can span a bit longer - cap the pktlen in case 2 bursts are OK
			  // else just take the pktlen of the good burst, and 0 if both bursts are bad
			  if (res1 && res2) {
				  pktlen = MIN(pktlen1, pktlen2);
				  rx_fifo = rx_fifo2;
				  buffdiff = memcmp(rx_fifo1, rx_fifo2, MIN(pktlen, THN122N_PKTLEN_USED_DECODE)); // Note: compare only decoded part
			  }
			  else {
				  pktlen = ((res1)?pktlen1:pktlen2);
#if !PARANOID_NEEDS_BOTH_MESSAGES
				  rssi_dbm1 = rssi_dbm2 = ((res1)?rssi_dbm1:rssi_dbm2);
				  lqi1 = lqi2 = ((res1)?lqi1:lqi2);
				  rx_fifo = ((res1)?rx_fifo1:rx_fifo2);
#endif
				  buffdiff = 0;
			  }

#if PARANOID_NEEDS_BOTH_MESSAGES
			  // check if both Rx bursts are ok, if bursts are sufficiently long, and compare the two buffers
			  if (res1 && res2 && (pktlen >= THN122N_MIN_PKTLEN_FOR_DECODE) && !buffdiff &&
#else
			  // check if at least one Rx burst is ok, and if that burst is sufficiently long
			  if ((res1 || res2) && (pktlen >= THN122N_MIN_PKTLEN_FOR_DECODE) &&
#endif
					  cc1101_oregon.get_oregon_data(rx_fifo, pktlen, &(my_instance->oregon_data)) && my_instance->oregon_data.cksum_ok)
			  {
				  my_instance->good_reads++;
				  my_instance->oregon_data.rssi_dbm = MIN(rssi_dbm1, rssi_dbm2);
				  my_instance->oregon_data.lqi = MAX(lqi1, lqi2);
				  update_global_stats(my_instance);
				  if (debug_level) {
					  Msg("=== Rx stats ====");
					  disp_rx_stats(my_instance);
				  } else {
					  if (test_mode && ((my_instance->total_reads % SKIP_LOG_COUNT) == 1))
						  Msg("Oregon pkt (bad/all) # %lu / %lu ", my_instance->total_reads - my_instance->good_reads, my_instance->total_reads);
				  }
				  my_instance->last_upd_time = time(NULL);
				  if (test_mode) {
					  if (debug_level) {
						 Msg("=== Decoded packet ==");
					  }
					  disp_oregon_data(my_instance, 0);
				  }
			  }
			  if (!res1)
				  my_instance->brst1_errors++;
			  if (!res2)
				  my_instance->brst2_errors++;
			  if (pktlen < THN122N_MIN_PKTLEN_FOR_DECODE)
				  my_instance->pktlen_errors++;
			  if (buffdiff)
				  my_instance->buffmatch_errors++;
			  if (!my_instance->oregon_data.cksum_ok)
				  my_instance->chksum_errors++;
			  if (test_mode)
				Msg("");
		  }
		  first_iter = 0;
		}
		if (clear_stats && add_delay) { // reset statistics has been requested
			clear_stats = 0;
			init_inst_struct(my_instance, 0);
			Msg("Oregon Rx statistics was reset!");
		}
	}
	if (test_mode) {
		Msg("\n=== Oregon Rx statistics ===");
		disp_rx_stats(my_instance);
		Msg("");
	}
}


///////////////////////////////////////////////////////////////////////////
void process_options(int argc, char *argv[])
{
	extern  int     optind, opterr;
	extern  char    *optarg;
	int     c, have_args = 0;

	while ((c = getopt(argc, argv, OPTCHARS)) != EOF)	{
		switch (c) {
		case 'd':
			if (optarg != NULL)
				debug_level = MAX(atoi(optarg),1);
			else
				debug_level = 1;
			break;
		case 'h':
			Usage();
			exit(0);
	    case 'b': // show bare temperature
			bare_temp = 1;
			have_args |= ARG_b;
			break;
	    case 'o': // show oregon data
			show_data = 1;
			have_args |= ARG_o;
			break;
		case 'V':
			show_verbose = 1;
			have_args |= ARG_V;
			break;
		case 'r':
			reset_stats = 1;
			if (optarg != NULL)
				reset_flags = strtol(optarg, NULL, 2);
			else
				reset_flags = 0xff;
			have_args |= ARG_r;
			break;
        case 'K':
            kill_proc = 1;
			have_args |= ARG_K;
            break;
		case 't':
			test_mode = 1;
			have_args |= ARG_t;
			break;
		case 'n':
			data_invalid_timeout = MAX(atoi(optarg),OREGON_DATA_MIN_TIMEOUT_S);
			have_args |= ARG_n;
			break;
		default:
			Usage();
			exit(0);
		}
	}
	if (bare_temp && (have_args != ARG_b)){
	    Msg("Error! -b option can't be used with any other options.");
	    exit(1);
	}
	if (show_data && (have_args != ARG_o)){
	    Msg("Error! -o option can't be used with any other options.");
	    exit(1);
	}
	if (test_mode && (have_args != ARG_t)){
	    Msg("Error! -t option can't be used with any other options.");
	    exit(1);
	}
	if (debug_level > 0 && !test_mode) {
	    Msg("Error! -d option can be used only with -t option");
	    exit(1);

	}
	if (reset_stats && (have_args != ARG_r)){
	    Msg("Error! -r option can't be used with any other options.");
	    exit(1);
	}
	if (kill_proc && (have_args != ARG_K)){
	    Msg("Error! -K option can't be used with any other options.");
	    exit(1);
	}
	if (show_verbose && (have_args != ARG_V)){
	    Msg("Error! -S option can't be used with any other options.");
	    exit(1);
	}
	if (argc - optind) {
	    Usage();
	    exit(1);
	}
	return;
}

///////////////////////////////////////////////////////////////////////////
void sigchld_handler(int signum)	//entered on death of child (SIGCHLD)
{
	int	status;

	waitpid(-1, &status, WNOHANG | WUNTRACED);
}
///////////////////////////////////////////////////////////////////////////
void exit_handler(int signum)	//entered on SIGINT,  SIGTERM & SIGQUIT
{
	keep_running = 0;
	// don't reset handler -- exit on second signal
}

void	resetstats_handler(int signum)
{
	clear_stats = 1;
}

/////////////////////////////////////////////////////////////////////////
#if SHM_DEBUG
void dump_shm(struct shmid_ds *d)
{
	    fprintf(stderr, "\nShared memory info:\n");
	    fprintf(stderr, "\tsize of struct shmid_ds is %d\n", (int)sizeof(struct shmid_ds));
	    fprintf(stderr, "\tmode %o\n", d->shm_perm.mode & 0x1ff);
	    fprintf(stderr, "\tuid %d, gid %d\n",
					d->shm_perm.uid, d->shm_perm.gid);
	    fprintf(stderr, "\tcreated by uid %d, gid %d\n",
					d->shm_perm.cuid, d->shm_perm.cgid);
	    fprintf(stderr, "\tsegment size = %d\n", (int)d->shm_segsz);
	    fprintf(stderr, "\tlast attach time = %s",   ctime(&d->shm_atime));
	    fprintf(stderr, "\tlast detach time = %s",   ctime(&d->shm_dtime));
	    fprintf(stderr, "\tcreation time = %s",   ctime(&d->shm_ctime));
	    fprintf(stderr, "\tnumber of attached procs = %d\n", (int)d->shm_nattch);
	    fprintf(stderr, "\tcreation process id = %d\n", d->shm_cpid);
	    fprintf(stderr, "\tlast user process id = %d\n", d->shm_lpid);
}
#endif

void disp_rx_stats(struct INSTANCE *is)
{
	Msg("Bad/Total received Oregon packets:           %lu / %lu", is->total_reads - is->good_reads, is->total_reads);
//	if (is->good_reads < is->total_reads)
	Msg("Errors: brst1 / brst2 / mburst:              %u / %u / %u", is->brst1_errors, is->brst2_errors, is->mbrst_errors);
	Msg("Errors: pktlen / bfmatch / chksum:           %u / %u / %u", is->pktlen_errors, is->buffmatch_errors, is->chksum_errors);
	if (is->good_reads > 1) {
		Msg("Min/Max time between good packets [s]:       %u / %u", is->min_intvl, is->max_intvl);
		Msg("Max T variation between updates [degC]:      %.1f", is->max_temp_diff);
	}
	if (is->good_reads > 0) {
		if (is->rssi_max >= is->rssi_min)
			Msg("Min/Average/Max RSSI (good packets) [dBm]:  %d / %ld / %d", is->rssi_min, is->rssi_sum / (long)is->good_reads, is->rssi_max);
		if (is->lqi_max >= is->lqi_min)
			Msg("Max/Average/Min LQI (good packets):          %u / %lu / %u", is->lqi_max, is->lqi_sum / is->good_reads, is->lqi_min);
	}
}


void disp_oregon_data(struct INSTANCE *is, int disp_time)
{
	if (disp_time)
		Msg("Time received: %s (%d sec. ago)", nol_ctime(&(is->last_upd_time)), time(NULL)-is->last_upd_time);
	Msg("RSSI min [dBm]: %d  LQI max: %d", is->oregon_data.rssi_dbm, is->oregon_data.lqi);
	Msg("sensor ID: 0x%04X", is->oregon_data.sensor_id);
	Msg("sensor chan: %d", is->oregon_data.channel);
	Msg("roll code: 0x%02X", is->oregon_data.roll_code);
	Msg("batt_low: %d", is->oregon_data.batt_low);
	Msg("cksum_ok: %d", is->oregon_data.cksum_ok);
//	Msg("Time received: %s", ctime(&(is->last_upd_time)));
	Msg("temperature [degC]: %.1f", is->oregon_data.temperature);

}

void init_inst_struct(struct INSTANCE *is, int clear_all)
{
	if (clear_all)
		memset((char *) is, 0, sizeof(is));
	else {
		if (is->reset_flags == 0xff) {
			is->total_reads = 0;
			is->rssi_sum = 0;
			is->lqi_sum = 0;
		}
		if (is->reset_flags & 0x1) {
			is->good_reads = is->total_reads;
			is->brst1_errors = 0;
			is->brst2_errors = 0;
			is->mbrst_errors = 0;
			is->pktlen_errors = 0;
			is->buffmatch_errors = 0;
			is->chksum_errors = 0;
			is->reset_flags |= 0x4 | 0x8;
		}
		if (is->reset_flags & 0x2)
			is->max_intvl = 0;
		if (is->reset_flags & 0x8)
			is->lqi_max = 0;
		if (is->reset_flags & 0x10)
			is->max_temp_diff = 0;
	}
	if ((is->reset_flags & 0x2) || clear_all)
		is->min_intvl = 0xffff;
	if ((is->reset_flags & 0x8) || clear_all)
		is->lqi_min = 127;
	if ((is->reset_flags & 0x4) || clear_all) {
		is->rssi_max = -128;
		is->rssi_min = 127;
	}
	is->reset_flags = 0xff;
}

void init_HW()
{
	//------------- hardware setup ------------------------

	wiringPiSetup();			//setup wiringPi library

	cc1101_oregon.begin(debug_level);			//setup cc1101 RF IC
	cc1101_oregon.sidle();

	if (test_mode)
		cc1101_oregon.show_main_settings();
	if (debug_level > 1)
		cc1101_oregon.show_register_settings();

	cc1101_oregon.receive();
}

/////////////////////////////////////////////////////////////////////////
int get_shm_info()
{
	int	flag;
	struct	shmid_ds ds;

	flag = IPC_CREAT | 0666;
	if ((shmid = shmget(OREAD_KEY, SHMEM_SIZE, flag)) == -1) {
	    Msg("Cannot get shared memory (%s). Ending!", strerror(errno));
	    return FATALERR;
	}
	if (shmctl(shmid, IPC_STAT, &ds) != 0) {
	    Msg("Cannot stat shared memory (%s). Ending!", strerror(errno));
	    return FATALERR;
	}
	if (ds.shm_nattch > 0) {
	    Msg("One %s process is already active.", program);
#if SHM_DEBUG
	    dump_shm(&ds);
#endif
	    return FATALERR;
	}
	if ((shmaddr = shmat(shmid, 0, 0)) == (void *)-1) {
	    Msg("Cannot attach shared memory (%s). Ending!", strerror(errno));
#if SHM_DEBUG
	    dump_shm(&ds);
#endif
	    return FATALERR;
	}
	my_instance = (struct INSTANCE *)shmaddr;
	init_inst_struct(my_instance, 1);
	my_instance->pid = getpid();
	my_instance->data_invalid_timeout = data_invalid_timeout;
	return SUCCESS;
}
/////////////////////////////////////////////////////////////////////////
int run_as_background()
{
	int	x;
	char	*err_string;

	x = fork();
	switch (x) {
	    case -1:		// error
	    	err_string = strerror(errno);
	    	Msg( "Can't fork (%s)! Ending!\n", err_string);
	    	return FATALERR;
	    default:		// parent
	    	exit(0);
	    case 0:		//child
	    	setsid();
			if (get_shm_info() == FATALERR)
				return FATALERR;
			init_HW();
			log2syslog++;
			if (log2syslog > 0)
				fclose(stderr);
			fclose(stdout);
			fclose(stdin);
			syslog(LOG_INFO, "v%s daemon started\n",VERSION_SW);
			do_main_cycle();
			cc1101_oregon.end();
			syslog(LOG_INFO, "v%s daemon ended.\n", VERSION_SW);
			break;
	}
	return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////
void interact_with_daemon()
{
	int	i;
	int	flag, cfg_found=0;
	struct	INSTANCE *is;
	time_t curr_time;

	if ((shmid = shmget(OREAD_KEY, SHMEM_SIZE, 0)) != -1) {
	    flag = ((kill_proc || reset_stats) ? 0 : SHM_RDONLY);
	    if ((shmaddr = shmat(shmid, 0, flag)) != (void *)-1) {
		    is = (struct INSTANCE *)shmaddr;
		    if (is->pid) {
				if (kill_proc) {
					if (kill(is->pid, SIGTERM) != 0) {
						if (errno == ESRCH) {	// no process
							Msg("Process %d not found!",  is->pid);
						} else {
							Msg("Can't terminate process %d (%s)!",
									is->pid, strerror(errno));
							shmdt(shmaddr);
							return;
						}
					} else
						Msg("Process %d terminated.",  is->pid);
					shmdt(shmaddr);
					if (shmctl(shmid, IPC_RMID, NULL) != 0) {
					    Msg("Cannot remove shared memory (%s)!", strerror(errno));
					}
					return;
				}
				curr_time  =  time(NULL);
				if (show_verbose)
				{
					Msg("\nDaemon process active: %d",
						is->pid);
					Msg("Timeout for Oregon data to be claimed invalid: %d s", is->data_invalid_timeout);
					Msg("");
					if (is->last_upd_time > 0) {
						if (is->good_reads > 0) {
							Msg("=== Rx stats ====");
							disp_rx_stats(is);
						}
						Msg("=== Last update ==");
						disp_oregon_data(is, 1);
						if (curr_time - is->last_upd_time >= is->data_invalid_timeout)
							Msg("Warning: The update is too old!");
					}
					else
						Msg("No data yet!");
				}
				if (show_data || bare_temp)
				{
					if (show_data) {
						if (is->last_upd_time > 0) {
							printf("Last update: %s (%d sec. ago)\n", nol_ctime(&(is->last_upd_time)), curr_time-is->last_upd_time);
							printf("Outdoor temperature [degC]: %.1f\n", is->oregon_data.temperature);
						} else
							printf("No data yet!\n");
					}
					if (curr_time - is->last_upd_time < is->data_invalid_timeout) {
						if (bare_temp)
							printf("%.1f\n", is->oregon_data.temperature);
					} else {
						if (bare_temp)
							printf("U\n"); // for an RRD database - unknown value
						else {
							if (is->last_upd_time > 0)
								printf("Invalid reading (last update too old)!\n");
						}
					}
				}
				if (reset_stats) {
					is->reset_flags = reset_flags;
					if (kill(is->pid, SIGUSR1) != 0) // send signal to reset stats
						Msg("Could not reset statistics of daemon process %d!",  is->pid);
					else
						Msg("Daemon process %d - statistics were reset.",  is->pid);
				}
		    } else
		    	Msg("Error in shared memory data - daemon PID is 0!");
			shmdt(shmaddr);
			return;
	    }
    }
    Msg("No %s processes active.", program);
    if (kill_proc) {
		Msg("No process terminated.");
    }
}

char   *nol_ctime(const time_t *timep)
{
	struct tm *ptm = localtime(timep);
	strftime(strbuf, LINELEN, "%c", ptm);
	return strbuf;
}

/////////////////////////////////////////////////////////////////////////////
void Msg(const char *fmt, ...)
{
	va_list ap;
	char	errmsg[LINELEN];

	va_start(ap, fmt);
	vsnprintf(errmsg, LINELEN-1, fmt, ap);
	va_end(ap);
	if (log2syslog > 0)
		syslog(LOG_ERR, "%s\n", errmsg);
	else
		fprintf(stderr, "%s\n", errmsg);
}
