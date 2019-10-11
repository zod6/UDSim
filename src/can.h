#ifndef UDS_CAN_H
#define UDS_CAN_H

#include <unistd.h>
#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <string.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#include "canframe.h"
#include "timer.h"

using namespace std;

#define CANID_DELIM '#'
#define DATA_SEPERATOR '.'

class CanFrame;

class Can
{
  public:
    Can(char *);
    ~Can();
    bool Init();
    string getIfname() { return ifname; }
    int parse_canframe(char *, struct canfd_frame *);
    unsigned char asc2nibble(char);
    vector <CanFrame *>getPackets();
    void sendPackets(vector <CanFrame *>);
	void sendPackets(CanFrame *);
	void sendPackets_TP20(vector <CanFrame *>);
    void sendPackets(vector <CanFrame *>, int);
	void periodic_add(vector <CanFrame *> cfs){ _periodic_queue.insert(_periodic_queue.end(), cfs.begin(), cfs.end()); }
	void periodic_add(CanFrame *cf){ _periodic_queue.push_back(cf); }
	void periodic_start(int interval){ timer_start(can_periodic_send, interval); }
	void periodic_end(){ _periodic_queue.clear(); } // when cleared, thread exits
	deque<CanFrame*> periodic_get(){ return _periodic_queue; }

  private:
    string ifname;
    int _canfd;
    struct sockaddr_can _addr;
	deque<CanFrame*> _periodic_queue;
//	mutex _periodic_mutex;
};


#endif
