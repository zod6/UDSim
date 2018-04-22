#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>
#include "input.h" 

struct termios orig_termios;
char input[101];
int input_pos=0;

void processInput(){
	char c;

	if (kbhit()) {
		c=getch();
		if(c=='q'){
			exit(0);
		} else if(c=='s'){
			gd.SaveConfig();
		} else if(c=='p'){ // works only for single-packet response
			gd.random_packet^=1;
			cout << "random: " << gd.random_packet << endl;
		} else if( 0 && c=='i'){
			Module *module = gd.get_module(1917);
			if(module){
				vector<CanFrame *> it = module->getHistory();
				for(vector<CanFrame*>::iterator i = it.begin(); i != it.end(); ++i){
					CanFrame *old = *i;
					cout << old->str() << endl;
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
			if(input[input_pos-1]=='\n' || input_pos>=100){
				if(input_pos<101) input[input_pos]=0; else input[100]=0;
				if(input[3]=='#' && input_pos>=8){
					input[3]=0;
					Module *module = gd.get_module(strtol(input,NULL,16));
					if(module){
						input[3]='#';
						module->addPacket_front(input);
						cout << "packet added: " << module->getArbId() << "#" << input+4 << endl;
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

int kbhit(){
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

int getch(){
    int r;
    unsigned char c;
    if ((r = read(0, &c, sizeof(c))) < 0) return r;
    else return c;
}

