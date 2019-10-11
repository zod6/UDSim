#include "timer.h"
#include "gamedata.h"
#include <chrono>
#include <thread>
#include <iostream>

extern GameData gd;

void timer_start(std::function<bool(void)> func, unsigned int interval){
	std::thread([func, interval](){
	while (true){ 
		if(func()==false) return;
		std::this_thread::sleep_for(std::chrono::milliseconds(interval));
	}
	}).detach();
}

bool can_periodic_send(){
	//Can *canif;
	deque<CanFrame*> periodic_queue=gd.getCan()->periodic_get(); 
	if(periodic_queue.empty()) return false;
	//cout << "X: " << periodic_queue.front()->str() << endl;
	for(auto x: periodic_queue){
		gd.getCan()->sendPackets(x);
		usleep(8000);
	}
	// cfs->pop_front();
	return true;
}

