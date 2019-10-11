#include <iostream>
#include <getopt.h>
#include "udsim.h"
#include "input.h"
#include "timer.h"

using namespace std;

/* Globals */
GameData gd;

void Usage(string msg) {
 cout << msg << endl;
 cout << "Usage: udsim [options] <can-if>" << endl;
 cout << "     -c <config file>    Configuration file for simulator" << endl;
 cout << "     -l <logfile>        Parse candump log to generate learning DB" << endl;
 cout << "     -f                  Fullscreen" << endl;
 cout << "     -g                  without GUI" << endl;
 cout << "     -z                  answer 0x01010101 to every unknown 'read data' request (can be toggled when running)" << endl;
 cout << "     -t                  car manufacturer. possible answers are 'NONE', 'AUTO', 'VAG', 'BMW', 'GM', 'GM2', 'CHRYSLER', 'MB', 'RENAULT', 'NISSAN', 'VOLVO', 'PSA', 'SUBARU'" << endl;
 cout << "     -x                  send 'confirm' to every 'write data' request (can be toggled when running)" << endl;
 cout << "     -m min_packet_addr. filter out packets where address < (min_packet_addr<<8)" << endl;
 cout << "     -o <0-100>          confidence threshold (percentage of UDS packages to keep module. default 60%)" << endl;
 cout << "     -r                  Don't repair frames" << endl;
 cout << "     -v                  Increase verbosity" << endl;
 cout << endl;
}

int main(int argc, char *argv[]) {
	int running = 1, opt, res;
	int verbose = 0;
	int nogui = 0;
	bool process_log = false;
	Gui gui;
	LogParser log;
	ConfigParser conf;
#ifndef SDL
	nogui=1;
#endif

	cout << "UDSim " << VERSION << endl;

	while ((opt = getopt(argc, argv, "vfc:l:gt:m:o:7rh?")) != -1) {
		switch(opt) {
			case 'v':
				verbose++;
				break;
			case 'c':
				cout << "Loading " << optarg << endl;
				if(!conf.parse(optarg)) {
					exit(10);
				}
				cout << "Conf parsed" << endl;
				strncpy(gd.conf_file, optarg, 60);
				break;
			case 'l':
				log.setLogFile(optarg);
				process_log = true;
				gd.setMode(MODE_LEARN);
				break;
			case 'f':
				gui.setFullscreen(true);
				break;
			case 'g':
				nogui=1;
				break;
			case 'z':
				gd.autoresponse=1;
				break;
			case 'x':
				gd.autowrite=1;
				break;
			case 't':
				if(!strcasecmp(optarg, "NONE")) gd.car_type=NONE;
				else if(!strcasecmp(optarg, "AUTO")) gd.car_type=AUTO;
				else if(!strcasecmp(optarg, "VAG")) gd.car_type=VAG;
				else if(!strcasecmp(optarg, "BMW")) gd.car_type=BMW;
				else if(!strcasecmp(optarg, "GM")) gd.car_type=GM;
				else if(!strcasecmp(optarg, "GM2")) gd.car_type=GM2;
				else if(!strcasecmp(optarg, "CHRYSLER")) gd.car_type=CHRYSLER;
				else if(!strcasecmp(optarg, "MB")) gd.car_type=MB;
				else if(!strcasecmp(optarg, "RENAULT")) gd.car_type=RENAULT;
				else if(!strcasecmp(optarg, "NISSAN")) gd.car_type=NISSAN;
				else if(!strcasecmp(optarg, "VOLVO")) gd.car_type=VOLVO;
				else if(!strcasecmp(optarg, "PSA")) gd.car_type=PSA;
				else if(!strcasecmp(optarg, "SUBARU")) gd.car_type=SUBARU;
				else Usage("Unknown manufacturer none/auto/vag/bmw/gm/gm2/chrysler/mb/renault/nissan/volvo/psa/subaru");
				break;
			case 'm':
				gd.min_packet_addr=atoi(optarg);
				break;
			case 'o':
				gd.confidence_threshold=atoi(optarg);
				break;
			case '7':
				gd.multiple_responses_7df=1; // not needed. yet
				break;
			case 'r':
				gd.config_repair_frame=false;
				break;
			default:
				Usage("Help Menu");
				exit(1);
		}
	}

	if (optind >= argc) {
		Usage("You must specify at least one can device");
		exit(2);
	}

	gd.setCan(new Can(argv[optind]));
	if(!gd.getCan()->Init()) {
		cout << "Failed to initialize CAN.  Aborting." << endl;
		return 20;
	}

	cout << endl << "keyboard shortcuts:" << endl;
	cout << "'p': toggle between persistent and random answers" << endl;
	cout << "'r': reload config" << endl;
	cout << "'s': save config" << endl;
	cout << "'z': answer 0x01010101 to every unknown read data response" << endl;
	cout << "'x': confirm every write data reponse" << endl;
	cout << "'q': quit" << endl << endl;
	cout << "to define new responses: '[src,]640#f1100862d5500001,640#f121000003fffff'" << endl << endl;

	if(gd.min_packet_addr) printf("filter: addr < 0x%3X\n\n", gd.min_packet_addr<<8);

	gui.setVerbose(verbose);
	if(!nogui){
		res=gui.Init();
		if(res < 0) nogui=1; // exit(3);
		else gd.setGUI(&gui);
	}

	if(!process_log){
		gd.setMode(MODE_SIM);
//		timer_start(do_something, 1000);
	}
	gd.setVerbose(verbose);

	set_conio_terminal_mode();

	while(running) {
		if(!nogui){
			running = gui.HandleEvents();
			gui.HandleAnimations();
		}

		processInput();

		if(process_log) {
			if(log.Eof()) {
				process_log = false;
				gd.setMode(MODE_SIM);
//				timer_start(do_something, 1000);
				if(!nogui) gui.Redraw();
			} else {
				gd.Msg(log.processNext());
			}
		} else {
			gd.processCan();
		}
	}
}

