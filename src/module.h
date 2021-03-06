#ifndef UDS_MODULE_H
#define UDS_MODULE_H

#include <cstddef>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef SDL
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#endif

// very long answers could contain more than 0xff characters but haven't seen in TP20 yet
#define is_tp20_multipacket(cf) (!(cf->data[0]&0x10) && cf->data[0]<0x40)	// not last. more to follow
#define is_tp20_last_packet(cf) (cf->data[0]&0x10)					// 0x10 || 0x30
#define is_tp20_waiting_ack(cf) !(cf->data[0]&0x20)					// 0x00 || 0x10

#define is_first_multipacket(cf, offset) ((cf->data[offset]&0xF0) == 0x10 && cf->len==8 && (((cf->data[0+offset]&0x0F)<<8)+cf->data[1+offset]+1) > (7-offset))
#define is_first_packet(cf, offset) (cf->data[offset]<8 ||is_first_multipacket(cf, offset))
#define PACKET_COUNT(cf, offset) ((cf->data[offset] < 0x10 ? 1 : (unsigned int)ceil((double)(((cf->data[offset]&0x0F)<<8)+cf->data[1+offset]+1)/(7-offset))))

extern const char *Protocol_str[]; // module.cc

// also update Protocol_str in gamedata.cc
enum Protocol {
	UDS,
	BMW_P,	// UDS through 0x6F1
	TP20	// VAG
};

#include "gamedata.h"
#include "canframe.h"

using namespace std;

class GameData;


#define STATE_IDLE      0
#define STATE_ACTIVE    1
#define STATE_MOUSEOVER 2
#define STATE_SELECTED  3

#define MODULE_H 30
#define MODULE_W 35

#define ACTIVE_TICK_DELAY 100
#define DEFAULT_VIN "PWN3D OP3N G4R4G3"

class Module
{
 public:
  Module(int id);
  ~Module();
  void setArbId(int i) {arbId = i;}
  int getArbId() { return arbId; }
  void incMatchedISOTP() {matched_isotp++;}
  void incMissedISOTP() {missed_isotp++;}
  int getMatchedISOTP() { return matched_isotp; }
  int getMissedISOTP() { return missed_isotp; }
  void setPaddingByte(char b) {padding = true; padding_byte = b; }
  float confidence();
  void setPositiveResponderID(int i) { positive_responder_id = i; }
  int getPositiveResponder() { return positive_responder_id; }
  int getPositiveResponder(struct canfd_frame *); // also works for BMW
  void setNegativeResponderID(int i) { negative_responder_id = i; }
  int getNegativeResponder() { return negative_responder_id; }
  int getNegativeResponder(struct canfd_frame *); // also works for BMW
  void setResponder(bool v) { responder = v; }
  bool isResponder() { return responder; }
  void addPacket(struct canfd_frame *);
  void addPacket_TP20(struct canfd_frame *);
  void addPacket(string);
  void addPacket_front(string);
  void repair_queue(struct canfd_frame *);
  vector <CanFrame *>getHistory() { return can_history; }
  vector <CanFrame *> *getHistory_ptr() { return &can_history; }
  vector <CanFrame *>getPacketsByBytePos(unsigned int, unsigned char);
  vector <CanFrame *>getPacketsByMask(uint8_t*, uint8_t*,int);
  bool foundResponse(Module *);
  Module *get_module_with_response(int);
  Module *get_module_gmlan_uudt(int);
  int getState();		// for GUI
  void setState(int s); // for GUI
  int getX() { return _x; }
  int getY() { return _y; }
  void setX(int x) { _x = x; }
  void setY(int y) { _y = y; }
#ifdef SDL
  SDL_Texture *getIdTexture() { return id_texture; }
  void setIdTexture(SDL_Texture *t) { id_texture = t; }
#endif
  //vector <CanFrame *>getResponse(struct canfd_frame *,bool);
  vector <CanFrame *>getResponse(vector<struct canfd_frame>,bool);
  void toggleFakeResponses() { _fake_responses ? _fake_responses = false : _fake_responses = true; }
  void setFakeResponses(bool t) { _fake_responses = t; }
  bool getFakeResponses() { return _fake_responses; }
  void toggleIgnore() { _ignore ? _ignore = false : _ignore = true; }
  void setIgnore(bool t) { _ignore = t; }
  bool getIgnore() { return _ignore; }
  void toggleFuzzVin() { _fuzz_vin ? _fuzz_vin = false : _fuzz_vin = true; }
  void setFuzzVin(bool t) { _fuzz_vin = t; }
  bool getFuzzVin() { return _fuzz_vin; }
  unsigned int getFuzzLevel() { return _fuzz_level; }
  void setFuzzLevel(unsigned int);
  unsigned char calc_vin_checksum(char *, int);
  vector <CanFrame *>inject_vin_checksum(vector <CanFrame *>);
  vector <CanFrame *>fetchHistory(struct canfd_frame *, int);
  vector <CanFrame *>showCurrentData(vector <CanFrame *>, struct canfd_frame *);
  vector <CanFrame *>vehicleInfoRequest(vector <CanFrame *>, struct canfd_frame *);
  vector <CanFrame *>fuzzResp(vector <CanFrame *>, struct canfd_frame *);
  CanFrame *createPacket(int, char *, int);
  void setActiveTicks(int i) { _activeTicks = i; }
  unsigned int getActiveTicks() { return _activeTicks; }
  void queue_set(vector <CanFrame *> v, int offset){ _queue.assign(v.begin() + offset, v.end()); }
  vector <CanFrame *> *queue_get() { return &_queue; }
  bool get_sendall() { return _sendall; }
  void set_sendall() { _sendall = true; }
  void clear_sendall() { _sendall = false; }
  void setProtocol(Protocol p){ _protocol=p; }
  void setProtocol(const char*);
  Protocol getProtocol(){ return _protocol; }
  void setOffset(int offset){ _offset=offset; }
  int getOffset(){ return _offset; }
 private:
  int arbId;
  int matched_isotp = 0;
  int missed_isotp = 0;
  bool padding = false; // not used. yet
  char padding_byte;	// not used. yet
  bool responder = false;
  int state = STATE_IDLE;
  int _activeTicks = 0;
  int _x = 0;
  int _y = 0;
  vector<CanFrame *>can_history;
  vector<CanFrame *>_queue; // hold multi-line response temporarily until 0x30... comes
  bool _sendall = false;		// send all at once. don't pick from results
  bool _expect_consecutive_frame = false;
  CanFrame *_repair_frame = NULL; // sometimes we have missing packets. try to repair queue (pointer to can_history element)
  unsigned int _repair_frame_num = 0;
  int positive_responder_id = -1;
  int negative_responder_id = -1;
  unsigned int _fuzz_level = 0;
  bool _fake_responses = false;
  bool _fuzz_vin = false;
  bool _ignore = false;
  Protocol _protocol = UDS;
  int _offset = 0;
#ifdef SDL
  SDL_Texture *id_texture = NULL;
#endif
};

#endif
