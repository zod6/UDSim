#include "module.h"

const char *Protocol_str[] = { "UDS", "BMW", "TP20", NULL }; // extern

Module::Module(int id) {
  arbId = id;
}

Module::~Module() {
}

void Module::setProtocol(const char* str){
	int i=0;
	for(;Protocol_str[i]!=NULL; i++) if(!strcmp(str,Protocol_str[i])){ setProtocol((Protocol)i); return; }
	cout << "protocol not found " << str << endl;
}

/* Calculates the confidence that it is a UDS module based on seen ISOTP packets to non */
float Module::confidence() {
  if (getMatchedISOTP() == 0) return 0;
  float total = getMatchedISOTP() + getMissedISOTP();
  return (float)getMatchedISOTP() / total;
}

int Module::getPositiveResponder(struct canfd_frame *cf){
	if(cf->can_id==0x6F1) return cf->data[0]+0x600;	// BMW
	return positive_responder_id;
}
int Module::getNegativeResponder(struct canfd_frame *cf){
	if(cf->can_id==0x6F1) return cf->data[0]+0x600;	// BMW
	return negative_responder_id;
}

// add when learning
void Module::addPacket(struct canfd_frame *cf) {
	CanFrame *old_frame;
	bool dup_found = false;
	int offset=0;

    if(_protocol==TP20 && cf->can_id>0x200 && cf->data[1]!=0xD0){ addPacket_TP20(cf); return; } // handle 0x200 and it's response like normal packet

	//if(_protocol==BMW || ((cf->can_id>>8)==0x6 && cf->data[0]==0xF1) || cf->can_id==0x6F1) offset++; // BMW
	offset=_offset;
	if(_repair_frame!=NULL && (cf->data[0+offset]&0xF0) == 0x20){
		Module::repair_queue(cf); // sometimes my extended frames have 'holes'
		return;
	}

	for(vector<CanFrame *>::iterator it = can_history.begin(); it != can_history.end(); ++it) {
		old_frame = *it;
		if(old_frame->framesmatch(cf)){ dup_found = true; break; }
	}
	if(_protocol==TP20){// && cf->can_id<0x300) {
		if(!dup_found) can_history.push_back(new CanFrame(cf));
		else return; //? just to set _repair_frame=NULL; and _expect_consecutive_frame=false; when match found?
		return;
	}

	if(!dup_found) {
		if((cf->data[0+offset]&0xF0) == 0x20) {
			if(_expect_consecutive_frame==true && can_history.back()){
				// check order and correct
				if( can_history.back()->queue.size()==0 || cf->data[offset] > can_history.back()->queue.back()->data[offset] // new order is larger than old one (0x22 > 0x21)
						|| PACKET_COUNT(can_history.back(), offset) > 15 ){  // more than 15 lines and numbers starting to repeat
					can_history.back()->queue.push_back(new CanFrame(cf));
				} else {
					int pos=(cf->data[offset]&0x0F)-1;
					if(pos<0) pos=0; // shouldn't happen anymore
					if(pos > (int)can_history.back()->queue.size()) pos=can_history.back()->queue.size();
					can_history.back()->queue.insert(can_history.back()->queue.begin()+pos, new CanFrame(cf));
					if(gd.getVerbose()) cout << "Frame order corrected: " << can_history.back()->str() << " [" << (cf->data[offset]&0x0F) << "]" << endl;
				}
			}
		} else {
			can_history.push_back(new CanFrame(cf));
			if( cf->can_id!=0x200 && is_first_multipacket(cf,offset)) _expect_consecutive_frame=true;
			else _expect_consecutive_frame=false;
		}
		_repair_frame=NULL;
	} else {
		// test for missing multi-frames
		if( is_first_multipacket(cf,offset) && old_frame->queue.size() < PACKET_COUNT(cf, offset) ){ _repair_frame=old_frame; _repair_frame_num=1; }
		else _repair_frame=NULL;
		_expect_consecutive_frame=false;
	}
}

