#include "gamedata.h"
#include <fstream>

#define type_is(type) (car_type==type || car_type==NONE || car_type==AUTO)
#define set_car_type(type, str, set_responders) { \
		if(set_responders==true){ \
			it->setPositiveResponderID(responder->getArbId()); \
			it->setNegativeResponderID(responder->getArbId()); \
		} \
		if(responder) responder->setResponder(true); \
		if(car_type==AUTO){ \
			car_type=type; \
			cout << "Manufacturer found: " << str << endl; \
		} \
	}

extern GameData gd;

GameData::GameData() { }

GameData::~GameData() { }

Module *GameData::get_module(int id) {
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    if(it->getArbId() == id) return &*it;
  }
  return NULL;
}

// TP20 can have multiple id, resp_id combinations
Module *GameData::get_module(int id, int resp_id) {
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    if(it->getArbId() == id && (it->getProtocol()!=TP20 || resp_id==0 || it->getPositiveResponder()==resp_id)) return &*it;
  }
  return NULL;
}

vector <Module *> GameData::get_active_modules() {
  vector<Module *> active_modules;
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    if(it->isResponder() == false) active_modules.push_back(&*it);
  }
  return active_modules;
}

/* Same as get_active_modules but designed for learning mode */
vector <Module *> GameData::get_possible_active_modules() {
  vector<Module *> possible_active_modules;
  for(vector<Module>::iterator it = possible_modules.begin(); it != possible_modules.end(); ++it) {
    if(it->isResponder() == false) possible_active_modules.push_back(&*it);
  }  
  return possible_active_modules;
}


Module *GameData::get_possible_module(int id) {
  for(vector<Module>::iterator it = possible_modules.begin(); it != possible_modules.end(); ++it) {
	if(it->getArbId() == id && (it->getProtocol()!=TP20 || it->getPositiveResponder()==-1 || _tp20_last_requester==0 || it->getPositiveResponder()==(int)_tp20_last_requester)) return &*it;
  }
  return NULL;
}

void GameData::setMode(int m) {
  switch(m) {
    case MODE_SIM:
      Msg("Switching to Simulator mode");
      if(_gui) _gui->setStatus("Simulation Mode");
      if(mode == MODE_LEARN && possible_modules.size() > 0) { // Previous mode was learning, update
        Msg("Normalizing learned data");
        GameData::processLearned();
      }
      mode=m;
      if(_gui) _gui->DrawModules();
      break;
    case MODE_LEARN:
      Msg("Switching to Learning mode");
      if(_gui) _gui->setStatus("Learning Mode");
      mode=m;
      break;
    case MODE_ATTACK:
      Msg("Switching to Attack mode");
      if(_gui) _gui->setStatus("Attack Mode");
      if(mode == MODE_LEARN && possible_modules.size() > 0) { // Previous mode was learning, update
        Msg("Normalizing learned data");
        GameData::processLearned();
      }
      mode=m;
      if(_gui) _gui->DrawModules();
      break;
    default:
      Msg("Unknown game mode");
      break;
  }
}

void GameData::processPkt(canfd_frame *cf) {
	if((cf->can_id&CAN_SFF_MASK) < (gd.min_packet_addr<<8) && !(cf->can_id&CAN_EFF_FLAG)) return;
  switch(mode) {
    case MODE_SIM:
      GameData::HandleSim(cf, false);
      break;
    case MODE_LEARN:
      GameData::LearnPacket(cf);
      break;
    case MODE_ATTACK:
      //GameData::AttackPacket(cf);
      GameData::HandleSim(cf, true);
      break;
    default:
      cout << "ERROR: Processing packets while in an unknown mode" << mode << endl;
      break;
  }
}

