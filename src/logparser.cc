#include "logparser.h"

#define COMMENTSZ 200

/* buffer sizes for CAN frame string representations */
#define CL_ID (sizeof("12345678##1"))
#define CL_DATA sizeof(".AA")
#define CL_BINDATA sizeof(".10101010")

 /* CAN FD ASCII hex short representation with DATA_SEPERATORs */
#define CL_CFSZ (2*CL_ID + 64*CL_DATA)

/* CAN FD ASCII hex long representation with binary output */
#define CL_LONGCFSZ (2*CL_ID + sizeof("   [255]  ") + (64*CL_BINDATA))

#define BUFSZ (sizeof("(1345212884.318850)") + IFNAMSIZ + 4 + CL_CFSZ + COMMENTSZ) /* for one line in the logfile */

extern GameData gd;

LogParser::LogParser() {
}

LogParser::~LogParser() {
  if(log_opened) {
    fclose(logfp);
    log_opened = false;
  }
}

string LogParser::processNext() {
  static int line;
  static char buf[BUFSZ], device[BUFSZ], ascframe[BUFSZ];
  static struct timeval log_tv;
  struct canfd_frame cf;
  char *fret;
  if (!log_opened) {
    logfp = fopen(logfile.c_str(), "r");
    if(!logfp) {
      log_eof = true;
      return "Couldn't open logfile " + logfile;
    } else {
      log_eof = false;
      log_opened = true;
	  line=0;
	  return "logfile: " + logfile;
    }
  } else {  // Log already opened
    /* read first non-comment frame from logfile */
    while ((fret = fgets(buf, BUFSZ-1, logfp)) != NULL && buf[0] != '(') {
	  line++;
      if (strlen(buf) >= BUFSZ-2) return "comment line too long for input buffer";
    }
	line++;
    if(!fret) { 
      log_eof = true; 
    } else {
      if (sscanf(buf, "(%ld.%ld) %s %s", &log_tv.tv_sec, &log_tv.tv_usec, device, ascframe) != 4) {
		  printf("%s\n",buf);
		  return "incorrect line format in logfile ("+to_string(line)+")";
      }
      if((gd.getCan()->parse_canframe(ascframe, &cf))!=0) gd.processPkt(&cf);
      if(gd.getVerbose()>1) return "Packet: " + string(ascframe);
	  else return "";
    }
  }
  return "Finished processing logfile";
}