void Module::addPacket_TP20(struct canfd_frame *cf) {
	CanFrame *old_frame;
	bool dup_found = false;
	int i;
	if(cf->len==1) return; // ignore ACK
	if(cf->len>=3 && !memcmp(cf->data, "\x30\x00\x05", 3)) return; // ignore xxx#300005.... // what is it? broadcast? appears randomly

	// skip frames if we already have them. stop skipping when last packet encountered
	if(_repair_frame!=NULL && cf->can_id>=0x200 && (cf->data[0]&0xF0)<0x40){
		if(is_tp20_last_packet(cf->data[0])) _repair_frame=NULL;
		//Module::repair_queue(cf); // TP20 not implemented
		return;
	}
	// TP20 ignore seq (b0&0x0F)
	for(vector<CanFrame *>::iterator it = can_history.begin(); it != can_history.end(); ++it) {
		old_frame = *it;
		if( (*it)->len!=cf->len || (((*it)->data[0]^cf->data[0])&0xF0)) continue;
		for(i=1; i<cf->len; i++) if((*it)->data[i]!=cf->data[i]) break;
		if(i==cf->len){ dup_found=true; break; }
	}
	if(!dup_found) {
		if((cf->data[0]&0xF0)<0x40 && _expect_consecutive_frame==true && can_history.back()) can_history.back()->queue.push_back(new CanFrame(cf)); // data packet
		else can_history.push_back(new CanFrame(cf));
		if( cf->can_id>=0x200 && is_tp20_multipacket(cf->data[0]) ) _expect_consecutive_frame=true; // data packet, more to follow
		else _expect_consecutive_frame=false;
		_repair_frame=NULL;
	} else {
		if( cf->can_id>=0x300 && is_tp20_multipacket(cf->data[0]) ){ _repair_frame=old_frame; _repair_frame_num=1; } 
		else _repair_frame=NULL;
		if(cf->data[0]!=0xA1) _expect_consecutive_frame=false; 	// parameters response can be inside data
	}
}

// todo: 16+ lines long packages handeled correctly?
void Module::repair_queue(canfd_frame *cf){
	int offset=0;
	if(_repair_frame==NULL || _protocol==TP20) return;

	//if(((cf->can_id>>8)==0x6 && cf->data[0]==0xF1) || cf->can_id==0x6F1) offset++; // BMW
	offset=_offset;
	if(_repair_frame_num%16 != (cf->data[offset]&0x0F)) { // new data also has holes. ff one frame
		_repair_frame_num++;
		if(_repair_frame_num%16 != (cf->data[offset]&0x0F) || _repair_frame->queue.size() < _repair_frame_num || _repair_frame->queue[_repair_frame_num-1]->data[offset] != cf->data[offset]){ // frames still doesn't match
			_repair_frame=NULL;
			return;
		}
	}
	if(_repair_frame->queue.size() < _repair_frame_num){
		if(_repair_frame->queue.size()==0 || _repair_frame->queue.back()->data[offset] < cf->data[offset]){
			if(gd.getVerbose()) cout << "adding to the end of " << _repair_frame->str() << " [" << _repair_frame_num << "]" << endl;
			_repair_frame->queue.push_back(new CanFrame(cf));
		} else {
			_repair_frame=NULL;
			return;
		}
	} else if(_repair_frame->queue[_repair_frame_num-1]->data[offset] != cf->data[offset]){
		if(gd.getVerbose()) cout << "inserting to " << _repair_frame->str() << " [" << _repair_frame_num << "]" << endl;
		_repair_frame->queue.insert(_repair_frame->queue.begin()+_repair_frame_num-1, new CanFrame(cf)); // not suitable for larger holes
	}
	if(_repair_frame->queue.size() == PACKET_COUNT(_repair_frame, offset)){
		if(gd.getVerbose()) cout << "Frame repaired: " << _repair_frame->str() << endl;
		_repair_frame=NULL;
	}
	_repair_frame_num++;
	return;
}

// Searches a given module for something that resemble a proper ISO-TP response
bool Module::foundResponse(Module *responder) {
	vector <CanFrame *>possible_resp;
	// first pass: check for standard response. second pass: check for extended response
	for(int pass=0; pass<2; pass++){
		for(vector<CanFrame *>::iterator it = can_history.begin(); it != can_history.end(); ++it) {
			CanFrame *cf = *it;
			possible_resp = responder->getPacketsByBytePos(1+pass+_offset, cf->data[1+_offset] + 0x40);
			if(possible_resp.size() > 0) { // Standard response
				for(vector<CanFrame *>::iterator it = possible_resp.begin(); it != possible_resp.end(); ++it) {
					CanFrame *pcf = *it;
					if(pass && !is_first_multipacket(pcf,0+_offset)) continue; // second pass and not extender response
					if(cf->data[0+_offset] == 1) return true;
					else if(cf->data[2+_offset] == pcf->data[2+pass+_offset]) return true; // Request has a sub function
				}
			}
		}
	}
	return false;
}

