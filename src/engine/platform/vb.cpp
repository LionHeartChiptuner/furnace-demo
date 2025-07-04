/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2025 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "vb.h"
#include "../engine.h"
#include "IconsFontAwesome4.h"
#include "furIcons.h"
#include <math.h>

//#define rWrite(a,v) pendingWrites[a]=v;
#define rWrite(a,v) if (!skipRegisterWrites) {writes.push(QueuedWrite(a,v)); if (dumpWrites) {addWrite(a,v);} }
#define chWrite(c,a,v) rWrite(0x400+((c)<<6)+((a)<<2),v);

#define CHIP_DIVIDER 16

const char* regCheatSheetVB[]={
  "Wave0", "000",
  "Wave1", "080",
  "Wave2", "100",
  "Wave3", "180",
  "Wave4", "200",
  "ModTable", "280",

  "S1INT", "400",
  "S1LRV", "404",
  "S1FQL", "408",
  "S1FQH", "40C",
  "S1EV0", "410",
  "S1EV1", "414",
  "S1RAM", "418",

  "S2INT", "440",
  "S2LRV", "444",
  "S2FQL", "448",
  "S2FQH", "44C",
  "S2EV0", "450",
  "S2EV1", "454",
  "S2RAM", "458",

  "S3INT", "480",
  "S3LRV", "484",
  "S3FQL", "488",
  "S3FQH", "48C",
  "S3EV0", "480",
  "S3EV1", "484",
  "S3RAM", "488",

  "S4INT", "4C0",
  "S4LRV", "4C4",
  "S4FQL", "4C8",
  "S4FQH", "4CC",
  "S4EV0", "4C0",
  "S4EV1", "4C4",
  "S4RAM", "4C8",

  "S5INT", "500",
  "S5LRV", "504",
  "S5FQL", "508",
  "S5FQH", "50C",
  "S5EV0", "510",
  "S5EV1", "514",
  "S5RAM", "518",

  "S5SWP", "51C",

  "S6INT", "540",
  "S6LRV", "544",
  "S6FQL", "548",
  "S6FQH", "54C",
  "S6EV0", "550",
  "S6EV1", "554",
  "S6RAM", "558",

  "RESET", "580",
  NULL
};

const char** DivPlatformVB::getRegisterSheet() {
  return regCheatSheetVB;
}

void DivPlatformVB::acquireDirect(blip_buffer_t** bb, size_t len) {
  for (int i=0; i<6; i++) {
    oscBuf[i]->begin(len);
    vb->oscBuf[i]=oscBuf[i];
  }

  vb->bb[0]=bb[0];
  vb->bb[1]=bb[1];

  for (size_t h=0; h<len; h++) {
    if (!writes.empty()) {
      QueuedWrite w=writes.front();
      vb->Write(h,w.addr,w.val);
      regPool[w.addr>>2]=w.val;
      writes.pop();
    } else {
      break;
    }
  }
  vb->EndFrame(len);

  for (int i=0; i<6; i++) {
    oscBuf[i]->end(len);
  }
}

void DivPlatformVB::updateWave(int ch) {
  if (romMode) return;
  if (ch>=5) return;

  for (int i=0; i<32; i++) {
    rWrite((ch<<7)+(i<<2),chan[ch].ws.output[(i+chan[ch].antiClickWavePos)&31]);
  }
  chan[ch].antiClickWavePos&=31;
}

void DivPlatformVB::writeEnv(int ch, bool upperByteToo) {
  chWrite(ch,0x04,(chan[ch].outVol<<4)|(chan[ch].envLow&15));
  if (ch<5 || upperByteToo) {
    chWrite(ch,0x05,chan[ch].envHigh);
  }
}

