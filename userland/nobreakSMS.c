/*
 * Userland application for SMS brand uninterruptible power supply.
 * Aplicativo para nobreak SMS.
 *
 * Peter Senna Tschudin - peter.senna @ gmail.com
 *
 * This code is distributed under Version 2 of the GNU General Public
 * License.
 *
 * Version 0.1
 */

#include <stdio.h>	/* Standard input/output definitions */
#include <stdlib.h>	/* strtol() */
#include <stdbool.h>	/* bool type */
#include <string.h>	/* String function definitions */
#include <unistd.h>	/* UNIX standard function definitions */
#include <fcntl.h>	/* File control definitions */
#include <errno.h>	/* Error number definitions */
#include <termios.h>	/* POSIX terminal control definitions */

#define DEFAULT_FIRST_RETURN 61 /* Value of first char sent from device */
#define DEFAULT_LAST_RETURN 13 /* Value of last char sent from device */
#define BAUDRATE B2400
#define DEFAULT_TTY "/dev/ttyUSB0"
#define QUERY_SIZE 7
#define RESULT_SIZE 18
#define HUMAN_VALUES 7

/*TODO: 6 Fucntion prototypes */
/*TODO: 7 code documentation */

struct smsstatus {
	/* Boolean states */
	bool beepon;
	bool shutdown;
	bool test;
	bool upsok;
	bool boost;
	bool onacpower;
	bool lowbattery;
	bool onbattery;

	/* (not really) Floating point values */
	float lastinputVac;
	float inputVac;
	float outputVac;
	float outputpower; /* Unit: % */
	float outputHz;
	float batterylevel; /* Unit: % */
	float temperatureC;
};

int open_config_tty(char *tty)
{
	int fd;
	struct termios options;

	fd = open(tty, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd == -1)
		return fd; /* ERROR! */

//	fcntl(fd, F_SETFL, 0); /* Block and wait for data */
	fcntl(fd, F_SETFL, FNDELAY); /* Return immediately */

	/* BPS */
	cfsetispeed(&options, BAUDRATE);
	cfsetospeed(&options, BAUDRATE);

	/* 8N1 */
	options.c_cflag &= ~PARENB;
	options.c_cflag &= ~CSTOPB;
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= CS8;

	/* RAW DATA MODE */
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	/* APPLY SETTINGS */
	tcsetattr(fd, TCSANOW, &options);

	return fd;
}

void close_tty(int fd)
{
	close (fd);
}

int send_query (int fd, int query[])
{
	char buf[QUERY_SIZE];

	sprintf(buf, "%c%c%c%c%c%c%c", query[0], query[1], query[2], query[3],
		query[4], query[5], query[6]);

	write(fd, buf, QUERY_SIZE);

	return 0;
}

/* This was reading only a char at a time
int get_results (int fd, int rawvalues[])
{
	int i;
	int failcount = 0;
	char buf[2];

	for (i = 0;i < RESULT_SIZE; i++){

		read (fd, buf, 1);
		// 
		// If this is the first char, check 
		// if it is the expected first char.
		// If not wait until get it.
		//
		if (failcount > 32){
			perror ("Error reading from tty");
			return -2;
		}
		if (i == 0){
			if (buf[0] != DEFAULT_FIRST_RETURN){
				i--;
				failcount++;
				continue;
			}
		}
		rawvalues[i] = buf[0];

	}

//	for (i = 0;i < RESULT_SIZE; i++){
//		printf ("%02x\n",rawvalues[i]);
//	}

	return 0;
}
*/

/* Looks simpler and more efficient */
int get_results2 (int fd, int rawvalues[])
{
	int i;
	int nbytes = 0;
	char buf[128];

	while (nbytes <  RESULT_SIZE){
		usleep(15000);
		nbytes += read(fd, (buf + nbytes), 64);
		//printf ("nbytes=%d\n",nbytes);
		if (nbytes < 0){
			/* Read error */
			return nbytes;
		}
	}


	for (i = 0;i < RESULT_SIZE; i++){
		rawvalues[i] = buf[i];
	}

	return 0;
}