void GameData::HandleSim(canfd_frame *cf, int fuzz) {
	Module *module = GameData::get_module(cf->can_id);
	vector<canfd_frame> qry;
	int offset=0;

	if(!module && cf->data[offset] == 0x30 && cf->len==8) module = GameData::get_module(cf->can_id - 1); // Flow control is special (?)

	// ignore if module doesn't exists, must be ignored or is reponder
	if(!module || (module && cf->data[offset]!=0x30 && ( module->getIgnore() || module->isResponder() )) ) return;

	if(module->getProtocol()==TP20){ Handle_TP20(cf, module); return; }

	offset=module->getOffset(); // bmw, peugeot, ...
	if(cf->data[offset] == 0x10){ // multipacket query received, first packet // limit to 0x0FF packets
		CanFrame pkt;
		int can_id;
		char str[24];
		// save first packet, set estimated packet count and wait
		multiquery.clear();
		multiquery.push_back(*cf);
		can_id = module->getPositiveResponder(cf);
		// sometimes 300F05AAAAAAAAAA; // send 15 frames before wait for next flow ctrl frame, 5ms between frames
		if(!offset) snprintf(str, 23, "%03X#300002AAAAAAAAAA", can_id); // 3zXXYY, z: continue, XX: 0=remaining will be sent without flow ctrl or delay, YY: 2ms between frames
		else snprintf(str, 23, "%03X#F1300000", can_id);		// BMW doesn't use padding
		canif->sendPackets( {new CanFrame(str)} );
		return;
	}
	if((cf->data[0+offset]&0xF0) == 0x20 && multiquery.size()>0){  // fastforward all consecutive packets // also check can_id??
		multiquery.push_back(*cf);
		if(multiquery.size() >= PACKET_COUNT((&multiquery[0]), offset)){
			//*cf=multiquery[0];	// now only process first packet
			qry=multiquery;
			multiquery.clear();
			*cf=qry[0];
			offset++; // applies to first packet
		} else return;
	} else qry.push_back(*cf);

	//printf("%X:%02X%02X%02X%02X%02X%02X%02X%02X\n", cf->can_id, cf->data[0], cf->data[1], cf->data[2], cf->data[3], cf->data[4], cf->data[5], cf->data[6], cf->data[7]);
	module->setState(STATE_ACTIVE); // for GUI
	if(_gui) _gui->DrawModules(true);

	vector<CanFrame *>response = module->getResponse(qry, fuzz); // also process flow_ctrl
	if(!response.empty()) {
		//CanFrame *resFrame = response.at(0);
		if ( response.size() > 1 && is_first_multipacket(response.front(), module->getOffset())){ // Multi-packet
			// send first and wait for 0x30...
			canif->sendPackets( {response.front()} );
			module->queue_set(response, 1);
		} else { // Single packet
			if(response.size() > 1 && module->queue_get()->size()==0 && !module->get_sendall()){
				// More than one possible answer, select a random one
				if(random_packet) canif->sendPackets({response.at(rand() % response.size())});
				else canif->sendPackets({response.front()});
			} else {
				canif->sendPackets(response);
				if(module->queue_get()->size()==0) module->clear_sendall();
			}
		}
	}
}

