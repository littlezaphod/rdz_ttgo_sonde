#include <U8x8lib.h>
#include <U8g2lib.h>

#include "Sonde.h"
#include "RS41.h"
#include "DFM.h"

extern U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8;

//SondeInfo si = { STYPE_RS41, 403.450, "P1234567", true, 48.1234, 14.9876, 543, 3.97, -0.5, true, 120 };
const char *sondeTypeStr[5] = { "DFM6", "DFM9", "RS41" };

static unsigned char kmh_tiles[] U8X8_PROGMEM = {
   0x1F, 0x04, 0x0A, 0x11, 0x00, 0x1F, 0x02, 0x04, 0x42, 0x3F, 0x10, 0x08, 0xFC, 0x22, 0x20, 0xF8
   };
static unsigned char ms_tiles[] U8X8_PROGMEM = {
   0x1F, 0x02, 0x04, 0x02, 0x1F, 0x40, 0x20, 0x10, 0x08, 0x04, 0x12, 0xA4, 0xA4, 0xA4, 0x40, 0x00
   };
static unsigned char stattiles[4][4] =  {
   0x00, 0x00, 0x00, 0x00 ,
   0x00, 0x10, 0x10, 0x00 ,
   0x1F, 0x15, 0x15, 0x00 ,
   0x00, 0x1F, 0x00, 0x00  };

byte myIP_tiles[8*11];

static const uint8_t font[10][5]={
  0x3E, 0x51, 0x49, 0x45, 0x3E,   // 0
  0x00, 0x42, 0x7F, 0x40, 0x00,   // 1
  0x42, 0x61, 0x51, 0x49, 0x46,   // 2
  0x21, 0x41, 0x45, 0x4B, 0x31,   // 3
  0x18, 0x14, 0x12, 0x7F, 0x10,   // 4
  0x27, 0x45, 0x45, 0x45, 0x39,   // 5
  0x3C, 0x4A, 0x49, 0x49, 0x30,   // 6
  0x01, 0x01, 0x79, 0x05, 0x03,   // 7
  0x36, 0x49, 0x49, 0x49, 0x36,   // 8
  0x06, 0x49, 0x39, 0x29, 0x1E }; // 9;  .=0x40

static uint8_t halfdb_tile[8]={0x80, 0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00};

static uint8_t halfdb_tile1[8]={0x00, 0x38, 0x28, 0x28, 0x28, 0xC8, 0x00, 0x00};
static uint8_t halfdb_tile2[8]={0x00, 0x11, 0x02, 0x02, 0x02, 0x01, 0x00, 0x00};

static uint8_t empty_tile[8]={0x80, 0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x00};

static uint8_t empty_tile1[8]={0x00, 0xF0, 0x88, 0x48, 0x28, 0xF0, 0x00, 0x00};
static uint8_t empty_tile2[8]={0x00, 0x11, 0x02, 0x02, 0x02, 0x01, 0x00, 0x00};
static uint8_t ap_tile[8]={0x00,0x04,0x22,0x92, 0x92, 0x22, 0x04, 0x00};

Sonde::Sonde() {
	config.noisefloor = -130;
	strcpy(config.call,"NOCALL");
	strcpy(config.passcode, "---");
	config.udpfeed.active = 1;
	config.udpfeed.type = 0;
	strcpy(config.udpfeed.host, "192.168.42.20");
	config.udpfeed.port = 9002;
	config.udpfeed.highrate = 1;
	config.udpfeed.idformat = ID_DFMGRAW;
	config.tcpfeed.active = 0;
	config.tcpfeed.type = 1;
	strcpy(config.tcpfeed.host, "radiosondy.info");
	config.tcpfeed.port = 12345;
	config.tcpfeed.highrate = 10;
	config.tcpfeed.idformat = ID_DFMDXL;
}

void Sonde::setIP(const char *ip, bool AP) {
  memset(myIP_tiles, 0, 11*8);
  int len = strlen(ip);
  int pix = (len-3)*6+6;
  int tp = 80-pix+8;
  if(AP) memcpy(myIP_tiles+(tp<16?0:8), ap_tile, 8);
  for(int i=0; i<len; i++) {
    if(ip[i]=='.') { myIP_tiles[tp++]=0x40; myIP_tiles[tp++]=0x00; }
    else {
      int idx = ip[i]-'0';
      memcpy(myIP_tiles+tp, &font[idx], 5);
      myIP_tiles[tp+5] = 0;
      tp+=6;
    }
  }
  while(tp<8*10) { myIP_tiles[tp++]=0; }
}

void Sonde::clearSonde() {
	nSonde = 0;
}
void Sonde::addSonde(float frequency, SondeType type, int active)  {
	if(nSonde>=MAXSONDE) {
		Serial.println("Cannot add another sonde, MAXSONDE reached");
		return;
	}
	sondeList[nSonde].type = type;
	sondeList[nSonde].freq = frequency;
	sondeList[nSonde].active = active;
	memcpy(sondeList[nSonde].rxStat, "\x00\x01\x2\x3\x2\x1\x1\x2\x0\x3\x0\x0\x1\x2\x3\x1\x0", 18);
	nSonde++;
}
void Sonde::nextConfig() {
	currentSonde++;
	// Skip non-active entries (but don't loop forever if there are no active ones
	for(int i=0; i<MAXSONDE; i++) {
		if(!sondeList[currentSonde].active) {
			currentSonde++;
			if(currentSonde>=nSonde) currentSonde=0;
		}
	}
	if(currentSonde>=nSonde) {
		currentSonde=0;
	}
	setup();
}
SondeInfo *Sonde::si() {
	return &sondeList[currentSonde];
}