int check_results(const int rawvalues[])
{
	char buf[2], *endptr;
	float value;
	int i, j, error = 0;

	struct range {
		int min;
		int max;
	};

	struct range smsranges[HUMAN_VALUES];

	/* lastinputVac */
	smsranges[0].min = 0;
	smsranges[0].max = 270;

	/* inputVac */
	smsranges[1].min = 0;
	smsranges[1].max = 270;

	/* outputVac */
	smsranges[2].min = 0;
	smsranges[2].max = 270; /* Should this change to 135? */

	/* outputpower */
	smsranges[3].min = 0;
	smsranges[3].max = 100; /* Should this change to 150? */

	/* outputHz */
	smsranges[4].min = 40;
	smsranges[4].max = 70;

	/* batterylevel */
	smsranges[5].min = 0;
	smsranges[5].max = 100;

	/* temperatureC */
	smsranges[6].min = -20;
	smsranges[6].max = 70;

	for (i = 0, j = 0; i < HUMAN_VALUES; i++){
		j += 1; /* WHY? */
		sprintf (buf, "0x%02x%02x", (unsigned char)rawvalues[j], 
			(unsigned char)rawvalues[(j+1)]);
		j += 1; /* WHY? */

//		printf("Value: %s\n", buf);

		value = strtol(buf, &endptr, 16); /* 16 == hex */
		value /= 10;
//		printf("Value: %3.2f\n", value);
		if ((value > smsranges[i].max) || (value < smsranges[i].min)){
			error++;
		}
	}
	return error;
}

int results_to_human(int rawvalues[], struct smsstatus *results)
{
	int i, j;
	char buf[2], *endptr;
	unsigned char byte, mask;

	/* To make it simpler to use values in sequencial order */
	float *ordered_values[HUMAN_VALUES] = { &results->lastinputVac, 
		&results->inputVac, &results->outputVac, &results->outputpower,
		&results->outputHz, &results->batterylevel, 
		&results->temperatureC };

	/* To make it simpler to use values in sequencial order */
	bool *ordered_bits[8] = { &results->beepon, &results->shutdown, 
		&results->test, &results->upsok, &results->boost, 
		&results->onacpower, &results->lowbattery,
		&results->onbattery };

	for (i = 0, j = 0; i < HUMAN_VALUES; i++){
		j += 1; /* WHY? */
		sprintf (buf, "0x%02x%02x", (unsigned char)rawvalues[j], 
			(unsigned char)rawvalues[(j+1)]);
		j += 1; /* WHY? */

		*ordered_values[i] = strtol(buf, &endptr, 16); /* 16 == hex */
		*ordered_values[i] /= 10;
	}

	byte = rawvalues[15];
	mask = 1;
	for (i = 0; i < 8; i++){
		*ordered_bits[i] = ((byte & (mask << i)) != 0) ? true : false;
	}

	return 0;
}

void print_values(struct smsstatus *results)
{
	int i;
	/*
	 * Original strings in pt_BR
	 * char *desc[7] = { "UltimaTensao", "TensaoEntrada", 
         *	"TensaoSaida", "PotenciaSaida", "FrequenciaSaida", 
	 *	"PorcentagemTensaoBateria", "Temperatura" };
	 */
	char *desc[HUMAN_VALUES] = { "Last Input(Vac)", "Input(Vac)", 
		"Output(Vac)", "Output Power(%)", "Output(Hz)", 
		"Battery level(%)", "Temperature(C)"};

	/*
	 * Original strings in pt_BR
	 * char *byte_desc[8] = {"Beep Ligado", "Shutdown Ativo", 
	 *	"Teste Ativo", "UpsOK", "Boost", "ByPass", 
	 *	"Bateria Baixa", "Bateria Ligada"};
	 */
	char *bits_desc[8] = { "Beep on", "Active shutdown", 
		"Active test", "UPS OK", "Boost ON", "On AC Power",
		"Low battery", "On battery power" };

	/* To make it simpler to use values in sequencial order */
	float *ordered_values[HUMAN_VALUES] = { &results->lastinputVac, 
		&results->inputVac, &results->outputVac, &results->outputpower,
		&results->outputHz, &results->batterylevel, 
		&results->temperatureC };

	/* To make it simpler to use values in sequencial order */
	bool *ordered_bits[8] = { &results->beepon, &results->shutdown, 
		&results->test, &results->upsok, &results->boost, 
		&results->onacpower, &results->lowbattery,
		&results->onbattery };

	/* The value of lastinputVac is not relevant, so it is not printed.
         * This is why i = 1.
	 */
	for (i = 1; i < HUMAN_VALUES; i++){
		printf ("%20s:%3.2f\n", desc[i], *ordered_values[i]);
	}


	for (i = 0; i < 8; i++){
		printf ("%20s:%d\n", bits_desc[i], *ordered_bits[i]);
	}
}