void GameData::Handle_TP20(canfd_frame *cf, Module *module) {
	Module *responder;
	static uint8_t ack_seq;
	// static uint8_t packet_seq; // ack and packet seq are different?

	if(cf->can_id==0x200){ // VAG TP2.0 channel setup
		ack_seq=0;
		if( (responder=GameData::get_module(0x200+cf->data[0])) && responder->getProtocol()==TP20){
			vector<CanFrame *> hist = responder->getHistory();
			for(vector<CanFrame *>::iterator it = hist.begin(); it != hist.end(); ++it) {
				if((*it)->data[0]==0x00 && (*it)->data[1]==0xD0){ canif->sendPackets({*it}); return; } // positive answer found
			}
			//cout << responder->getHistory().front()->can_id << endl;
			canif->sendPackets_TP20({ hist.front() }); // no positive match. just send something
		}
	}

	if((cf->data[0]&0xF0)==0x90) return;  // ACK, not ready for next packet

	if((cf->data[0]&0xF0)==0xB0){	// ACK. send rest of packets if necessary
		if(module->queue_get()->size() > 0) {
			canif->sendPackets(*module->queue_get());
			module->queue_get()->clear();
		}
		return;
	}

	if( (responder=GameData::get_module(module->getPositiveResponder(cf), module->getArbId())) ){
		char str[24];
		if(cf->data[0]==0xA8){ // disconnect
			snprintf(str, 7, "%03X#A8", responder->getArbId());
			canif->sendPackets({new CanFrame(str)});
			return;
		}
		if(cf->data[0]==0xA0 || cf->data[0]==0xA3){ // 'parameters request' and 'channel test'
			vector<CanFrame *> hist = responder->getHistory();
			for(vector<CanFrame *>::iterator it = hist.begin(); it != hist.end(); ++it) {
				if((*it)->data[0]==0xA1){ canif->sendPackets({*it}); return; } // answer with parameters response
			}
		}

		// more packets to follow (0x00 || 0x20). if first packet then save
		if(!is_tp20_last_packet(cf) && multiquery.size()==0) multiquery.push_back(*cf);

		if((cf->data[0]&0xF0)==0x20) return;  // Not waiting for ACK, more packets to follow

		if(is_tp20_waiting_ack(cf)){ // Waiting for ACK
			if((cf->data[0]&0xF0)==0x00 && multiquery.size()==0) multiquery.push_back(*cf); // this is first packet. save
			snprintf(str, 7, "%03X#%02X", responder->getArbId(), 0xB0+(++ack_seq&0x0F));
			canif->sendPackets({new CanFrame(str)});
		}

		if(is_tp20_last_packet(cf)){ // last packet
			if( multiquery.size()>0) *cf=multiquery[0]; // use first packet
			multiquery.clear();
			// find answers
			vector<CanFrame *>response = module->getResponse({*cf}, false);
			if(!response.empty()) {
				if(!is_tp20_last_packet(response.front())){ // multipacket
					for(vector<CanFrame *>::iterator it = response.begin(); it != response.end(); ++it) (*it)->data[0]=((*it)->data[0]&0xF0) + (++ack_seq&0x0F); // replace all seq
					if(is_tp20_waiting_ack(response.front())){
						canif->sendPackets( {response.front()} );
						module->queue_set(response, 1);
					} else { // send all at once. let's hope ack not needed in the middle
						// unfortunately sometimes needed :(
						canif->sendPackets_TP20(response);
					}
				} else { // single packet answer	
					CanFrame *frame;
					if(random_packet) frame=response.at(rand() % response.size());
					else frame=response.front();
					frame->data[0]=(frame->data[0]&0xF0) + (++ack_seq&0x0F);
					//if(random_packet) canif->sendPackets({response.at(rand() % response.size())});
					//else canif->sendPackets({response.front()});
					canif->sendPackets({frame});
				}
			}
		}
	}
/*
 (1533797951.457062) can0 4C2#A00F00FF00FF
 (1533797951.549243) can0 304#A10F8AFF4AFF
 (1533797951.729310) can0 4C2#10000322F1A2		// len=6
 (1533797951.817996) can0 304#B1
 (1533797951.829613) can0 304#20000962F1A25330 // b3=b30x40 b4=b4, b5=b5 len>6
 (1533797951.839640) can0 304#1130303032
 (1533797951.841434) can0 4C2#B2
 (1533797951.945183) can0 4C2#A3
 (1533797952.029659) can0 304#A10F8AFF4AFF
 (1533797952.193432) can0 4C2#11000322F1DF
 (1533797952.291656) can0 304#B2

 (1534517201.102424) can3 4C5#1200021089 // Send KWP2000 startDiagnosticSession request (or b3==0x21: readDataByLocalIdentifier) len=5
 (1534517201.105569) can3 307#B3
 (1534517201.245955) can3 307#1300025089 // b3=b30x40, b4=b4, 
 (1534517201.322424) can3 4C5#B4

 */
}

void GameData::LearnPacket(canfd_frame *cf) {
	Module *module = GameData::get_possible_module(cf->can_id);
	Module *possible_module = GameData::isPossibleISOTP(cf);

	//if((module || possible_module) && cf->can_id==0x755) printf("%X:%02X%02X%02X%02X%02X%02X%02X%02X\n", cf->can_id, cf->data[0], cf->data[1], cf->data[2], cf->data[3], cf->data[4], cf->data[5], cf->data[6], cf->data[7]);

	// If module exists then we have seen this ID before
	if(module) {
		module->addPacket(cf);
		if(possible_module) {
			module->incMatchedISOTP();
			delete possible_module;
		} else {
			// Still maybe an ISOTP answer, check for common syntax
			if(is_first_multipacket(cf,0)) {
				module->incMatchedISOTP();
			} else if(cf->data[0] == 0x30 && (cf->len == 3 || cf->len == 8)) { // flow ctrl can come from different address
				module->incMatchedISOTP();
			} else if(cf->len==8 && cf->data[0] >= 0x21 && cf->data[0] <= 0x22) { // possible multipacket (match first 2)
				module->incMatchedISOTP();
			} else {
				module->incMissedISOTP();
			}
		}
		module->setState(STATE_ACTIVE);
		if(_gui) _gui->DrawModules(true);
	} else if(possible_module) { // Haven't seen this ID yet
		possible_module->addPacket(cf);
		possible_modules.push_back(*possible_module);
		if(_gui) _gui->DrawModules();
	}
}

