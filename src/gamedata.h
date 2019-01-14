#ifndef UDS_GAMEDATA_H
#define UDS_GAMEDATA_H

#include <cstddef>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <list>
#include "module.h"
#include "math.h"
//#ifdef SDL
#include "gui.h"
//#endif
#include "can.h"

using namespace std;

#define MODE_SIM    0
#define MODE_LEARN  1
#define MODE_ATTACK 2

#define CONFIDENCE_THRESHOLD 0.6 // 60%

#define CAN_DELAY 1000 * 10

enum Car_Type {
	NONE, // scan all possibilities
	AUTO, // fist match defines car type (default)
	VAG,
	//BMW,
	GM,
	CHRYSLER,
	MB,
	RENAULT,
	NISSAN
};

class Gui;
class Module;

class GameData
{
  public:
    GameData();
    ~GameData();
    Module *get_module(int);
    Module *get_possible_module(int);
    vector<Module> modules;
    vector<Module> possible_modules;
    vector<Module *> get_active_modules();
    vector<Module *> get_possible_active_modules();
    void setMode(int);
    int getMode() { return mode; }
    void setVerbose(int v) { verbose = v; }
    int getVerbose() { return verbose; }
    void setCan(Can *c) { canif = c; }
    Can *getCan() { return canif; }
    void processPkt(canfd_frame *);
    string frame2string(canfd_frame *);
    void setGUI(Gui *g) { _gui = g; }
    void Msg(string);
    bool SaveConfig();
    void nextMode();
    void launchPeach();
    void processCan();
    int string2hex(string);
    int string2int(string);
	int random_packet=1;
	char conf_file[61];
	int autoresponse=0; // respond 0x11 to every 0x22 packet
	int autowrite=0;	// confirm every 0x2E packet
	Car_Type car_type=AUTO;
  private:
    void HandleSim(canfd_frame *, int);
	void Handle_TP20(canfd_frame *, Module *);
    void LearnPacket(canfd_frame *);
    void pruneModules();
    Module *isPossibleISOTP(canfd_frame *);
    void processLearned();
	int process_TP20(Module *it);
    int mode = MODE_SIM;
    int verbose = 0;
    int _lastTicks = 0;
    Can *canif = NULL;
    Gui *_gui = NULL;
	canfd_frame multiquery; // buffer first packet to play it later
	int multiquery_cnt=0;
	Protocol _lastprotocol=UDS;
};

#endif