void DivPlatformVB::tick(bool sysTick) {
  bool mustUpdateWaves=false;

  for (int i=0; i<6; i++) {
    // anti-click
    int actualFreq=2047-chan[i].freq;
    if (antiClickEnabled && !screwThis && sysTick && actualFreq>0) {
      chan[i].antiClickPeriodCount+=(chipClock/MAX(parent->getCurHz(),1.0f));
      chan[i].antiClickWavePos+=chan[i].antiClickPeriodCount/actualFreq;
      chan[i].antiClickPeriodCount%=actualFreq;
    }

    chan[i].std.next();
    // this is handled first to work around an envelope problem
    // once envelope is over, you cannot enable it again unless you retrigger the channel
    if (chan[i].std.phaseReset.had && chan[i].std.phaseReset.val==1) {
      chWrite(i,0x00,0x80);
      chan[i].intWritten=true;
      chan[i].antiClickWavePos=0;
      chan[i].antiClickPeriodCount=0;
    }
    if (chan[i].std.vol.had) {
      chan[i].outVol=VOL_SCALE_LINEAR(chan[i].vol&15,MIN(15,chan[i].std.vol.val),15);
      writeEnv(i);
    }
    if (NEW_ARP_STRAT) {
      chan[i].handleArp();
    } else if (chan[i].std.arp.had) {
      if (!chan[i].inPorta) {
        chan[i].baseFreq=NOTE_PERIODIC(parent->calcArp(chan[i].note,chan[i].std.arp.val));
      }
      chan[i].freqChanged=true;
    }
    if (i==5 && chan[i].std.duty.had) {
      if ((chan[i].std.duty.val&7)!=((chan[i].envHigh>>4)&7)) {
        chan[i].envHigh&=~0x70;
        chan[i].envHigh|=(chan[i].std.duty.val&7)<<4;
        writeEnv(i,true);
      }
    }
    if (chan[i].std.wave.had) {
      if (chan[i].wave!=chan[i].std.wave.val || chan[i].ws.activeChanged()) {
        chan[i].wave=chan[i].std.wave.val;
        if (romMode) {
          chWrite(i,0x06,chan[i].wave);
        }
        chan[i].ws.changeWave1(chan[i].wave);
        if (!chan[i].keyOff) chan[i].keyOn=true;
      }
    }
    if (chan[i].std.panL.had) {
      chan[i].pan&=0x0f;
      chan[i].pan|=(chan[i].std.panL.val&15)<<4;
    }
    if (chan[i].std.panR.had) {
      chan[i].pan&=0xf0;
      chan[i].pan|=chan[i].std.panR.val&15;
    }
    if (chan[i].std.panL.had || chan[i].std.panR.had) {
      chWrite(i,0x01,isMuted[i]?0:chan[i].pan);
    }
    if (chan[i].std.pitch.had) {
      if (chan[i].std.pitch.mode) {
        chan[i].pitch2+=chan[i].std.pitch.val;
        CLAMP_VAR(chan[i].pitch2,-32768,32767);
      } else {
        chan[i].pitch2=chan[i].std.pitch.val;
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].active) {
      if (chan[i].ws.tick() || (chan[i].std.phaseReset.had && chan[i].std.phaseReset.val==1)) {
        if (!romMode) {
          chan[i].deferredWaveUpdate=true;
        }
        mustUpdateWaves=true;
      }
    }
    if (chan[i].freqChanged || chan[i].keyOn || chan[i].keyOff) {
      chan[i].freq=parent->calcFreq(chan[i].baseFreq,chan[i].pitch,chan[i].fixedArp?chan[i].baseNoteOverride:chan[i].arpOff,chan[i].fixedArp,true,0,chan[i].pitch2,chipClock,CHIP_DIVIDER);
      if (chan[i].freq<1) chan[i].freq=1;
      if (chan[i].freq>2047) chan[i].freq=2047;
      chan[i].freq=2048-chan[i].freq;
      chWrite(i,0x02,chan[i].freq&0xff);
      chWrite(i,0x03,chan[i].freq>>8);
      if (chan[i].keyOn) {
      }
      if (chan[i].keyOff) {
        chWrite(i,0x04,0);
      }
      if (chan[i].keyOn) chan[i].keyOn=false;
      if (chan[i].keyOff) chan[i].keyOff=false;
      chan[i].freqChanged=false;
    }
  }

  // trigger wave changes
  if (mustUpdateWaves && !romMode) {
    if (!screwThis) {
      rWrite(0x580,1);
    }
    for (int i=0; i<5; i++) {
      //if (chan[i].deferredWaveUpdate) {
        //chan[i].deferredWaveUpdate=false;
        updateWave(i);
      //}
    }
    if (!screwThis) {
      // restore channel state...
      for (int i=0; i<6; i++) {
        if (chan[i].intWritten) {
          chWrite(i,0x00,0x80);
        }
      }
    }
  }

  for (int i=0; i<6; i++) {
    if ((chan[i].envHigh&3)==0) {
      chan[i].hasEnvWarning=0;
    } else {
      switch (vb->EnvelopeModMask[i]) {
        case 0: // envelope OK
          chan[i].hasEnvWarning=0;
          break;
        case 1: // envelope has finished
          chan[i].hasEnvWarning=21;
          break;
        case 2: // can't envelope
          chan[i].hasEnvWarning=22;
          break;
      }
    }
  }
  /*if (vb->ModLock) {
    chan[4].hasEnvWarning=4;
  }*/
}