// matches too much
Module *GameData::isPossibleISOTP(canfd_frame *cf) {
	int i, offset;
	bool padding = false;
	uint8_t last_byte;
	Module *possible = NULL;

	if( (cf->can_id==0x200 && cf->len==7 && cf->data[1]==0xC0) 						// Possible VAG TP20 channel setup
		|| (cf->can_id>0x200 && cf->can_id<0x300 && cf->len==7 && cf->data[1]==0xD0)// possible VAG TP20 channel setup response. only match positive
		|| (_lastprotocol==TP20 && cf->len>1 && (cf->data[0]&0xF0)==0xA0) 			// possible TP20 channel parameters
		|| (_lastprotocol==TP20 && cf->len==1)										// possible TP20 ACK
		|| cf->can_id==_tp20_last_requester || cf->can_id==_tp20_last_responder		// dont't reset last protocol
	  ) {
		possible = new Module(cf->can_id);
		possible->setProtocol(TP20);
		if(_tp20_last_requester && cf->can_id==_tp20_last_responder){
			possible->setPositiveResponderID(_tp20_last_requester); // hack. shoud be setPositiveRequesterID
			possible->setNegativeResponderID(_tp20_last_requester);
		}

		if(cf->can_id==0x200) _tp20_last_responder=cf->data[5]<<8|cf->data[4]; // 0x300
		if(cf->can_id>0x200 && cf->can_id<0x300 && cf->len==7 && cf->data[1]==0xD0) _tp20_last_requester=cf->data[5]<<8|cf->data[4];
		_lastprotocol=TP20;
		return possible;
	}

	if(	cf->can_id==0x6F1 										// Possible BMW gateway
			|| ((cf->can_id>>8)==0x6 && cf->data[0]==0xF1) ) {	// possible BMW response (612#F1046C01F303)
		possible = new Module(cf->can_id);
		if(cf->can_id!=0x6F1) possible->setResponder(true);	// BMW uses first byte for addressing
		possible->setProtocol(BMW_P);
		possible->setOffset(1);
	} else if( ((cf->can_id>0x540 && cf->can_id<0x560) || (cf->can_id>=0x5e8 && cf->can_id<0x5f0)) && cf->data[0] == 0x81) { // GMLAN DTC. other possible values are 0x00, 0xFE, 0xFD, ...
		possible = new Module(cf->can_id);
	} else {
		for(offset=0; offset<2; offset++){
			if(cf->data[offset] == cf->len - 1 - offset					// Possible UDS request
					|| is_first_multipacket(cf,offset)					// Possible multipacket UDS request
					|| (cf->data[offset] == 0x30 && (cf->len == 3+offset || cf->len == 8))){ // possible flow-ctrl
				possible = new Module(cf->can_id);
				possible->setOffset(offset);
				break;
			} else if( (cf->data[offset]!=0 && cf->data[offset] < cf->len - 2-offset) // Check if remaining bytes are just padding
					|| (cf->data[offset]==6-offset && cf->len==8 && (cf->data[7]==0 || cf->data[7]==0xAA || cf->data[7]==0x55)) ) { // only last byte is padding
				padding = true;
				last_byte = cf->data[cf->data[offset] + 1];
				for(i=cf->data[offset] + 2; i < cf->len; i++) if(cf->data[i] != last_byte) padding = false;
				if(padding == true) { // Possible UDS w/ padding
					possible = new Module(cf->can_id);
					possible->setPaddingByte(last_byte);
					possible->setOffset(offset);
					break;
				}
			} else if(offset==0 && cf->data[0]<8 && cf->len>3 && cf->data[1]>0x40){ // sometimes padding is random garbage (MB, 4AD#065003001400C892)
				possible = new Module(cf->can_id);
				break;
			}
		} // offset++
	}
	if(possible){
		_lastprotocol=UDS;
		_tp20_last_requester=0; _tp20_last_responder=0;
	}
	return possible;
}