int main (int argc, char *argv[])
{
	int fd;
	int opt;
	int error;
	int errorcount = 0;
	int rawvalues[RESULT_SIZE];
	char *tty = DEFAULT_TTY;
	bool starttest = false;
	bool aborttest = false;
	bool switchbeep = false;
	struct smsstatus results;

/***************************************************************************
 * UPS QUERIES START
 ***************************************************************************/

	/* Interrupt the battery test. No return. */
	int query1[QUERY_SIZE] ={'\x44', /* D */
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xc0',
				 '\x0d'};

	/* Return strange values
	 * 0x3b (;), 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 
         *           0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xe5, 0x0d;
	 */
	int query2[QUERY_SIZE] ={'\x46', /* F */
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xbe',
				 '\x0d'};

	/* Does nothing? */
	int query3[QUERY_SIZE] ={'\x47', /* G */
				 '\x01',
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xbb',
				 '\x0d'};

	/* Return model name and firmware version?
	 * 0x3a (:), 0x53 (S), 0x45 (E), 0x4e (N), 0x4f (O), 0x49 (I), 
	 * 0x44 (D), 0x41 (A), 0x4c (L), 0x20, 0x20, 0x20, 0x20, 0x37 (7), 
	 * 0x2e (.), 0x30 (0), 0x62 (b), 0x0d;
	 */
	int query4[QUERY_SIZE] ={'\x49', /* I */
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xbb',
				 '\x0d'};

	/* Switch the buzzer ON/OFF. No return. */
	int query5[QUERY_SIZE] ={'\x4d', /* M */
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xb7',
				 '\x0d'};

	/* Return standard values
	 * 0x3d (=)	- Just a mark
	 * 0x08		- 0XAA	 - 
	 * 0x34 (4)	- 0X  AA - lastinputVac
	 * 0x08		- 0XBB   -
	 * 0x34 (4)	- 0X  BB - inputVac
	 * 0x04		- 0XCC   -
	 * 0x38 (8)	- 0X  CC - outputVac
	 * 0x01		- 0XDD   -
	 * 0x22 (")	- 0X  DD - outputpower
	 * 0x02		- 0XEE   -
	 * 0x58 (X)	- 0X  EE - outputHz
	 * 0x03		- 0XFF   -
	 * 0xe8		- 0X  FF - batterylevel
	 * 0x01		- 0XGG   -
	 * 0x7c (|)	- 0X  GG - temperatureC
	 * 0x29 ())	- HH     - State bits (beepon, shutdown, test, upsok, 
	 * 0x01		- ??	 - ??	boost, onacpower, lowbattery, onbattery)
	 * 0x0d		- Just a mark
	 */
	int query6[QUERY_SIZE] ={'\x51', /* Q */
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xff',
				 '\xb3',
				 '\x0d'};

	/* Test the battery for 10 seconds. No return. */
	int query7[QUERY_SIZE] ={'\x54', /* T */
				 '\x00',
				 '\x10',
				 '\x00',
				 '\x00',
				 '\x9c',
				 '\x0d'};

/***************************************************************************
 * UPS QUERIES END
 ***************************************************************************/

	/* Command line parser */
	while ((opt = getopt(argc, argv, "abst:")) != -1) {
			switch (opt) {
				case 'a':
					aborttest = true;
				break;
				case 'b':
					switchbeep = true;
		           	break;
				case 's':
					starttest = true;
				break;
				case 't':
					tty = optarg;
		           	break;
				default: /* '?' */
					fprintf(stderr, "Usage: %s\n\t-s: Start battery test\n\t-a: "
						"Abort battery test\n\t-b: Switch buzzer ON/OFF\n\t-t: Path to TTY (Ex: /dev/ttyUSB0)\n", argv[0]);
					exit(EXIT_FAILURE);
			}
	}

	/* Avoid calling start and abort battery test simultaneosly */
	if (starttest && aborttest){
		printf ("ERROR: Can't start and abort battery test at same time.\n");
		exit(EXIT_FAILURE);
	}

	if ( (fd = open_config_tty(tty)) < 0){
		perror("open_port: Unable to open DEFAULT_TTY - ");
		return -1;
	}

	if (switchbeep)
		send_query (fd, query5);

	if (starttest)
		send_query (fd, query7);

	if (aborttest)
		send_query (fd, query1);

	/* Device Startup
         * This is done on the official software but looks not necessary
	 *

	send_query (fd, query3);
	send_query (fd, query4);
	get_results2(fd, rawvalues);
	send_query (fd, query4);
	get_results2(fd, rawvalues);
	send_query (fd, query2);
	get_results2(fd, rawvalues);
	send_query (fd, query6);
	get_results2(fd, rawvalues);

	*/

	while (errorcount < 2 ){

		send_query (fd, query6);
		if (get_results2(fd, rawvalues) < 0){
			perror("Error reading tty. ");
			return -1;
		}

//		printf("Errorcount: %d\n", errorcount);
		if ((error = check_results(rawvalues)) > 0){
//			printf("Error: %d\n", error);
			errorcount++;
			usleep (250000);
			continue;
		} else
			break;

	}
	if (errorcount > 1){
		perror("get_results: tty was opened but is sending incorrect values - ");
		return error;
	}


	results_to_human(rawvalues, &results);

	close_tty(fd);

	print_values(&results);

	exit(EXIT_SUCCESS);
}