int DivPlatformVB::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins,DIV_INS_PCE);
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=NOTE_PERIODIC(c.value);
        chan[c.chan].freqChanged=true;
        chan[c.chan].note=c.value;
      }
      chan[c.chan].active=true;
      chan[c.chan].keyOn=true;
      chan[c.chan].macroInit(ins);
      if (c.chan==4 && chan[c.chan].insChanged && ins->fds.initModTableWithFirstWave) {
        chWrite(4,0x00,0x00);
        for (int i=0; i<32; i++) {
          modTable[i]=ins->fds.modTable[i];
          rWrite(0x280+(i<<2),modTable[i]);
        }
        chWrite(4,0x00,0x80);
      }
      if (!parent->song.brokenOutVol && !chan[c.chan].std.vol.will) {
        chan[c.chan].outVol=chan[c.chan].vol;
        writeEnv(c.chan);
      }
      if (chan[c.chan].wave<0) {
        chan[c.chan].wave=0;
        chan[c.chan].ws.changeWave1(chan[c.chan].wave);
      }
      chan[c.chan].ws.init(ins,32,63,chan[c.chan].insChanged);
      chan[c.chan].insChanged=false;
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].active=false;
      chan[c.chan].keyOff=true;
      chan[c.chan].macroInit(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].ins=c.value;
        chan[c.chan].insChanged=true;
      }
      break;
    case DIV_CMD_VOLUME:
      if (chan[c.chan].vol!=c.value) {
        chan[c.chan].vol=c.value;
        if (!chan[c.chan].std.vol.has) {
          chan[c.chan].outVol=c.value;
          if (chan[c.chan].active) {
            writeEnv(c.chan);
          }
        }
      }
      break;
    case DIV_CMD_GET_VOLUME:
      if (chan[c.chan].std.vol.has) {
        return chan[c.chan].vol;
      }
      return chan[c.chan].outVol;
      break;
    case DIV_CMD_PITCH:
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_WAVE:
      chan[c.chan].wave=c.value;
      chan[c.chan].ws.changeWave1(chan[c.chan].wave);
      if (romMode) {
        chWrite(c.chan,0x06,chan[c.chan].wave);
      }
      chan[c.chan].keyOn=true;
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=NOTE_PERIODIC(c.value2);
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_STD_NOISE_MODE:
      if (c.chan!=5) break;
      chan[c.chan].envHigh&=~0x70;
      chan[c.chan].envHigh|=(c.value&7)<<4;
      writeEnv(c.chan,true);
      break;
    case DIV_CMD_STD_NOISE_FREQ:
      chan[c.chan].envHigh&=~3;
      chan[c.chan].envHigh|=(c.value>>4)&3;
      chan[c.chan].envLow=c.value&15;
      writeEnv(c.chan,true);
      break;
    case DIV_CMD_FDS_MOD_DEPTH: // set modulation
      if (c.chan!=4) break;
      modulation=(c.value&15)<<4;
      modType=true;
      chWrite(4,0x07,modulation);
      if (modulation!=0) {
        chan[c.chan].envHigh&=~0x70;
        chan[c.chan].envHigh|=0x40|(c.value&0xf0);
      } else {
        chan[c.chan].envHigh&=~0x70;
      }
      writeEnv(4);
      break;
    case DIV_CMD_GB_SWEEP_TIME: // set sweep
      if (c.chan!=4) break;
      modulation=c.value;
      modType=false;
      chWrite(4,0x07,modulation);
      if (modulation!=0) {
        chan[c.chan].envHigh&=~0x70;
        chan[c.chan].envHigh|=0x40;
      } else {
        chan[c.chan].envHigh&=~0x70;
      }
      writeEnv(4);
      break;
    case DIV_CMD_FDS_MOD_WAVE: { // set modulation wave
      if (c.chan!=4) break;
      DivWavetable* wt=parent->getWave(c.value);
      chWrite(4,0x00,0x00);
      for (int i=0; i<32; i++) {
        if (wt->max<1 || wt->len<1) {
          modTable[i]=0;
          rWrite(0x280+(i<<2),0);
        } else {
          int data=(wt->data[i*wt->len/32]*255/wt->max)-128;
          if (data<-128) data=-128;
          if (data>127) data=127;
          modTable[i]=data;
          rWrite(0x280+(i<<2),modTable[i]);
        }
      }
      chWrite(4,0x00,0x80);
      break;
    }
    case DIV_CMD_PANNING: {
      chan[c.chan].pan=(c.value&0xf0)|(c.value2>>4);
      chWrite(c.chan,0x01,isMuted[c.chan]?0:chan[c.chan].pan);
      break;
    }
    case DIV_CMD_LEGATO:
      chan[c.chan].baseFreq=NOTE_PERIODIC(c.value+((HACKY_LEGATO_MESS)?(chan[c.chan].std.arp.val):(0)));
      chan[c.chan].freqChanged=true;
      chan[c.chan].note=c.value;
      break;
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_PCE));
      }
      if (!chan[c.chan].inPorta && c.value && !parent->song.brokenPortaArp && chan[c.chan].std.arp.will && !NEW_ARP_STRAT) chan[c.chan].baseFreq=NOTE_PERIODIC(chan[c.chan].note);
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_GET_VOLMAX:
      return 15;
      break;
    case DIV_CMD_MACRO_OFF:
      chan[c.chan].std.mask(c.value,true);
      break;
    case DIV_CMD_MACRO_ON:
      chan[c.chan].std.mask(c.value,false);
      break;
    case DIV_CMD_MACRO_RESTART:
      chan[c.chan].std.restart(c.value);
      break;
    default:
      break;
  }
  return 1;
}