// Goes through the modules and removes ones that are less likely to be of use
void GameData::pruneModules() {
  vector<Module> goodModules;
  bool keep;

  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    keep = false;
	if(it->getArbId()==0x7DF) keep = true; // OBD
	if(it->getPositiveResponder() > -1 || it->getNegativeResponder() > -1)  keep = true;
	if(it->isResponder()) keep = true;
	if(it->getProtocol()==TP20) keep = true;
	if(confidence_threshold>100) keep = true;
	if(confidence_threshold>0 && it->confidence() > confidence_threshold) {
      if(!keep && it->getMatchedISOTP() > 0) keep = true;
    }

    if(keep) {
      goodModules.push_back(*it);
    } else {
      if(verbose) cout << "Removing module " << hex << it->getArbId() << endl;
    }
  }
  modules = goodModules;
}

void GameData::processLearned() {
	if(verbose) cout << "Identified " << possible_modules.size() << " possible modules" << endl;
	modules = possible_modules;
	if(verbose) cout << "Locating responders" << endl;
	for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
		Module *responder = NULL;
		if(it->getArbId()==0x7DF && (responder=GameData::get_module(0x7E8))!=NULL ){ // OBD
			it->setPositiveResponderID(0x7E8);
			it->setNegativeResponderID(0x7E8);
			responder->setResponder(true);
			continue;
		}

		if(it->isResponder() == false) {
			if(it->getArbId()>=0x7E0 && it->getArbId()<0x7E8 && (responder = it->get_module_with_response(it->getArbId() + 0x08))!=NULL ){ // general
				it->setPositiveResponderID(responder->getArbId());
				it->setNegativeResponderID(responder->getArbId());
				responder->setResponder(true);
				if(type_is(GM2) && (responder=it->get_module_gmlan_uudt(it->getArbId() - 0x1F8))){ responder->setResponder(true); responder->setOffset(0); } // GMLan UUDT answer // 7E0->5E8->7E8
				continue;
			}

			if(type_is(GM)){
				responder = it->get_module_with_response(it->getArbId() + 0x300);
				if(!responder) responder = it->get_module_with_response(it->getArbId() + 0x20); // Opel LD
				if(responder) { // GM style positive response
					it->setPositiveResponderID(responder->getArbId());
					set_car_type(GM,"GM", false);
				}
			}
			if(type_is(GM2)){
				// GMW3110-2010.pdf, page 25. UUDT. page 201 DTC
				// https://cdn.intrepidcs.net/support/VehicleSpy/spyExampleDTCPart1.htm
				// req 241-25F
				responder = GameData::get_module(it->getArbId() + 0x400);
				if(responder && it->foundResponse(responder)) { // some (old?) GM have different negative and positive responses
					if(it->getPositiveResponder()==-1) it->setPositiveResponderID(responder->getArbId()); // Opel
					it->setNegativeResponderID(responder->getArbId());
					set_car_type(GM2,"GM2", false);
					if(it->getArbId()>0x240 && it->getArbId()<0x260 && (responder=it->get_module_gmlan_uudt(it->getArbId() + 0x300))) responder->setResponder(true);	// GMLan UUDT answer // 241(USDT req)->541(UUDT)->641(USDT resp)
				}
			}

			if(type_is(CHRYSLER) && it->getArbId()>0x280){ // other GM?
				responder = GameData::get_module(it->getArbId() - 0x11C); // BCM
				if(!responder) responder = GameData::get_module(it->getArbId() - 0x280);
				if(responder && it->foundResponse(responder)) {
					if(it->getPositiveResponder()==-1) it->setPositiveResponderID(responder->getArbId()); // 2018 Chrysler Pacifica
					set_car_type(CHRYSLER,"CHRYSLER", false);
				}
			}

			if(type_is(VAG)){
				if(it->getProtocol()==TP20){
					if(GameData::process_TP20(&(*it))){
						set_car_type(VAG, "VAG", false);
					}
				} else {
					responder = GameData::get_module(it->getArbId() + 0x6A);
					if(!responder && it->getArbId()>0x10000000) responder = GameData::get_module(it->getArbId() + 0x20000); // newer Audi
					if(responder && it->foundResponse(responder)) set_car_type(VAG, "VAG", true);
				}
				if(it->getArbId()==0x200){ // VAG TP2.0 channel setup
				}
			}
			if(type_is(RENAULT) || type_is(NISSAN)){	// Renault/Dacia/Nissan response
				responder = GameData::get_module(it->getArbId() + 0x20);
				if(!responder) responder = GameData::get_module(it->getArbId() + 0x10);
				if(responder && it->foundResponse(responder)) set_car_type(RENAULT, "Renault", true);
			}
			if(type_is(MB)){
				responder = it->get_module_with_response(it->getArbId() - 0x80);
				if( !responder && it->getArbId()>0x600 && it->getArbId()<0x800
						&& (it->getArbId()%8==1 || it->getArbId()%8==2 || it->getArbId()%8==3) ) { // there may be other modules but haven't seen them yet
					responder = it->get_module_with_response(0x400 + 0x40*(it->getArbId()%8) + (int)((it->getArbId()-0x600)/8)); // 0x691->0x452, 0x602->0x480, 0x6A3->0x4D4 
				}
				// Light-Duty vehicles
				if( !responder && it->getArbId()==0x703) responder = it->get_module_with_response(0x4F0); // what is this?
				if( !responder && it->getArbId()==0x4E0) responder = it->get_module_with_response(0x5FF);
				if( !responder && it->getArbId()>0x700) responder = it->get_module_with_response(it->getArbId()+0x01);
				if( !responder && it->getArbId()>0x500) responder = it->get_module_with_response(it->getArbId()%0x20 + 0x4E0); // 0x5D6->0x4F6, 0x667->0x4E7, 0x791->0x4F1

				if(responder) set_car_type(MB, "Mercedes-Benz", true);
			}
			if(type_is(VOLVO) && it->getArbId()>0x1DD01000){ // Volvo
				responder = GameData::get_module(0x1E000E80 + ((2*((it->getArbId()>>8)&0x0F))<<20) + (((it->getArbId()&0xff)*2)<<12));
				if(!responder) responder = GameData::get_module(it->getArbId() - 0x100); // needs confirmation
				if(responder && it->foundResponse(responder)) set_car_type(VOLVO, "VOLVO", true);
			}
			if(type_is(PSA) && it->getArbId()>0x10000000){ // Citroen
				responder = GameData::get_module(it->getArbId() - 0x42E1); // OBD
				if(!responder) responder = GameData::get_module((it->getArbId()&0xFFFF0000) + ((it->getArbId()&0xFF)<<8) + ((it->getArbId()&0xFF00)>>8)); // 0x18DA28F1 -> 0x18DAF128
				if(responder && it->foundResponse(responder)) set_car_type(PSA, "Citröen", true);
			}
			if(type_is(PSA)){ // Peugeot // order is important!
				responder = GameData::get_module(it->getArbId() - 0x100);
				if(!responder) responder = GameData::get_module(it->getArbId() - 0xA0); // probably with some prefix (0x48)
				if(!responder) responder = GameData::get_module(it->getArbId() - 0x20);
				if(!responder) responder = GameData::get_module(it->getArbId() - 0xC0);
				if(responder && it->foundResponse(responder)) set_car_type(PSA, "Peugeot", true);
			}
			if(type_is(SUBARU)){ // Subaru // some modules use prefix // and also Ford
				responder = GameData::get_module(it->getArbId() + 0x08);
				if(responder && it->foundResponse(responder)) set_car_type(SUBARU, "Subaru", true);
			}
			// still not found
			if(!responder){
				responder = it->get_module_with_response(it->getArbId() + 0x01); // MB LD. what else ???
				if(responder) { // check for flow control
					vector<CanFrame *>pkts = responder->getPacketsByBytePos(0, 0x30); // what? query can also be several packets
					if(pkts.size() > 0) responder->setResponder(true);
				}
				if( (responder = it->get_module_with_response(it->getArbId() - 0x24)) ) responder->setResponder(true); // only set as responder (0x7df->0x7bb) // hyundai
			}
		}
	}
	GameData::pruneModules();
	// Cleanup - After pruning we can space out modules more
	possible_modules = modules; // Clear up possible to known modules
	if(_gui){
		for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
			Module *mod = &*it;
			if(_gui->isModuleOverlapping(mod)) _gui->setRandomModulePosition(mod);
		}
		_gui->Redraw();
	}
	stringstream m;
	m << "Identified " << GameData::get_active_modules().size() << " Active modules";
	GameData::Msg(m.str());
}

