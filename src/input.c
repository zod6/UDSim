#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>
#include "input.h" 

#define INPUT_LEN 500
struct termios orig_termios;
char input[INPUT_LEN+1];
int input_pos=0;

extern GameData gd;

void processInput(){
	unsigned char c;
	unsigned int r;
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);

	if (select(1, &fds, NULL, NULL, &tv)) {
		if ((r = read(0, &c, sizeof(c))) < 0) c=r;

		if(c=='q'){
			exit(0);
		} else if(c=='s'){
			gd.SaveConfig();
		} else if(c=='p'){ // works only for single-packet response
			gd.random_packet^=1;
			cout << "random: " << gd.random_packet << endl;
		} else if( 0 && c=='i'){ // some debugging
			Module *module = gd.get_module(0x222);
			if(module){
				vector<CanFrame *> it = module->getHistory();
				for(vector<CanFrame*>::iterator i = it.begin(); i != it.end(); ++i){
					CanFrame *old = *i;
					cout << old->str() << " | " << module->getProtocol() << endl;
				}
			}
		} else if(c=='r'){
			ConfigParser conf;
			gd.modules.clear();
			if(!conf.parse(gd.conf_file)) exit(10);
			cout << "conf reloaded" << endl;
		} else if(c=='z'){
			gd.autoresponse^=1;
			cout << "autoresponse=" << gd.autoresponse << endl;
		} else if(c=='x'){
			gd.autowrite^=1;
			cout << "autowrite=" << gd.autowrite <<  endl;
		} else {
			input[input_pos++]=c;
			input[input_pos]=0;
			if(input[input_pos-1]=='\n' || input_pos>=INPUT_LEN){
				uint8_t offset=0;
				int src=0;
				if(input_pos<=INPUT_LEN) input[input_pos-1]=0; else input[INPUT_LEN]=0;
				if(input_pos>=3 && input[3]==','){ // # src,dst#xxxxxx // for some TP20 packets
					input[3]=0;
					src=strtol(input,NULL,16);
					offset=4;
				}
				if(input_pos>=(8+offset) && (input[3+offset]=='#' || input[8+offset]=='#')){
					char mod[9];
					memcpy(mod, input+offset, 8);
					if(mod[3]=='\n') mod[3]=0; else mod[8]=0;
					Module *module = gd.get_module(strtol(mod,NULL,16), src);
					if(module){
						if(gd.getCan()->periodic_get().size()>0 && !offset && input[4]>0x31){ // GMLAN periodic response active
							CanFrame *cf = new CanFrame(input+offset);
							gd.getCan()->periodic_end();
							gd.getCan()->periodic_add(cf);
							gd.getCan()->periodic_add(cf->queue); // if exists
							cout << "GMLAN periodic added: " << input+offset << " (" << std::hex << src << ")" << endl;
						} else { // normal packet
							module->addPacket_front(input+offset);
							cout << "packet added: " << input+offset << " (" << std::hex << src << ")" << endl;
						}
					}
				}
				input_pos=0;
			}
		}
	}
}

void reset_terminal_mode(){
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode(){
    struct termios new_termios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
	memcpy(&new_termios, &orig_termios, sizeof(new_termios));

	//turn off canonical mode
	new_termios.c_lflag &= ~ICANON;
	//minimum of number input read.
	new_termios.c_cc[VMIN] = 1;

    /* register cleanup handler, and set the new terminal mode */
    atexit(reset_terminal_mode);
//    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}

