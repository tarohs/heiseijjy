///
// heiseijjy - fake SW JJY (Japan standarad time radio broadcast) station
//             of Heisei era - only sound.
//
// (c) 2021 by taroh (sasaki.taroh@gmail.com)
//
// 2021. 11.  8-11: ver. 0.1->1.0: time tick + DFPlayer morse code/voice
// 2021. 11. 13-14: ver. 1.1: added RTC DS3231, PLL (v1-v2) to adjust internal clock
// 2021. 11. 15   : ver. 1.2: added OLED SSD1331 (color)
// 2022.  1. 18   : ver. 1.3: added OLED ST7735 (color)
// 2022.  1. 19   : ver. 1.4: added OLED SSD1306 (I2C, b/w)
//
//...................................................................
// hardware config
#define DEVDFP  // DFPlayer mini: https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299
#define DEVRTC  // RTC DS3231:
#define DEVOLED // OLED Adafruit GFX lib: 
#undef SSD1331 
#undef ST7735
#define SSD1306

#include <Wire.h>
#define PIN_DAC   (25)
#if defined(DEVDFP)
# define PIN_DFPTX (27)
# define PIN_DFPRX (32)
# include <DFRobotDFPlayerMini.h>
#endif
#if defined(DEVRTC)
# define PIN_RTCSDA (21)
# define PIN_RTCSCL (22)
# include <RTClib.h>
#endif
#if defined(DEVOLED)
# define PIN_OLEDSCL (18) // fixed assign?
# define PIN_OLEDSDA (23) // fixed assign?
# define PIN_OLEDDC  (16)
# define PIN_OLEDCS  (17)
# define PIN_OLEDRES (19)
# include <Adafruit_GFX.h>
# if defined(SSD1331)
#  include <SPI.h>
#  include <Adafruit_SSD1331.h>
#  define SCREEN_WIDTH (96)
#  define SCREEN_HEIGHT (64)
# endif
# if defined(ST7735)
#  include <SPI.h>
#  include <Adafruit_ST7735.h>
#  define SCREEN_WIDTH (160)
#  define SCREEN_HEIGHT (80)
# endif
# if defined(SSD1331) | defined(ST7735)
#  define COL_BLACK (0x0000)
#  define COL_BLUE  (0x001f)
#  define COL_GREEN (0x07e0)
#  define COL_RED   (0xf800)
#  define COL_WHITE (0xffff)
#  define COL_ERASE COL_BLACK
#  define COL_TIME  COL_WHITE
#  define COL_DATE  COL_BLUE
#  define COL_DUT COL_RED
#  define COL_ION COL_GREEN
# endif
# if defined(SSD1306)
#  include <Adafruit_SSD1306.h>
#  define I2C_LCD (0x3C)  // SSD1306
#  define SCREEN_WIDTH (128)
#  define SCREEN_HEIGHT (64)
#  define COL_ERASE SSD1306_BLACK
#  define COL_TIME  SSD1306_WHITE
#  define COL_DATE  SSD1306_WHITE
#  define COL_DUT SSD1306_WHITE
#  define COL_ION SSD1306_WHITE
# endif
#endif

#include <WiFi.h>
#include <BluetoothSerial.h>
#define DEVICENAME "heiseiJJY"
#define SBUFLEN (64)
#define TZ (9 * 60 * 60)  // JST-9JST

//...................................................................
#define SUBSEC  (5)           // 5ms frame to decide action
#define NSUBSEC (1000 / (SUBSEC))
#define SAMPLERATE   (16000)   // must be times of NSUBSEC
#define TM0CYCLE  (80000000 / (SAMPLERATE))  // within uint16_t
#define TM0RES    (1)
//#define TM0RES (100)
#define SAMPLEDIV ((SAMPLERATE) / (NSUBSEC))