int GameData::process_TP20(Module *module){
	uint16_t qry_addr, resp_addr;
	Module *m=NULL;
	CanFrame *f;
	vector<CanFrame *> *h_it;
	/*
	 * https://jazdw.net/tp20
	 *
	 * 200#26C00010020301	channel setup to ECU 0x226. ECU use 0x0302
	 * 226#00D00203C60401	channel setup reply. diagn. use 0x04C6
	 * 4C6#A00F00FF00FF		channel param. request
	 * 302#A10F8AFF4AFF		channel param. response
	 * 4C6#10000322F1A2		send KWP2000 request. 3 bytes. req 22, param F1 A2
	 * 302#B1				ACK
	 * 302#20000962F1A23030	no ACK. more packets to follow. 9 bytes, resp. 62, param F1 A2
	 * 302#1130303136		last packet. expecting ACK. data: 3030 30303136
	 * 4C6#B2				ACK
	 * 4C6#A3				req channel test
	 * 302#A10F8AFF4AFF		channel param. response
	 * 4C6#A8				disconnect req
	 * 302#A8				disconnect resp.
	 */

	if(module->getArbId()==0x200){ // VAG TP20 channel setup
		uint8_t found=0;
		vector <CanFrame *>frames = module->getHistory();
		for(vector<CanFrame *>::iterator it = frames.begin(); it != frames.end(); ++it) { // search for TP20 modules
			CanFrame *frame = *it;
			resp_addr=frame->data[5]<<8|frame->data[4];
			if((m = GameData::get_module(frame->data[0]+0x200))){ // search for 0x226
				m->setResponder(true);
				m->setProtocol(TP20);
				f=m->getHistory().front(); // we only have positive matches in history // not yet!!! delete negative one's first
				qry_addr=f->data[5]<<8|f->data[4];

				if((m = GameData::get_module(qry_addr))){		// search for 0x4C6
					m->setNegativeResponderID(resp_addr);
					m->setPositiveResponderID(resp_addr);
					m->setProtocol(TP20);
					h_it = m->getHistory_ptr();
					for(vector<CanFrame*>::iterator i = h_it->begin(); i != h_it->end();){ if((*i)->len==1) i=h_it->erase(i); else i++; } // remove ACK
					// h_it->erase( remove_if(h_it->begin(), h_it->end(), [](const CanFrame *o) { return o->len==1; }), h_it->end()); // same, requires <algorithm>
				}
				// response modules could share same address
				for(vector<Module>::iterator m_it = modules.begin(); m_it != modules.end(); ++m_it) { // process multiple combinations of addr, response_id
					if(m_it->getArbId()!=resp_addr) continue;
					m_it->setResponder(true);
					m_it->setProtocol(TP20);
					h_it = m_it->getHistory_ptr();
					for(vector<CanFrame*>::iterator i = h_it->begin(); i != h_it->end();){ if((*i)->len==1) i=h_it->erase(i); else i++; } // remove ACK
				}
				/*
				if((m = GameData::get_module(resp_addr))){		// search for 0x302
					m->setResponder(true);
					m->setProtocol(TP20);
					h_it = m->getHistory_ptr();
					for(vector<CanFrame*>::iterator i = h_it->begin(); i != h_it->end();){ if((*i)->len==1) i=h_it->erase(i); else i++; } // remove ACK
				}
				*/
				found=1;
				//cout << hex << "XX" << qry_addr << " | " << resp_addr << " | " << m->getNegativeResponder() << "XX" << endl;
			}
		}
		if(found) return 1;
	}
	return 0;
}

