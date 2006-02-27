#ifndef _OLB_CONFIG_H_
#define _OLB_CONFIG_H_
/******************************************************************************/
/*                                                                            */
/*                       X r d O l d C o n f i g . h h                        */
/*                                                                            */
/* (C) 2003 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*                DE-AC03-76-SFO0515 with the Deprtment of Energy             */
/******************************************************************************/

//         $Id$

#include "XrdOlb/XrdOlbPList.hh"
#include "XrdOlb/XrdOlbTypes.hh"
#include "XrdOuc/XrdOucTList.hh"
  
class XrdNetSecurity;
class XrdNetSocket;
class XrdOlbMeter;
class XrdOucError;
class XrdOucName2Name;
class XrdOucProg;
class XrdOucStream;

class XrdOlbConfig
{
public:

int   Configure(int argc, char **argv);
int   ConfigXeq(char *var, XrdOucStream &Config, XrdOucError *eDest);
int   GenLocalPath(const char *oldp, char *newp);
int   GenMsgID(char *oldmid, char *buff, int blen);
int   inSuspend();
int   inNoStage();
int   Manager() {return isManager;}
int   Server()  {return isServer;}

int         LUPDelay;     // Maximum delay at look-up
int         LUPHold;      // Maximum hold  at look-up (in millisconds)
int         DRPDelay;     // Maximum delay for dropping an offline server
int         SRVDelay;     // Minimum delay at startup
int         SUPCount;     // Minimum server count
int         SUPLevel;     // Minimum server count as floating percentage
int         SUPDelay;     // Maximum delay when server count falls below min
int         SUSDelay;     // Maximum delay when suspended
int         MaxLoad;      // Maximum load
int         MaxDelay;     // Maximum load delay
int         MsgTTL;       // Maximum msg lifetime
int         RefReset;     // Min seconds    before a global ref count reset
int         RefTurn;      // Min references before a global ref count reset
int         AskPerf;      // Seconds between perf queries
int         AskPing;      // Number of ping requests per AskPerf window
int         LogPerf;      // AskPerf intervals before logging perf

int         PortTCP;      // TCP Port to listen on

int         P_cpu;        // % CPU Capacity in load factor
int         P_fuzz;       // %     Capacity to fuzz when comparing
int         P_io;         // % I/O Capacity in load factor
int         P_load;       // % MSC Capacity in load factor
int         P_mem;        // % MEM Capacity in load factor
int         P_pag;        // % PAG Capacity in load factor

long long   DiskMin;      // Minimum KB needed of space in a partition
long long   DiskHWM;      // Minimum KB needed of space to requalify
int         DiskLinger;   // Manager Only
int         DiskAsk;      // Seconds between disk space reclaculations
int         DiskWT;       // Seconds to defer client while waiting for space
int         DiskSS;       // This is a staging server

int         sched_RR;     // 1 -> Simply do round robin scheduling
int         doWait;       // 1 -> Wait for a data end-point
int         Disabled;     // 1 -> Delay director requests

XrdOucName2Name *lcl_N2N; // Server Only

char        *N2N_Lib;     // Server Only
char        *N2N_Parms;   // Server Only
char        *LocalRoot;   // Server Only
char        *MsgGID;
int          MsgGIDL;
const char  *myName;
const char  *myDomain;
const char  *myInsName;
const char  *myInstance;
const char  *mySID;
XrdOucTList *myManagers;

char        *NoStageFile;
char        *SuspendFile;

XrdOucProg  *ProgCH;      // Server only chmod
XrdOucProg  *ProgMD;      // Server only mkdir
XrdOucProg  *ProgMV;      // Server only mv
XrdOucProg  *ProgRD;      // Server only rmdir
XrdOucProg  *ProgRM;      // Server only rm

XrdOlbPList_Anchor PathList;
XrdOlbMeter       *Meter;
XrdNetSocket      *AdminSock;
XrdNetSocket      *AnoteSock;
XrdNetSocket      *RedirSock;

      XrdOlbConfig() {ConfigDefaults();}
     ~XrdOlbConfig() {}

private:

XrdNetSocket *ASocket(char *path, const char *fn, mode_t mode, int isudp=0);
char *ASPath(char *path, const char *fn, mode_t mode);
void ConfigDefaults(void);
int  ConfigN2N(void);
int  ConfigProc(int getrole=0);
int  isExec(XrdOucError *eDest, const char *ptype, char *prog);
int  PidFile(void);
int  setupManager(void);
int  setupServer(void);
void UnderCover(void);
void Usage(int rc);
int  xapath(XrdOucError *edest, XrdOucStream &Config);
int  xallow(XrdOucError *edest, XrdOucStream &Config);
int  xcache(XrdOucError *edest, XrdOucStream &Config);
int  Fsysadd(XrdOucError *edest, int chk, char *fn);
int  xdelay(XrdOucError *edest, XrdOucStream &Config);
int  xfsxq(XrdOucError *edest, XrdOucStream &Config);
int  xfxhld(XrdOucError *edest, XrdOucStream &Config);
int  xlclrt(XrdOucError *edest, XrdOucStream &Config);
int  xnml(XrdOucError *edest, XrdOucStream &Config);
int  xpath(XrdOucError *edest, XrdOucStream &Config);
int  xperf(XrdOucError *edest, XrdOucStream &Config);
int  xpidf(XrdOucError *edest, XrdOucStream &Config);
int  xping(XrdOucError *edest, XrdOucStream &Config);
int  xport(XrdOucError *edest, XrdOucStream &Config);
int  xprep(XrdOucError *edest, XrdOucStream &Config);
int  xrole(XrdOucError *edest, XrdOucStream &Config);
int  xsched(XrdOucError *edest, XrdOucStream &Config);
int  xspace(XrdOucError *edest, XrdOucStream &Config);
int  xsubs(XrdOucError *edest, XrdOucStream &Config);
int  xthreads(XrdOucError *edest, XrdOucStream &Config);
int  xtrace(XrdOucError *edest, XrdOucStream &Config);

XrdNetSecurity   *Police;
XrdOucTList      *monPath;     // cache directive paths
XrdOucTList      *monPathP;    // path  directive paths (w or s only)
char             *AdminPath;
int               AdminMode;
char             *pidPath;
char             *ConfigFN;
int               isManager;
int               isServer;
char             *perfpgm;
int               perfint;
int               cachelife;
int               pendplife;
};
#endif