void Module::addPacket(string packet) {
  CanFrame *newcf = new CanFrame(packet);
  for(vector<CanFrame *>::iterator it = can_history.begin(); it != can_history.end(); ++it) {
    CanFrame *old = *it;
	if( newcf->framesmatch(old) ) { delete newcf; return; }
	// if(old->str() == newcf->str()) return; // veeery slow
  }
  can_history.push_back(newcf);
}

// insert manually to front (can be duplicate)
void Module::addPacket_front(string packet) {
  CanFrame *newcf = new CanFrame(packet);
  can_history.insert(can_history.begin(),newcf);
}

void Module::setState(int s) {
  // Only blink if not being moved
  if (state == STATE_SELECTED && s == STATE_ACTIVE) return;
  state = s;
#ifdef SDL
  if (state == STATE_ACTIVE) _activeTicks = SDL_GetTicks();
#endif
}

int Module::getState() {
  return state;  
}

void Module::setFuzzLevel(unsigned int level) {
  if(level >= 0 && level <= 4) _fuzz_level = level;
}

/* Retrives all matching packets give a matching byte at position */
vector <CanFrame *>Module::getPacketsByBytePos(unsigned int pos, unsigned char byte) {
  vector <CanFrame *>matches;
  for(vector<CanFrame *>::iterator it = can_history.begin(); it != can_history.end(); ++it) {
    CanFrame *frame = *it;
    if(frame->len > pos) {
      if(frame->data[pos] == byte) {
        matches.push_back(frame);
      }
    }
  }
  return matches;
}

// data=(uint8_t[]){0x10,0x0C,0xFC,0x19,0xD2,0x10,0x01,0x01}, mask=(uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}
vector <CanFrame *>Module::getPacketsByMask(uint8_t *data, uint8_t *mask, int len) {
	int i;
	vector <CanFrame *>matches;
	for(vector<CanFrame *>::iterator it = can_history.begin(); it != can_history.end(); ++it) {
		CanFrame *frame = *it;
		for(i=0;i<len;i++) if( (frame->data[i]&mask[i]) != (data[i]&mask[i]) ) break;
		if(i==len) matches.push_back(frame);
	}
	return matches;
}

/* This could be done in vector but we unwrap and call the more generic
 * calc_vin_checksum, then modify data to include a proper checksum byte
*/
vector <CanFrame *>Module::inject_vin_checksum(vector <CanFrame *>resp) {
  char *buf;
  unsigned char checksumByte;
  CanFrame *cf;
  int i = 0;
  int idx = 0;
  buf = (char *)malloc(1024);
  bool firstPacket = true;
  for(vector<CanFrame *>::iterator it = resp.begin(); it != resp.end(); ++it) {
    cf = *it;
    if(firstPacket) {
      for(i = 5; i < cf->len; i++, idx++) {
        buf[idx] = cf->data[i];
      }
      firstPacket = false;
    } else {
      for(i = 1; i < cf->len; i++, idx++) {
        buf[idx] = cf->data[i];
      }
    }
  }
  checksumByte = Module::calc_vin_checksum(buf, idx);
  idx = 0;
  firstPacket = true;
  for(vector<CanFrame *>::iterator it = resp.begin(); it != resp.end(); ++it) {
    cf = *it;
    if(firstPacket) {
      for(i = 5; i < cf->len; i++, idx++) {
      }
      firstPacket = false;
    } else {
      for(i = 1; i < cf->len; i++, idx++) {
        if(idx == 8) // The 8th byte of the vin is the checksum
          cf->data[i] = checksumByte;
      }
    }
  }
  free(buf);
  return resp;
}