//...................................................................
// globals
hw_timer_t *tm0 = NULL;
volatile SemaphoreHandle_t  timerSemaphore;
portMUX_TYPE  timerMux = portMUX_INITIALIZER_UNLOCKED;
#if defined(DEVDFP) 
HardwareSerial DFPSerial(1);
//SoftwareSerial DFPSerial(PIN_DFPRX, PIN_DFPTX);
DFRobotDFPlayerMini DFPlayer;
#endif
#if defined(DEVRTC)
RTC_DS3231 extrtc;
#endif
#if defined(DEVOLED)
# if defined(SSD1331)
#  pragma message "Using SWSPI"
Adafruit_SSD1331 display = Adafruit_SSD1331(PIN_OLEDCS, PIN_OLEDDC, PIN_OLEDSDA, PIN_OLEDSCL, PIN_OLEDRES);
//#pragma message "Using HWSPI"
//SPIClass tftSPI(VSPI);
//Adafruit_SSD1331 display = Adafruit_SSD1331(&tftSPI, PIN_OLEDCS, PIN_OLEDDC, PIN_OLEDRES);
#  define YOFFSET (0)
# endif
# if defined(ST7735)
#  pragma message "Using SWSPI"
Adafruit_ST7735 display = Adafruit_ST7735(PIN_OLEDCS, PIN_OLEDDC, PIN_OLEDSDA, PIN_OLEDSCL, PIN_OLEDRES);
#  define YOFFSET (24)
# endif
# if defined(SSD1306)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#  define YOFFSET (0)
# endif
char scurtime[SBUFLEN] = "        ";
#endif
BluetoothSerial SerialBT;

volatile uint32_t ttready = 0;
int samplec = 0,  // sampling counter
    tsubsec = 0,  // 5ms frame # in a sec (0..200)
    tsec = 0, tmin = 0, thour = 9,  // initial values for date/time
    tday = 15, tmon = 11, tyear = 21, // tyear: lower 2 digits of 20xx
    tdow, tdoy,   // day of the week (0..6), day of the year (1..365/366)
    dut1 = -1;     // DUT1 == UT1 - UTC, -8..0..8 for -0.8sec..0sec..+0.8sec
char ionostat = 'N';  // should be {'N', 'U', 'W'}
#define PLLMAX  (4)     // don't set too big value.
                        // 4 means 1 second shrinks/extends for 0.980-1.020sec.
#define PLLADJ  (193)   // any number in >0, but should be no sound term and
                        // before PLLMAX from next second (PLLMAX + PLLADJ < NSUBSEC, 4 + 185 < 200).
                        // when 185, plladj() is called at 185 subsec (.925 sec).
                        // if PLLADJ exceeds (NSUBSEC - PLLMAX), the PLLADJ moment never comes
                        // because the subsec counter resetted before PLLADJ subsec.
int plloff = 0;         // +1..PLLMAX if RTCsec < tsec (adv), -1..-PLLMAX if RTCsec > tsec (delay)
#define DFPVOL_MAX (30)
#define DFPVOL  (30)
int dfpvol = DFPVOL; //  DFPlayer volume
#define TIMETICKVOL_MAX (3) 
#define TIMETICKVOL (3) // timetick volume
int timetickvol = TIMETICKVOL;
uint8_t outval = 0x80;
#define JMODE_NORMAL  (0)
#define JMODE_BUSY    (1)
int jjymode = JMODE_NORMAL;

// sample sine waves: created by "makesinwave.py"
typedef struct {
  int len;
  const PROGMEM uint8_t data[];
} wave_t;
wave_t wave1000 = {16,
{
  0x80, 0xb1, 0xda, 0xf5, 0xff, 0xf5, 0xda, 0xb1, 
  0x80, 0x4f, 0x26, 0x0b, 0x01, 0x0b, 0x26, 0x4f
}};
wave_t wave1600 = {10,
{
  0x80, 0xcb, 0xf9, 0xf9, 0xcb, 0x80, 0x35, 0x07, 
  0x07, 0x35
}};
wave_t wave600 = {80, {
  0x80, 0x9e, 0xba, 0xd2, 0xe7, 0xf5, 0xfd, 0xff, 
  0xf9, 0xec, 0xda, 0xc2, 0xa7, 0x8a, 0x6c, 0x4f, 
  0x35, 0x1f, 0x0f, 0x05, 0x01, 0x05, 0x0f, 0x1f, 
  0x35, 0x4f, 0x6c, 0x8a, 0xa7, 0xc2, 0xda, 0xec, 
  0xf9, 0xff, 0xfd, 0xf5, 0xe7, 0xd2, 0xba, 0x9e, 
  0x80, 0x62, 0x46, 0x2e, 0x19, 0x0b, 0x03, 0x01, 
  0x07, 0x14, 0x26, 0x3e, 0x59, 0x76, 0x94, 0xb1, 
  0xcb, 0xe1, 0xf1, 0xfb, 0xff, 0xfb, 0xf1, 0xe1, 
  0xcb, 0xb1, 0x94, 0x76, 0x59, 0x3e, 0x26, 0x14, 
  0x07, 0x01, 0x03, 0x0b, 0x19, 0x2e, 0x46, 0x62
}};
wave_t wavebreak = {1, {0}};