void Sonde::setup() {
	// Test only: setIP("123.456.789.012");
	// update receiver config: TODO
	Serial.print("Setting up receiver on channel ");
	Serial.println(currentSonde);
	switch(sondeList[currentSonde].type) {
	case STYPE_RS41:
		rs41.setup();
		rs41.setFrequency(sondeList[currentSonde].freq * 1000000);
		break;
	case STYPE_DFM06:
	case STYPE_DFM09:
		dfm.setup( sondeList[currentSonde].type==STYPE_DFM06?0:1 );
		dfm.setFrequency(sondeList[currentSonde].freq * 1000000);
		break;
	}
	// Update display
	//updateDisplayRXConfig();
	//updateDisplay();
}
int Sonde::receiveFrame() {
	int ret;
	if(sondeList[currentSonde].type == STYPE_RS41) {
		ret = rs41.receiveFrame();
	} else {
		ret = dfm.receiveFrame();
	}
	memmove(sonde.si()->rxStat+1, sonde.si()->rxStat, 17);
	sonde.si()->rxStat[0] = ret==0 ? 3 : 1;    // OK or Timeout; TODO: add error (2)
	return ret;  // 0: OK, 1: Timeuot, 2: Other error (TODO)
}

void Sonde::updateDisplayPos() {
	char buf[16];
	u8x8.setFont(u8x8_font_7x14_1x2_r);
	if(si()->validPos) {
		snprintf(buf, 16, "%2.5f", si()->lat);
		u8x8.drawString(0,2,buf);
		snprintf(buf, 16, "%2.5f", si()->lon);
		u8x8.drawString(0,4,buf);
	} else {
		u8x8.drawString(0,2,"<??>     ");
		u8x8.drawString(0,4,"<??>     ");
	}
}

void Sonde::updateDisplayPos2() {
	char buf[16];
	u8x8.setFont(u8x8_font_chroma48medium8_r);
	if(!si()->validPos) {
		u8x8.drawString(10,2,"      ");
		u8x8.drawString(10,3,"      ");
		u8x8.drawString(10,4,"      ");
		return;
	}
	snprintf(buf, 16, si()->hei>999?" %5.0fm":" %3.1fm", si()->hei);
	u8x8.drawString((10+6-strlen(buf)),2,buf);
	snprintf(buf, 16, si()->hs>99?" %3.0f":" %2.1f", si()->hs);
	u8x8.drawString((10+4-strlen(buf)),3,buf);
	snprintf(buf, 16, " %+2.1f", si()->vs);
	u8x8.drawString((10+4-strlen(buf)),4,buf);
	u8x8.drawTile(14,3,2,kmh_tiles);
	u8x8.drawTile(14,4,2,ms_tiles);
}

void Sonde::updateDisplayID() {
        u8x8.setFont(u8x8_font_chroma48medium8_r);
	if(si()->validID) {
        	u8x8.drawString(0,1, si()->id);
	} else {
		u8x8.drawString(0,1, "nnnnnnnn        ");
	}
}
	
void Sonde::updateDisplayRSSI() {
	char buf[16];
	u8x8.setFont(u8x8_font_7x14_1x2_r);
	snprintf(buf, 16, "-%d   ", sonde.si()->rssi/2);
	int len=strlen(buf)-3;
	buf[5]=0;
	u8x8.drawString(0,6,buf);
	u8x8.drawTile(len,6,1,(sonde.si()->rssi&1)?halfdb_tile1:empty_tile1);
	u8x8.drawTile(len,7,1,(sonde.si()->rssi&1)?halfdb_tile2:empty_tile2);
}

void Sonde::updateStat() {
	uint8_t *stat = si()->rxStat;
	for(int i=0; i<18; i+=2) {
		uint8_t tile[8];
		*(uint32_t *)(&tile[0]) = *(uint32_t *)(&(stattiles[stat[i]]));
		*(uint32_t *)(&tile[4]) = *(uint32_t *)(&(stattiles[stat[i+1]]));
		u8x8.drawTile(7+i/2, 6, 1, tile);
	}
}

void Sonde::updateDisplayRXConfig() {
	char buf[16];
	u8x8.setFont(u8x8_font_chroma48medium8_r);
	u8x8.drawString(0,0, sondeTypeStr[si()->type]);
	snprintf(buf, 16, "%3.3f MHz", si()->freq);
	u8x8.drawString(5,0, buf);

}

void Sonde::updateDisplayIP() {
        u8x8.drawTile(5, 7, 11, myIP_tiles);
}

// Probing RS41
// 40x.xxx MHz
void Sonde::updateDisplayScanner() {
	char buf[16];
        u8x8.setFont(u8x8_font_7x14_1x2_r);
	u8x8.drawString(0, 0, "Probing");
	u8x8.drawString(8, 0,  sondeTypeStr[si()->type]);
        snprintf(buf, 16, "%3.3f MHz", si()->freq);
        u8x8.drawString(0,3, buf);
	updateDisplayIP();
}

void Sonde::updateDisplay()
{
	char buf[16];
	updateDisplayRXConfig();
	updateDisplayID();
	updateDisplayPos();
	updateDisplayPos2();
	updateDisplayRSSI();
	updateStat();
        updateDisplayIP();
}

void Sonde::clearDisplay() {
	u8x8.clearDisplay();
}

Sonde sonde = Sonde();