unsigned char Module::calc_vin_checksum(char *vin, int size) {
  char w[17] = { 8, 7, 6, 5, 4, 3, 2, 10, 0, 9, 8, 7, 6, 5, 4, 3, 2 };
  int i;
  int checksum = 0;
  int num=0;
  for(i=0; i < size; i++) {
    if(vin[i] == 'I' || vin[i] == 'O' || vin[i] == 'Q') {
      num = 0;
    } else {
      if(vin[i] >= '0' && vin[i] <='9') num = vin[i] - '0';
      if(vin[i] >= 'A' && vin[i] <='I') num = (vin[i] - 'A') + 1;
      if(vin[i] >= 'J' && vin[i] <='R') num = (vin[i] - 'J') + 1;
      if(vin[i] >= 'S' && vin[i] <='Z') num = (vin[i] - 'S') + 2;
    }
    checksum += num * w[i];
  }
  checksum = checksum % 11;
  if (checksum == 10) return 'X';
  return ('0' + checksum);
}

CanFrame *Module::createPacket(int id,char *data, int len) {
  CanFrame *cf = new CanFrame();
  int i, counter;
  int left = len;
  cf->can_id = id;
  if(len < 7) {
    cf->len = len + 1;
    cf->data[0] = len;
    for(i=1; i < len; i++) cf->data[i] = data[i-1];
    return cf;
  } else {
    cf->len = 8;
    cf->data[0] = 0x10;
    cf->data[1] = len;
    for(i=2; i < 8; i++) cf->data[i] = data[i-2];
    left -= 6;
    counter = 0x21;
    while(left > 0) {
      CanFrame *cfq = new CanFrame();
      cfq->can_id = id;
      cfq->data[0] = counter;
      if(left > 7) {
        cfq->len = 8;
        for(i=1; i < 8; i++) cfq->data[i] = data[len - left + i-1];
        _queue.push_back(cfq);
        counter++;
        if(counter >= 0x30) counter = 0x20;
        left-=7;
      } else {
        cfq->len = left + 1;
        for(i=1; i < left + 1; i++) cfq->data[i] = data[len - left + i-1];
        _queue.push_back(cfq);
        left = 0;
      }
    }
  }
  return cf;
}

// returns multiple single-frame responses or one multi-frame response
vector <CanFrame *>Module::fetchHistory(struct canfd_frame *cf, int max_level) { // max_level: 1=cmd, 2=func, 3=subf.
	int req_offset=0;	// request offset
	int resp_offset=0;	// response offset
	vector <CanFrame *>resp;
	Module *responder = gd.get_module(getPositiveResponder(cf), cf->can_id);

	if(_protocol==TP20){ req_offset=2; resp_offset=2; }
	else {
		//if(cf->can_id==0x6F1){ req_offset=1; resp_offset=1; } // BMW
		req_offset=resp_offset=_offset;
		if(cf->data[req_offset] == 0x10) req_offset++;
	}
	if(cf->data[req_offset]<max_level) max_level=cf->data[req_offset];

	//if(responder != NULL && getPacketsByBytePos(1+resp_offset, cf->data[1+req_offset]).size() > 0 ) {} // responder exists and we have seen a request cmd like this before
	if(responder != NULL){
		for(vector<CanFrame *>::iterator it = responder->can_history.begin(); it != responder->can_history.end(); ++it) {
			uint8_t pcf_offset=resp_offset;
			CanFrame *pcf= *it;
			// TP20 length is always 2 bytes
			if(_protocol!=TP20 && (pcf->data[pcf_offset]&0xF0)==0x10){
				if((((pcf->data[pcf_offset]&0x0F)<<8) + pcf->data[pcf_offset+1]) < max_level) continue; // response cannot be shorter than request
				pcf_offset++;
			} else if(pcf->data[pcf_offset] < max_level) continue;	// response cannot be shorter than request

			if( pcf->len > max_level+pcf_offset 													// response cannot be shorter than request
					&& (				pcf->data[1+pcf_offset]==cf->data[1+req_offset] + 0x40 )	// resp_cmd  == req_cmd
					&& ( max_level<2 ||	pcf->data[2+pcf_offset]==cf->data[2+req_offset] )			// resp_func == req_func
					&& ( max_level<3 ||	pcf->data[3+pcf_offset]==cf->data[3+req_offset] ) )			// resp_subf == req_subf
			{
				resp.push_back(pcf);
				if(pcf->queue.size()>0) for(vector<CanFrame *>::iterator it2 = pcf->queue.begin(); it2 != pcf->queue.end(); ++it2) resp.push_back(*it2);
				if(resp.size()>0) break;
			}
		}
	}
	return resp;
}