// wave sample in SD card in DRPlayer
//   files must be: /MP3/00NN-ANYNAME.wav, where NN is defined as below
//   (H00-H23 as continuous 01-24, M00-M59 as 25-84).
#define NVOICE_H00  (1)
#define NVOICE_M00  (25)
#define NVOICE_jjy  (85)
#define NVOICE_jst  (86)
#define  NCODE_0  (87)
#define NCODE_jjy (97)
#define NCODE_nnnnn (98)
#define NCODE_uuuuu (99)
#define NCODE_wwwww (100)
#define NBREAK_0_1  (101)
#define NBREAK_0_5  (102)

// time track
#define TRST_STOP     (0)
#define TRST_RUN      (1)
int ttstatus = TRST_STOP;
wave_t *ttwave;            // current playing wave pointer
int ttsamplecount;         // counter for ttwave -> data[]
int ttplaylen;             // remaining sample count for current playing wave (infty if -1)

// voice track: DFPlayer
int vtstatus = TRST_STOP;
int vtseq[SBUFLEN];   // > # of voice samples in every x9 min 30 sec 
int vtseqp = 0;

// functions 
void IRAM_ATTR onTimer(void);
void setup(void);
void loop(void);
void incsubsec(void);
void rewritedisp(void);
void updatetimedisp(void);
void plladj(void);
void settt(void);
void playwave(wave_t *wave, int len);
void getwave(void);
void setvt(void);
void startvt(void);
void updatevt(void);
void setdoydow(void);
void setextrtc(void);
void setintrtc(void);
int readrtcsec(void);
int julian(int y, int m, int d);
void incday(void);
int docmd(char *buf);
void printbits60(void);
int a2toi(char *chp);
//void printDetail(uint8_t type, int value);


//...................................................................
// intr handler:
void IRAM_ATTR onTimer(void)
{
  if (! ttready) {  // interrupt is heavy :-)
    return;
  }
  if (timetickvol == 0) {
  } else if (timetickvol == 1) {
    dacWrite(PIN_DAC, (uint8_t)((outval / 4) + 0x60));
  } else if (timetickvol == 2) {
    dacWrite(PIN_DAC, (uint8_t)((outval / 2) + 0x40));
  } else if (timetickvol == 3) {
    dacWrite(PIN_DAC, (uint8_t)(outval));
  } 
  portENTER_CRITICAL_ISR(&timerMux);           // CRITICAL SECTION ---
  ttready--;
  portEXIT_CRITICAL_ISR(&timerMux);            // --- CRITICAL SECTION
  xSemaphoreGiveFromISR(timerSemaphore, NULL); // free semaphore
  return;
}