string GameData::frame2string(canfd_frame *cf) {
  stringstream pkt;
  if(cf->len < 0 || cf->len > 8) { 
    return "ERROR: CAN packet with imporoper length";
  }
  pkt << hex << cf->can_id << CANID_DELIM;
  int i;
  for(i=0; i < cf->len; i++) {
    pkt << setfill('0') << setw(2) << hex << (int)cf->data[i];
  }
  return pkt.str();
}

void GameData::Msg(string mesg) {
	if(mesg=="") return;
	if(_gui == NULL) cout << mesg << endl; // treat as verbose
	else _gui->Msg(mesg);
}

bool GameData::SaveConfig() {
  ofstream configFile;
  configFile.open("config_data.cfg");
  // Globals
  // Modules
  configFile << endl;
  for(vector<Module>::iterator it = modules.begin(); it != modules.end(); ++it) {
    configFile << "[" << hex << it->getArbId() << "]" << endl;
    if(it->getX() || it->getY()) configFile << "pos = " << dec << it->getX() << "," << it->getY() << endl;
    configFile << "responder = " << it->isResponder() << endl;
    //if(!it->isResponder()) { // for TP20
      if(it->getPositiveResponder() != -1) configFile << "positiveID = " << hex << it->getPositiveResponder() << endl;
      if(it->getNegativeResponder() != -1) configFile << "negativeID = " << hex << it->getNegativeResponder() << endl;
    //}
    if(it->getIgnore()) configFile << "ignore = " << it->getIgnore() << endl;
    if(it->getFuzzVin()) configFile << "fuzz_vin = " << it->getFuzzVin() << endl;
    if(it->getFuzzLevel() > 0) configFile << "fuzz_level = " << it->getFuzzLevel() << endl;
    if(it->getProtocol()!=UDS) configFile << "protocol = " << Protocol_str[it->getProtocol()] << endl;
    if(it->getOffset()) configFile << "offset = " << it->getOffset() << endl;
    configFile << "{Packets}" << endl;
    vector <CanFrame *>frames = it->getHistory();
    for(vector<CanFrame *>::iterator it2 = frames.begin(); it2 != frames.end(); ++it2) {
      CanFrame *frame = *it2;
      configFile << frame->estr() << endl;
    }
    configFile << endl;
  }
  configFile.close();
  Msg("Saved config_data.cfg");
  return true;
}