// Mode $01
vector <CanFrame *>Module::showCurrentData(vector <CanFrame *>resp, struct canfd_frame *cf) {
  char *buf;
  bool found = false;
  int target = cf->can_id + 9;
  stringstream ss;
  if(Module::getPositiveResponder() > -1) target = Module::getPositiveResponder();
  if(cf->len < 3) return resp;
  if (resp.size() > 0) found = true;
  if(!found && Module::getFakeResponses()) {
    switch(cf->data[2]) {
      case 0x00: // PIDS
      case 0x20:
      case 0x40:
      case 0x60:
      case 0x80:
      case 0xA0:
      case 0xC0:
        buf = (char *)malloc(6);
        buf[0] = cf->data[1] + 0x40;
        buf[1] = cf->data[2];
        buf[2] = 0xBF;
        buf[3] = 0xBF;
        buf[4] = 0xB9;
        buf[5] = 0x93;
        resp.push_back(Module::createPacket(target, buf, 6));
        free(buf);
        //ss << "PID supported " << (unsigned char)cf->data[2] << "-" << (unsigned char)cf->data[2] + 0x1F;
        //gd.Msg(ss.str());
        break;
      case 0x01: // MIL
        buf = (char *)malloc(6);
        buf[0] = cf->data[1] + 0x40;
        buf[1] = cf->data[2];
        buf[2] = 0x00;
        buf[3] = 0x07;
        buf[4] = 0xE5;
        buf[5] = 0xE5;
        resp.push_back(Module::createPacket(target, buf, 6));
        free(buf);
        gd.Msg("MIL and DTC Status");
        break;
      default:
        //ss << "Requested unsupported data 0x" << hex << cf->data[2];
        //gd.Msg(ss.str());
        break;
    }
  }
  return resp;
}

// Mode $09
vector <CanFrame *>Module::vehicleInfoRequest(vector <CanFrame *>resp, struct canfd_frame *cf) {
  CanFrame *test;
  bool found = false;
  int target = cf->can_id + 9;
  stringstream ss;
  char *buf;
  char default_vin[] = DEFAULT_VIN;
  if(Module::getPositiveResponder() > -1) target = Module::getPositiveResponder();
  if(cf->len < 3) return resp;
  if (resp.size() > 0) found = true;
  if(!found && Module::getFakeResponses()) {
    switch(cf->data[2]) {
      case 0x00: // Pids
        buf = (char *)malloc(6);
        buf[0] = cf->data[1] + 0x40;
        buf[1] = cf->data[2];
        buf[2] = 0x55;
        buf[3] = 0x0;
        buf[4] = 0x0;
        buf[5] = 0x0;
        resp.push_back(Module::createPacket(target, buf, 6));
        free(buf);
        gd.Msg("Vehicle PID Request");
        break;
      case 0x02: // VIN
        buf = (char *)malloc(strlen(DEFAULT_VIN) + 3);
        buf[0] = cf->data[1] + 0x40;
        buf[1] = cf->data[2];
        buf[2] = 1;
        memcpy(&buf[3], default_vin, strlen(DEFAULT_VIN));
        test = Module::createPacket(target, buf, strlen(DEFAULT_VIN) + 3);
        resp.push_back(test);
        free(buf);
        break;
      default:
        cout << "Requested unsupported info 0x" << (unsigned char)cf->data[2];
        //gd.Msg(ss.str());
        break;
    }
  }
  return resp;
}

// multibyte and TP20 needs fixing 
vector <CanFrame *>Module::fuzzResp(vector <CanFrame *>resp, struct canfd_frame *cf) {
  int i = 0;
  int iso_offset = 0;
  if(_protocol!=TP20 && getFuzzLevel() == 1) {
    CanFrame *cf2 = NULL;
    bool firstPkt = true;
    if(resp.size() > 1) iso_offset = 1;
    for(vector<CanFrame *>::iterator it = resp.begin(); it != resp.end(); ++it) {
      cf2 = *it;
      if(firstPkt) {
        if (cf2->len > cf->len + iso_offset) {  // Check to see if the response is bigger than the request
          for(i = cf->len + iso_offset; i < cf2->len; i++) {
            cf2->data[i] = rand() % 256;
          }
        }
        firstPkt = false;
      } else {
        for(i = 1; i < cf2->len; i++) {
          cf2->data[i] = rand() % 256;
        }
      }
    }
  }
  return resp;
}