//...................................................................
void setup(void)
{
  uint8_t macBT[6];

  Serial.begin(115200);
  delay(100);
  Serial.print("started...\n");

// WiFi, NTP setup
  Serial.print("Attempting to connect to Network named: ");
  Serial.println(ssid);                   // print the network name (SSID);
  WiFi.begin(ssid, passwd);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("");
  IPAddress ip = WiFi.localIP();
  Serial.printf("IP Address: ");
  Serial.println(ip);
  Serial.printf("configureing NTP...");
  configTime(TZ, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp"); // enable NTP
  delay(5000);
  Serial.printf("done\n");
#if defined(DEVRTC)
  Wire.setClock(100000);
  Wire.begin(PIN_RTCSDA, PIN_RTCSCL);
  while (! extrtc.begin()) {
    delay(10);
  }
  syncrtc();
  Serial.printf("ext RTC running: %02d/%2d/%2d-%02d:%02d:%02d\n",
                tyear, tmon, tday, thour, tmin, tsec);
#endif

  esp_read_mac(macBT, ESP_MAC_BT);
  Serial.printf(
    "Bluetooth %s %02X:%02X:%02X:%02X:%02X:%02X...",
    DEVICENAME, macBT[0], macBT[1], macBT[2], macBT[3], macBT[4], macBT[5]);
  while (! SerialBT.begin(DEVICENAME)) {
    Serial.println("error initializing Bluetooth");
    delay(2000);
  }
  Serial.print("\n");

#if defined(DEVDFP)
  DFPSerial.begin(9600, SERIAL_8N1, PIN_DFPRX, PIN_DFPTX);
//  DFPSerial.begin(9600);
  while (! DFPlayer.begin(DFPSerial)) {
    Serial.println("DFPlayer: cannot begin (check TF card)");
    delay(50);
  }
  DFPlayer.pause();
  DFPlayer.volume(dfpvol);
  DFPlayer.EQ(DFPLAYER_EQ_NORMAL);
  DFPlayer.disableLoop();
  Serial.println("DFPlayer started.");
#endif
  pinMode(PIN_DAC, OUTPUT);
  dacWrite(PIN_DAC, (uint8_t)outval);

#if defined(DEVOLED)
# if defined(SSD1331)
  display.begin();
# endif
# if defined(ST7735)
  // If your TFT's plastic wrap has a Black Tab, use the following:
  //display.initR(INITR_BLACKTAB);   // initialize a ST7735S chip, black tab
  // If your TFT's plastic wrap has a Red Tab, use the following:
  //display.initR(INITR_REDTAB);   // initialize a ST7735R chip, red tab
  // If your TFT's plastic wrap has a Green Tab, use the following:
  display.initR(INITR_GREENTAB); // initialize a ST7735R chip, green tab
  display.setRotation(3);
# endif
# if defined(SSD1306)
  //  SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  display.begin(SSD1306_SWITCHCAPVCC, I2C_LCD);
  display.clearDisplay();
  display.display();
# endif
  rewritedisp();
#endif

  timerSemaphore = xSemaphoreCreateBinary();    // create semaphore
  tm0 = timerBegin(0, TM0CYCLE, true);          // tm0 prescaler
  timerAttachInterrupt(tm0, &onTimer, true);    // tm0 intr routine: onTimer()
  timerAlarmWrite(tm0, TM0RES, true);          // tm0 resolution
  settt();
  timerAlarmEnable(tm0);                        // tm0 start
  Serial.print("started timer...\n");
}


void loop(void) {
  static char buf[128];
  static int bufp = 0;

  if (! ttready) {
    getwave();
    portENTER_CRITICAL(&timerMux);              // CRITICAL SECTION ---
    ttready++;
    portEXIT_CRITICAL(&timerMux);               // --- CRITICAL SECTION
    samplec = samplec + 1;
    if (SAMPLEDIV <= samplec) {
      samplec = 0;
      incsubsec();
    }
  }
  while (SerialBT.available()) {
    buf[bufp] = SerialBT.read();
    if (buf[bufp] == '\n' || buf[bufp] == '\r' ||
       bufp == sizeof(buf) - 1) {
      buf[bufp] = '\0';
      docmd(buf);
      bufp = 0;
    } else {
      bufp++;
    }
  }
}
//...................................................................


void
incsubsec(void)
{
  tsubsec++;
  if (tsubsec ==  PLLADJ) {
    plladj(); 
  }
  if (NSUBSEC + plloff <= tsubsec) {
    tsubsec = 0;
    tsec = tsec + 1;
    if (60 <= tsec) {
      tsec = 0;
      tmin = tmin + 1;
      if (60 <= tmin) {
        tmin = 0;
        thour = thour + 1;
        if (24 <= thour) {
          thour = 0;
          incday();
        }
      }
    }
#if defined(SSD1331) | defined(ST7735)
    updatetimedisp();
#else
    rewritedisp();
#endif
    Serial.printf("%d/%d/%d %02d:%02d:%02d - \n",        // .%03d
                  tyear, tmon, tday, thour, tmin, tsec); // tsubsec * SUBSEC
  }
  settt();
  if ((jjymode == JMODE_BUSY || (jjymode == JMODE_NORMAL && tmin % 10 == 9)) &&
        tsec == 30 && tsubsec == 6) {  // (x9:)30.030sec
    setvt();
  }
  updatevt();

  return;
}


//...................................................................
void
rewritedisp(void)
{
#if defined(DEVOLED)
  display.fillScreen(COL_ERASE);
  display.setTextSize(1);
  display.setCursor(0, 0 + YOFFSET);
  display.setTextColor(COL_DATE);
  display.printf("%d/%d/%d", tyear, tmon, tday);
  strcpy(scurtime, "        "); 
  updatetimedisp();
  display.setTextSize(1);
  display.setCursor(0, 40 + YOFFSET);
  display.setTextColor(COL_DUT);
  display.printf("dut1=%d ", dut1);
  display.setTextColor(COL_ION);
  display.printf("iono=%c", ionostat);
#if defined(SSD1306)
  display.display();
#endif
#endif
  return;
}


void
updatetimedisp(void)
{
#if defined(DEVOLED)
# if defined(SSD1331) | defined(ST7735)
  char supd[SBUFLEN];
  sprintf(supd, "%02d:%02d:%02d", thour, tmin, tsec);
  display.setTextSize(2);
  for (int x = 0; x < 8; x++) {
    if (scurtime[x] != supd[x]) {
      display.setCursor(x * 12, 20 + YOFFSET);
      display.setTextColor(COL_ERASE);
      display.print(scurtime[x]);
      display.setCursor(x * 12, 20 + YOFFSET);
      display.setTextColor(COL_TIME);
      display.print(supd[x]);
      scurtime[x] = supd[x];
    }
  }
# else
  display.setTextSize(2);
  display.setTextColor(COL_TIME);
  display.setCursor(0, 20 + YOFFSET);
  display.printf("%02d:%02d:%02d", thour, tmin, tsec);
# endif
#endif
  return;
}


//...................................................................
// instant PLL ver. 2:
//   Since I don't know how to change the interrupt cycle divider/counter (TM0CYCLE/TM0RES),
//   this rough way.
//   Adjust tsubsec:tsec (200:1) depending to the advance/delay of tsec to the ext RTC.
//   If + (tsec is advance), plloff is added to the division of tsubsec:tsec (e.g. +3 adv then 203:1)
//   If - (delay) plloff < 0 and division decreased (e.g. -3[sec] delay then 197:1).
//   This comparison is done once a second (to reduce communication to the ext RTC)
//   at the end of second where the time tick sound is silence (reading ext RTC breaks
//   the sound).  The difference (extRTCsec - tsec)[sec] is limited to +/- PLLMAX[subsec],
//   then added to the next subsecond count loop.
//   e.g. difference (tsec - extRTCsec) is 4 sec (advance), 4 is added to subsecond loop
//   (200) and next subsecond loop is 204 (x 5ms = 1.020 second).
//   Finally the difference will be within +/- 1 second theoretically.
//   The adjustment should be limited within 4 (0.02sec), otherwise shorten or extense of
//   a second can be felt.
//   *note: only second parts are compared; this means, tsec even forwards when
//   extRTCsec == 59 & tsec == 00,  but in remaining 59 seconds, tsec delays and no problem
//   (vice versa). -> this is avoided within +/-PLLMAX second adv/delay.

//#define sgn(x) ((x) == (0) ? (0 : ((x) < 0 ? (-1) : (1)))
/*
int
sgn(int x)
{
  if (x == 0) {
    return 0;
  }
  if (x < 0) {
    return -1;
  }
  return 1;
}
 */

void
plladj(void)
{
  static int pllcnt = 0;

  int rtcsec = readrtcsec();
  plloff = tsec - rtcsec;
  if (plloff <= -(60 - PLLMAX)) {
    plloff = 0; // junction of minute
  } else if (plloff < -PLLMAX) {
    plloff = -PLLMAX;
  } else if (60 - PLLMAX <= plloff) {
    plloff = 0; // junction of minute
  } else if (PLLMAX < plloff) {
    plloff = PLLMAX;
  }
  if (plloff != 0) {
    Serial.printf("[PLL]s%d-r%d=%d\n", tsec, rtcsec, plloff);
  }
  
  return;
}


// setup time track: called every subsecond
// JMODE_NORMAL: normal: same as JJY
// JMODE_BUSY:   busy  : 0-9s: beep, 10-19s: click, 20-29s: silent, 30-59s: click + voice
//                       every 09/19/29/59s: 600Hz
void
settt(void)
{
  if (jjymode == JMODE_NORMAL) {
    if (35 <= tmin && tmin < 39) {
      ttstatus = TRST_STOP;
      return;
    }
    if (tsubsec == 0) {           // 0ms
      if (0 < dut1 && (1 <= tsec && tsec <= dut1) ||
          dut1 < 0 && (9 <= tsec && tsec <= 8 - dut1)) {
        playwave(&wave1600, 9);       // 0..45ms play 1600ms at frame 1..8 if dut1 == 1..8,
                                      //                        frame 9..16 if dut1 == -1..-8
//        Serial.printf("1600(.000-.045)\n");
      } else {
        playwave(&wave1600, 1); //  0..5ms play 1600ms
//        Serial.printf("1600(.000-.005)\n");
      }
    } else if (tsubsec == 9) {
      if (tsec == 59) {             // 59 sec, 45ms-700ms
        playwave(&wave600, 131);
//        Serial.printf("600(.045-.700)\n");
      } else if (tmin % 10 < 5) {   // x0-x4 min, 45ms-960ms
        playwave(&wave1000, 183);
//        Serial.printf("1000(.045-.960)\n");
      }
    }
  } else { // jjymode == JMODE_BUSY
    if (20 <= tsec && tsec < 29) {
      ttstatus = TRST_STOP;
      return;
    }
    if (tsubsec == 0) {           // 0ms
      if (0 < dut1 && (1 <= tsec && tsec <= dut1) ||
          dut1 < 0 && (9 <= tsec && tsec <= 8 - dut1)) {
        playwave(&wave1600, 9);       // 0..45ms play 1600ms at frame 1..8 if dut1 == 1..8,
                                      //                        frame 9..16 if dut1 == -1..-8
//        Serial.printf("1600(.000-.045)\n");
      } else {
        playwave(&wave1600, 1); //  0..5ms play 1600ms
//        Serial.printf("1600(.000-.005)\n");
      }
    } else if (tsubsec == 9) {
      if (tsec == 9 || tsec == 19 || tsec == 29 || tsec == 59) { // 09/19/29/59 sec, 45ms-700ms
        playwave(&wave600, 131);
//        Serial.printf("600(.045-.700)\n");
      } else if (0 <= tsec && tsec < 9) {   // 0-9sec, 45ms-960ms
        playwave(&wave1000, 183);
//        Serial.printf("1000(.045-.960)\n");
      }
    }
    
  }
  return;
}


void
playwave(wave_t *wave, int len)
{
  ttwave = wave;
  ttplaylen = len * SAMPLEDIV;
  ttsamplecount = 0;
  ttstatus = TRST_RUN;
  getwave();
  return;
}


// note: "load then increment/decrement" policy.
//       that is, check if end-of-buffer occurs first (in sample str, morse str,
//       morse segment, wave data, playlen), then step ahead the above layer if exceeded,
//       finally load data, increment/decrement.
void
getwave(void)
{
  if (ttstatus == TRST_STOP) {
    outval = 0x80;
    return;
  }
  if (ttwave -> len <= ttsamplecount) { // exceeds end of wave
    ttsamplecount = 0;
  }
  if (ttplaylen <= 0) {
    outval = 0x80;
    ttstatus = TRST_STOP;
    return;
  }
  outval = ttwave -> data[ttsamplecount];
  ttsamplecount++;
  ttplaylen--;
  return;
}


static int dfpplaystart = 0;
//called when voice sequence starts
void
setvt(void)
{
#if defined(DEVDFP)
  int nextm = tmin + 1;
  int nexth = thour;
  if (60 <= nextm) {
    nextm = 0;
    nexth++;
    if (24 <= nexth) {
      nexth = 0;
    }
  }
  vtseq[0] = NCODE_jjy;
  vtseq[1] = NCODE_jjy;
  vtseq[2] = NCODE_0 + (nexth / 10);
  vtseq[3] = NCODE_0 + (nexth % 10);
  vtseq[4] = NCODE_0 + (nextm / 10);
  vtseq[5] = NCODE_0 + (nextm % 10);
  vtseq[6] = NBREAK_0_5;
  vtseq[7] = NVOICE_jjy;
  vtseq[8] = NVOICE_jjy;
  vtseq[9] = NBREAK_0_5;
  vtseq[10] = NVOICE_H00 + nexth;
  vtseq[11] = NVOICE_M00 + nextm;
  vtseq[12] = NVOICE_jst;
  vtseq[13] = NBREAK_0_5;
  if (ionostat == 'U') {
    vtseq[14] = NCODE_uuuuu; 
  } else if (ionostat == 'W') {
    vtseq[14] = NCODE_wwwww; 
  } else { // ionostat == 'N'
    vtseq[14] = NCODE_nnnnn;
  }
  vtseq[15] = -1;
/*  if (! DFPlayer.available()) {
    Serial.printf("DFPlayer not available...\n");
    vtstatus = TRST_STOP;
    return;
  }
  */
  
  vtstatus = TRST_RUN;
  dfpplaystart = 1;
  vtseqp = 0;
#endif
  return;
}

#define AFTERSTARTDISABLE (100)  // 500msec
void
updatevt(void)
{
#if defined(DEVDFP)
  static int dfpafterstartcount = 0;  // disable time to avoid continuous play, subsec unit

  if (0 < dfpafterstartcount) {
    dfpafterstartcount--;
    return;
  }
  dfpafterstartcount--;
  if (vtstatus == TRST_RUN) {
    if (dfpplaystart || (
         DFPlayer.available() &&
         DFPlayer.readType() == DFPlayerPlayFinished &&
         DFPlayer.read() == vtseq[vtseqp - 1]
       )) {
      if (vtseq[vtseqp] < 0) {
        vtstatus = TRST_STOP;
        Serial.printf("finished all voice play.\n");
        return;
      } else {
//        DFPlayer.play(vtseq[vtseqp]);
        DFPlayer.playMp3Folder(vtseq[vtseqp]);
        Serial.printf("started voice %d\n", vtseq[vtseqp]);
        vtseqp++;
        dfpplaystart = 0;
        dfpafterstartcount = AFTERSTARTDISABLE;
      }
    }
  }
#endif
  return;
}


//...................................................................
// calculate doy (day of year)/dow (day of week) from YY/MM/DD
void
setdoydow(void)
{
  int j0 = julian(tyear, 1, 1);      // new year day of this year
  int j1 = julian(tyear, tmon, tday);
  tdoy = j1 - j0 + 1; // 1..365/366
  tdow = j1 % 7;      // 0..6
  return;
}


void
setextrtc(void)
{
#if defined(DEVRTC)
  struct tm *tms;
  time_t now;

  time(&now);
  tms = localtime(&now);
  extrtc.adjust(DateTime(tms -> tm_year + 1900, tms -> tm_mon, tms -> tm_mday,
      tms -> tm_hour, tms -> tm_min, tms -> tm_sec));
  Serial.printf("ext RTC set to %d/%d/%d %02d:%02d:%02d\n",
      tms -> tm_year + 1900, tms -> tm_mon, tms -> tm_mday,
      tms -> tm_hour, tms -> tm_min, tms -> tm_sec);
#endif
}


void
setintrtc(void)
{
#if defined(DEVRTC)
  struct tm tms;
  DateTime extnow = rtc.now();
  tm.tm_year = now.year() - 1900;
  tm.tm_mon = now.month();
  tm.tm_mday = now.day();
  tm.tm_hour = now.hour();
  tm.tm_min = now.minute();
  tm.tm_sec = now.second();
  tm.tm_isdst = -1; // summer time (meaningless)
  time_t intrtc = mktime(&t) + TZ;
  struct timeval tv = {
    .tv_sec = timertc
  };
  settimeofday(&tv, NULL);
  return;
#endif
}


int
readrtcsec(void)
{
#if defined(DEVRTC)
  DateTime now = extrtc.now();
  return now.second();
#elif
  return tsec;
#endif
}


// return julian date (? relative date from a day)
// sunday is multiple of 7
int
julian(int y, int m, int d)
{
  if (m <= 2) {
    m = m + 12;
    y--;
  }
  return y * 1461 / 4 + (m + 1) * 153 / 5 + d + 6;
//  1461 / 4 == 365.25, 153 / 5 == 30.6
}


// increment tday-tmon-tyear, tdoy, tdow
void
incday(void)
{
  int year1 = tyear;   // year of next month
  int mon1 = tmon + 1; // next month
  if (12 < mon1) {
    mon1 = 1;
    year1++;
  }
  int day1 = tday + 1; // date# of tomorrow
  if (julian(year1, mon1, 1) - julian(tyear, tmon, 1) < day1) {
    tday = 1;  // date# exceeds # of date in this month
    tmon = mon1;
    tyear = year1;
  } else {
    tday = day1;
  }
  setdoydow(); // tdoy, tdow is updated from tyear-tmonth-tday
  rewritedisp();
  return;
}

//...................................................................
// Bluetooth command
//
// dYYMMDD: set date to YY/MM/DD
// tHHmmSS: set time to HH:mm:SS
// vNN: set DFPlayer volume to NN (00-29)
// z[0123]: time tick sound off..full
// m[nb]: JJY mode: normal/busy
// u[-]N: set dut1 to N (-8..0..8)
// i[nuw]: set ionostat to NNNNN/UUUUU/WWWWW

int
docmd(char *buf)
{
  int arg1, arg2;
  Serial.printf("cmd: >>%s<<\n", buf);
  if (buf[0] == 'd' || buf[0] == 'D') { // set date
    if (strlen(buf) != 7) {
      return 0;
    }
    int y = a2toi(buf + 1);
    int m = a2toi(buf + 3);
    int d = a2toi(buf + 5);
    Serial.printf("%d %d %d\n", y, m, d);
    if (y < 0 || m < 0 || 12 < m || d < 0 || 31 < d) {  // can set Feb 31 :-)
      return 0;
    }
    tyear = y;
    tmon = m;
    tday = d;
    tdow = julian(tyear, tmon, tday) % 7;
    setextrtc();
    rewritedisp();
    Serial.printf("set date: >>%s<<\n", buf + 1);
    return 1;
  } else if (buf[0] == 't' || buf[0] == 'T') { // set time & start tick
    if (strlen(buf) != 7) {
      return 0;
    }
    int h = a2toi(buf + 1);
    int m = a2toi(buf + 3);
    int s = a2toi(buf + 5);
    if (h < 0 || 24 < h || m < 0 || 60 < m || s < 0 || 60 < s) {
      return 0;
    }
    thour = h;
    tmin = m;
    tsec = s;
    tsubsec = 0;
    samplec = 0;
    settt();
    setextrtc();
    updatetimedisp();
    Serial.printf("set time...restart tick: >>%s<<\n", buf + 1);
    return 1;
  } else if (buf[0] == 'v' || buf[0] == 'V') { // DFPlayer volume
    int v = a2toi(buf + 1);
    if (0 <= v && v <= DFPVOL_MAX) {
      dfpvol = v;
      DFPlayer.volume(dfpvol);
      Serial.printf("DFP volume: >>%d<<\n", dfpvol);
      return 1;
    }
  } else if (buf[0] == 'z' || buf[0] == 'Z') { // time tick sound on(1)/off(0)
    int v = buf[1] - '0';
    if (0 <= v && v <= TIMETICKVOL_MAX) {
      timetickvol = v;
      Serial.printf("timetick volume: >>%d<<\n", timetickvol);
      return 1;
    }
  } else if (buf[0] == 'm' || buf[0] == 'M') {   // mode:
    if (buf[1] == 'n' || buf[1] == 'N') {        //    mn: normal
      jjymode = JMODE_NORMAL;
      Serial.printf("jjymode: normal\n", timetickvol);
      return 1;
    } else if (buf[1] == 'b' || buf[1] == 'B') { //    mb: busy
      jjymode = JMODE_BUSY;
      Serial.printf("jjymode: busy\n", timetickvol);
      return 1;
    }
  } else if (buf[0] == 'u' || buf[0] == 'U') {   // dut1:
    int d;
    if (buf[1] == '-') {
      d = -(buf[2] - '0');
    } else {
      d = buf[1] - '0';
    }
    if (-8 <= d && d <= 8) {
      dut1 = d;
      rewritedisp();
      Serial.printf("dut1: >>%d<<\n", dut1);
      return 1;
    }
  } else if (buf[0] == 'i' || buf[0] == 'I') {   // ionotstat:
    if (buf[1] == 'n' || buf[1] == 'u' || buf[1] == 'w') {
      ionostat = (char)(buf[1] - 'a' + 'A');
      rewritedisp();
      Serial.printf("ionostat: >>%c<<\n", ionostat);
      return 1;
    } else if (buf[1] == 'N' || buf[1] == 'U' || buf[1] == 'W') {
      ionostat = buf[1];
      rewritedisp();
      Serial.printf("ionostat: >>%c<<\n", ionostat);
      return 1;
    }
  }
  Serial.printf("invalid command (ignored)\n");
  return 0;
}

int
a2toi(char *chp)
{
  int v = 0;
  for (int i = 0; i < 2; chp++, i++) {
    if (*chp < '0' || '9' < *chp) {
      return -1;
    }
    v = v * 10 + (*chp - '0');
  }
  return v;
}

/*
// for debug: from DFRobot example
void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}
 */