void DivPlatformVB::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  chWrite(ch,0x01,isMuted[ch]?0:chan[ch].pan);
}

void DivPlatformVB::forceIns() {
  for (int i=0; i<6; i++) {
    chan[i].insChanged=true;
    chan[i].freqChanged=true;
    updateWave(i);
    if (romMode) {
      chWrite(i,0x06,chan[i].wave);
    }
    chWrite(i,0x01,isMuted[i]?0:chan[i].pan);
  }
  if (chan[5].active) {
    writeEnv(5,true);
  }
}

void* DivPlatformVB::getChanState(int ch) {
  return &chan[ch];
}

DivMacroInt* DivPlatformVB::getChanMacroInt(int ch) {
  return &chan[ch].std;
}

unsigned short DivPlatformVB::getPan(int ch) {
  return ((chan[ch].pan&0xf0)<<4)|(chan[ch].pan&15);
}

DivChannelModeHints DivPlatformVB::getModeHints(int ch) {
  DivChannelModeHints ret;
  //if (ch>4) return ret;
  ret.count=1;
  ret.hint[0]=ICON_FA_EXCLAMATION_TRIANGLE;
  ret.type[0]=chan[ch].hasEnvWarning;
  
  return ret;
}

DivDispatchOscBuffer* DivPlatformVB::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformVB::getRegisterPool() {
  return regPool;
}