/* To generate a response we check these steps:
 * 1) Check to see if we had seen a similar request
 *    1) Any possible positive responses
 *    2) Any possible negative responses
 * 3) Any generic answers
 */
vector <CanFrame *>Module::getResponse(struct canfd_frame *cf, bool fuzz) {
	vector <CanFrame *>resp;
	stringstream ss, errPkt;

	bool doFuzz = false;
	int offset=0;

	if(_protocol==TP20) offset=2;
	else offset=_offset;
	//if(cf->can_id==0x6F1) offset=1; // BMW

	if(cf->data[offset] == 0x30 && _protocol!=TP20) {  // Flow Control
		Module *module;
		if(_queue.size() > 0) {
			if(cf->data[1+offset]==0){ // block size == 0
				resp = _queue;
				_queue.clear();
			} else { // flow ctrl block size > 0   // 7E0#3001000000000000
				for(uint8_t i=0; i<cf->data[1+offset] && _queue.size(); i++) {
					resp.push_back(*_queue.begin());
					_queue.erase(_queue.begin());
				}
			}
		} else if(cf->can_id==0x7E0 && (module=gd.get_module(0x7DF))){ // MB OBD flow-ctrl hack
			if(module->_queue.size()>0){ resp = module->_queue; module->_queue.clear(); }
		} else if(cf->can_id==0x18DA10F1 && (module=gd.get_module(0x18DB33F1))){ // PSA OBD flow-ctrl hack
			if(module->_queue.size()>0){ resp = module->_queue; module->_queue.clear(); }
		}
	} else if (cf->len > (1+offset)) {
		if(_protocol!=TP20 && (cf->data[offset]&0xF0) == 0x10) offset++; // multi-frame query 
		resp = Module::fetchHistory(cf, 2);
		if(fuzz && getFuzzLevel() > 0) doFuzz = true;
		switch(cf->data[1+offset]) {
			// Modes
			case 0x01:
				ss << hex << cf->can_id << ": Mode Show Current Data";
				resp = Module::showCurrentData(resp, cf); // BMW: todo!
				break;
			case 0x02:
				ss << hex << cf->can_id << ": Mode Show Freeze Frame";
				break;
			case 0x03:
				ss << hex << cf->can_id << ": Mode Read DTC";
				break;
			case 0x04:
				ss << hex << cf->can_id << ": Mode Clear DTC";
				break;
			case 0x05:
				ss << hex << cf->can_id << ": Mode Non-CAN Test Results";
				break;
			case 0x06:
				ss << hex << cf->can_id << ": Mode CAN Test Results";
				break;
			case 0x07:
				ss << hex << cf->can_id << ": Mode Read Pending DTCs";
				break;
			case 0x08:
				ss << hex << cf->can_id << ": Mode Control Operations";
				break;
			case 0x09:
				ss << hex << cf->can_id << ": Mode Vehicle Information";
				resp = Module::vehicleInfoRequest(resp, cf);
				if(fuzz) {
					if(cf->data[2+offset] == 2) {
						if (getFuzzVin()) {
							resp = fuzzResp(resp, cf);
							//resp = inject_vin_checksum(resp);
						}
						doFuzz = false;
					}
				}
				break;
			case 0x0A:
				ss << hex << cf->can_id << ": Mode Read Perm DTCs";
				break;
				// UDS
			case 0x10:
				ss << hex << cf->can_id << ": Initiate Diagnostic";
				// ss << hex << cf->can_id << ": Diagnostic Control";
				if (resp.size() == 0 && getNegativeResponder(cf) > -1) { // todo: negative with offset
					if(_protocol==TP20) errPkt << hex << getNegativeResponder(cf) << "#1000037F1012";
					else if(cf->can_id==0x6F1) errPkt << hex << getNegativeResponder(cf) << "#F1037F1012"; // BMW
					else errPkt << hex << getNegativeResponder() << "#037F1012AAAAAAAA";
					resp.push_back(new CanFrame(errPkt.str()));
				}
				break;
			case 0x11:
				ss << hex << cf->can_id << ": ECU Reset";
				break;
			case 0x12:
				ss << hex << cf->can_id << ": (GMLAN) Read Failure Record";
				break;
			case 0x14:
				ss << hex << cf->can_id << ": Clear DTC";
				break;
			case 0x17: // ?? renault
				ss << hex << cf->can_id << ": 0x17 ??";
				if (resp.size() == 0 && getNegativeResponder(cf) > -1) {
					errPkt << hex << getNegativeResponder(cf) << "#025700aaaaaaaaaa";
					resp.push_back(new CanFrame(errPkt.str()));
				}
				break;
			case 0x19:
				ss << hex << cf->can_id << ": Read DTC";
				break;
			case 0x1A:
				ss << hex << cf->can_id << ": (GMLAN) Read DID by ID";
				if (resp.size() == 0 && getNegativeResponder(cf) > -1) {
					if(_protocol==TP20) errPkt << hex << getNegativeResponder(cf) << "#1000037F1A11";
					else if(cf->can_id==0x6F1) errPkt << hex << getNegativeResponder(cf) << "#F1037F1A11"; // BMW
					else errPkt << hex << getNegativeResponder() << "#037F1A11AAAAAAAA";
					resp.push_back(new CanFrame(errPkt.str()));
				}
				break;
			case 0x20:
				ss << hex << cf->can_id << ": (GMLAN) Restart Communications";
				break;
			case 0x21: // only TP20?
				ss << hex << cf->can_id << ": KWP2000 readDataByLocalIdentifier ";
				if(0 && resp.size() == 0 && gd.autoresponse){ // todo: TP20 autoresponses
					Module *responder = gd.get_module(getPositiveResponder(cf));
					if(responder!=NULL){
						char str[30];
						snprintf(str, 23, "%03X#0462%02X%02X01010101", responder->getArbId(), cf->data[2+offset], cf->data[3+offset]);
						resp.push_back(new CanFrame(str));
					}
				}
				if (resp.size() == 0 && getNegativeResponder(cf) > -1) {
					if(_protocol==TP20) errPkt << hex << getNegativeResponder(cf) << "#1880037F2178"; // porche: #19037F21784A0002 ??
					else if(cf->can_id==0x6F1) errPkt << hex << getNegativeResponder(cf) << "#F1037F2231"; // BMW
					else errPkt << hex << getNegativeResponder() << "#037F2231AAAAAAAA";
					resp.push_back(new CanFrame(errPkt.str()));
				}
				break;
			case 0x22:
				ss << hex << cf->can_id << ": Read Data by ID";
				resp = Module::fetchHistory(cf, 3);
				if(resp.size() == 0 && gd.autoresponse){ // todo: offset responses
					Module *responder = gd.get_module(getPositiveResponder(cf));
					if(responder!=NULL){
						char str[30];
						snprintf(str, 23, "%03X#0462%02X%02X01010101", responder->getArbId(), cf->data[2+offset], cf->data[3+offset]);
						resp.push_back(new CanFrame(str));
					}
				}
				if (resp.size() == 0 && getNegativeResponder(cf) > -1) {
					if(_protocol==TP20) errPkt << hex << getNegativeResponder(cf) << "#1000037F2231"; // #1080037F2278
					else if(cf->can_id==0x6F1){ // BMW
						if(gd.get_module(0x600+cf->data[0])) errPkt << hex << getNegativeResponder(cf) << "#F1037F2231";
					} else errPkt << hex << getNegativeResponder() << "#037F2231AAAAAAAA";
					if(!errPkt.str().empty()) resp.push_back(new CanFrame(errPkt.str()));
				}
				break;
			case 0x23:
				ss << hex << cf->can_id << ": Read Memory by Address";
				break;
			case 0x24:
				ss << hex << cf->can_id << ": Read Scaling by ID";
				break;
			case 0x27:
				ss << hex << cf->can_id << ": Security Access";
				resp = Module::fetchHistory(cf, 3);
				if (resp.size() == 0 && getNegativeResponder(cf) > -1) {
					if(cf->can_id==0x6F1) errPkt << hex << getNegativeResponder(cf) << "#F1037F2735"; // BMW
					else {
						if(gd.autoresponse) errPkt << hex << getNegativeResponder() << "#0267" << cf->data[2+offset] << "0000000000";
						else errPkt << hex << getNegativeResponder() << "#037F273500000000";
						resp.push_back(new CanFrame(errPkt.str()));
					}
				}
				break;
			case 0x28:
				ss << hex << cf->can_id << ": (GMLAN) Stop Communications";
				break;
			case 0x2A:
				ss << hex << cf->can_id << ": Read Data by ID Periodic";
				break;
			case 0x2C:
				ss << hex << cf->can_id << ": Define Data ID";
				break;
			case 0x2E:
				ss << hex << cf->can_id << ": Write Data by ID";
				if(resp.size() == 0 && gd.autowrite){
					Module *responder = gd.get_module(getPositiveResponder(cf));
					if(responder!=NULL){
						char str[30];
						snprintf(str, 23, "%03X#036E%02X%02XAAAAAAAA", responder->getArbId(), cf->data[2+offset], cf->data[3+offset]);
						cout << str << endl;
						resp.push_back(new CanFrame(str));
					}
				}
				if (resp.size() == 0 && getNegativeResponder(cf) > -1) {
					if(cf->can_id==0x6F1) errPkt << hex << getNegativeResponder(cf) << "#F1037F2E31"; // BMW
					else errPkt << hex << getNegativeResponder() << "#037F2E31AAAAAAAA";
					resp.push_back(new CanFrame(errPkt.str()));
				}
				break;
			case 0x2F:
				ss << hex << cf->can_id << ": IO Control by ID";
				break;
			case 0x31:
				ss << hex << cf->can_id << ": Routine Control";
				break;
			case 0x34:
				ss << hex << cf->can_id << ": Request Download";
				break;
			case 0x35:
				ss << hex << cf->can_id << ": Request Upload";
				// for peugeot: 752#0635C000000C11 -> 652#101A75C08000000C
				if(getPositiveResponder(cf) > -1){
					uint8_t data[8]={ 0x10, 0x1A, 0x75, cf->data[2],  0x80, cf->data[3], cf->data[4], cf->data[5] };
					uint8_t mask[8]={ 0xFF, 0x00, 0xFF, 0xFF		, 0x00, 0xFF,		 0xFF,		  0xFF };
					resp=gd.get_module(getPositiveResponder(cf))->getPacketsByMask(data, mask, 8);
					if(resp.size()>0 && resp[0]->queue.size()>0) for(vector<CanFrame *>::iterator it2 = resp[0]->queue.begin(); it2 != resp[0]->queue.end(); ++it2) resp.push_back(*it2);
					if(resp.size()==0){
						errPkt << hex << getPositiveResponder() << "#037F351200000000";
						resp.push_back(new CanFrame(errPkt.str()));
					}
				}
				break;
			case 0x36:
				ss << hex << cf->can_id << ": Transfer Data";
				break;
			case 0x37:
				ss << hex << cf->can_id << ": Request Transfer Exit";
				break;
			case 0x38: 
				ss << hex << cf->can_id << ": Request Transfer File";
				break;
			case 0x3D:
				ss << hex << cf->can_id << ": Write Memory by Address";
				break;
			case 0x3E:
				ss << hex << cf->can_id << ": Tester Present";
				break;
			case 0x83:
				ss << hex << cf->can_id << ": Access Timing";
				break;
			case 0x84:
				ss << hex << cf->can_id << ": Secured Data Transfer";
				break;
			case 0x85:
				ss << hex << cf->can_id << ": Control DTC Settings";
				break;
			case 0x86:
				ss << hex << cf->can_id << ": Response on Event";
				break;
			case 0x87:
				ss << hex << cf->can_id << ": Link Control";
				break;
			case 0xA2:
				ss << hex << cf->can_id << ": (GMLAN) Programmed State";
				break;
			case 0xA5:
				ss << hex << cf->can_id << ": (GMLAN) Programing Mode";
				break;
			case 0xA9:
				ss << hex << cf->can_id << ": (GMLAN) Read Diag Info";
				break;
			case 0xAA:
				ss << hex << cf->can_id << ": (GMLAN) Read Data by ID";
				break;
			case 0xAE:
				ss << hex << cf->can_id << ": (GMLAN) Device Control";
				break;
			default:
				ss << hex << cf->can_id << ": Unknown request " << hex << (unsigned int)cf->data[1+offset];
				break;
		}
	}
	if(doFuzz) {
		resp = fuzzResp(resp, cf);
	}
	if(ss.str().size() > 0) gd.Msg(ss.str());
	return resp;
}