void GameData::launchPeach() {
  ofstream peachXML;
  peachXML.open("fuzz_can.xml");
  peachXML << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << endl;
  peachXML << "<Peach xmlns=\"http://peachfuzzer.com/2012/Peach\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << endl;
  peachXML << "        xsi:schemaLocation=\"http://peachfuzzer.com/2012/Peach /peach/peach.xsd\">" << endl;
  // TODO Use learned information to populate DataModels
  peachXML << "        <DataModel name=\"CANPacket\">" << endl;
  peachXML << "                <Blob name=\"sample1\" valueType=\"hex\" value=\"00 00 03 33 03 01 02 03 00 00 00 00 00 00 00 00\"/>" << endl;
  peachXML << "        </DataModel>" << endl;
  peachXML << endl;
  peachXML << "        <StateModel name=\"TheState\" initialState=\"Initial\">" << endl;
  peachXML << "                <State name=\"Initial\">" << endl;
  peachXML << "                        <Action type=\"output\">" << endl;
  peachXML << "                                <DataModel ref=\"CANPacket\"/>" << endl;
  peachXML << "                        </Action>" << endl;
  peachXML << "                </State>" << endl;
  peachXML << "        </StateModel>" << endl;
  peachXML << "        <Agent name=\"TheAgent\">" << endl;
  peachXML << "        </Agent>" << endl;
  peachXML << "        <Test name=\"Default\">" << endl;
  peachXML << "                <Agent ref=\"TheAgent\"/>" << endl;
  peachXML << "                <StateModel ref=\"TheState\"/>" << endl;
  peachXML << "                <Publisher class=\"CAN\">" << endl;
  peachXML << "                        <Param name=\"Interface\" value=\"" << canif->getIfname() << "\"/>" << endl;
  peachXML << "                </Publisher>" << endl;
  peachXML << "                <Logger class=\"File\">" << endl;
  peachXML << "                        <Param name=\"Path\" value=\"logs\"/>" << endl;
  peachXML << "                </Logger>" << endl;
  peachXML << "        </Test>" << endl;
  peachXML << "</Peach>" << endl;
  peachXML.close();
  Msg("Created fuzz_can.xml");
}

void GameData::nextMode() {
  switch(mode) {
    case MODE_SIM:
      GameData::setMode(MODE_LEARN);
      break;
    case MODE_LEARN:
      GameData::setMode(MODE_ATTACK);
      break;
    case MODE_ATTACK:
      GameData::setMode(MODE_SIM);
      break;
  }
}

int GameData::string2hex(string s) {
  stringstream ss;
  int h;
  ss << hex << s;
  ss >> h;
  return h;
}

int GameData::string2int(string s) {
  stringstream ss;
  int i;
  ss << dec << s;
  ss >> i;
  return i;
}

void GameData::processCan() {
	struct canfd_frame cf;
	int i;
	if(!canif) return;

	vector <CanFrame *>frames = canif->getPackets();
	for(vector <CanFrame *>::iterator it=frames.begin(); it != frames.end(); ++it) {
		CanFrame *pkt = *it;
		if(verbose) Msg(pkt->str());
		cf.can_id = pkt->can_id;
		cf.len = pkt->len;
		for(i=0; i < pkt->len; i++) cf.data[i] = pkt->data[i];
		GameData::processPkt(&cf);
	}
//	SDL_Delay(1); // we use blocking select?
}