int DivPlatformVB::getRegisterPoolSize() {
  return 0x180;
}

int DivPlatformVB::getRegisterPoolDepth() {
  return 8;
}

void DivPlatformVB::reset() {
  while (!writes.empty()) writes.pop();
  memset(regPool,0,0x600);
  for (int i=0; i<6; i++) {
    chan[i]=DivPlatformVB::Channel();
    chan[i].std.setEngine(parent);
    chan[i].ws.setEngine(parent);
    chan[i].ws.init(NULL,32,63,false);
  }
  if (dumpWrites) {
    addWrite(0xffffffff,0);
  }
  vb->Power();
  tempL=0;
  tempR=0;
  curChan=-1;
  modulation=0;
  modType=false;
  memset(modTable,0,32);
  updateROMWaves();
  // set per-channel initial values
  for (int i=0; i<6; i++) {
    chWrite(i,0x01,isMuted[i]?0:chan[i].pan);
    chWrite(i,0x05,0x00);
    chan[i].intWritten=true;
    chWrite(i,0x00,0x80);
    if (romMode) {
      chWrite(i,0x06,0);
    } else {
      chWrite(i,0x06,i);
    }
  }
  delay=500;
}

int DivPlatformVB::getOutputCount() {
  return 2;
}

bool DivPlatformVB::keyOffAffectsArp(int ch) {
  return true;
}

bool DivPlatformVB::hasAcquireDirect() {
  return true;
}

float DivPlatformVB::getPostAmp() {
  return 6.0f;
}

void DivPlatformVB::updateROMWaves() {
  if (romMode) {
    // copy wavetables
    for (int i=0; i<5; i++) {
      int data=0;
      DivWavetable* w=parent->getWave(i);

      for (int j=0; j<32; j++) {
        if (w->max<1 || w->len<1) {
          data=0;
        } else {
          data=w->data[j*w->len/32]*63/w->max;
          if (data<0) data=0;
          if (data>63) data=63;
        }
        rWrite((i<<7)+(j<<2),data);
      }
    }
  }
}

void DivPlatformVB::notifyWaveChange(int wave) {
  for (int i=0; i<6; i++) {
    if (chan[i].wave==wave) {
      chan[i].ws.changeWave1(wave);
      updateWave(i);
    }
  }
  updateROMWaves();
}

void DivPlatformVB::notifyInsDeletion(void* ins) {
  for (int i=0; i<6; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformVB::setFlags(const DivConfig& flags) {
  chipClock=5000000.0;
  CHECK_CUSTOM_CLOCK;
  rate=chipClock;
  for (int i=0; i<6; i++) {
    oscBuf[i]->setRate(rate);
  }

  romMode=flags.getBool("romMode",false);
  antiClickEnabled=!flags.getBool("noAntiClick",false);
  screwThis=flags.getBool("screwThis",false);

  if (vb!=NULL) {
    delete vb;
    vb=NULL;
  }
  vb=new VSU;
}

void DivPlatformVB::poke(unsigned int addr, unsigned short val) {
  rWrite(addr,val);
}

void DivPlatformVB::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) rWrite(i.addr,i.val);
}

int DivPlatformVB::init(DivEngine* p, int channels, int sugRate, const DivConfig& flags) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;
  for (int i=0; i<6; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  vb=NULL;
  setFlags(flags);
  reset();
  return 6;
}

void DivPlatformVB::quit() {
  for (int i=0; i<6; i++) {
    delete oscBuf[i];
  }
  if (vb!=NULL) {
    delete vb;
    vb=NULL;
  }
}

DivPlatformVB::~DivPlatformVB() {
}
