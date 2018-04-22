#ifndef INPUT_H
#define INPUT_H

#include "udsim.h"

extern GameData gd;

void processInput(void);
int kbhit();
int getch();
void set_conio_terminal_mode();

#endif

