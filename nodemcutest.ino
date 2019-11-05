#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <time.h>

extern "C" {
  #include <user_interface.h>
}

#define DATA_LENGTH           112

#define TYPE_MANAGEMENT       0x00
#define TYPE_CONTROL          0x01
#define TYPE_DATA             0x02
#define SUBTYPE_PROBE_REQUEST 0x04
int timezone = 7 * 3600;                    //ตั้งค่า TimeZone ตามเวลาประเทศไทย
int dst = 0;                                //กำหนดค่า Date Swing Time
int count = 1;


struct RxControl {
 signed rssi:8; // signal intensity of packet
 unsigned rate:4;
 unsigned is_group:1;
 unsigned:1;
 unsigned sig_mode:2; // 0:is 11n packet; 1:is not 11n packet;
 unsigned legacy_length:12; // if not 11n packet, shows length of packet.
 unsigned damatch0:1;
 unsigned damatch1:1;
 unsigned bssidmatch0:1;
 unsigned bssidmatch1:1;
 unsigned MCS:7; // if is 11n packet, shows the modulation and code used (range from 0 to 76)
 unsigned CWB:1; // if is 11n packet, shows if is HT40 packet or not
 unsigned HT_length:16;// if is 11n packet, shows length of packet.
 unsigned Smoothing:1;
 unsigned Not_Sounding:1;
 unsigned:1;
 unsigned Aggregation:1;
 unsigned STBC:2;
 unsigned FEC_CODING:1; // if is 11n packet, shows if is LDPC packet or not.
 unsigned SGI:1;
 unsigned rxend_state:8;
 unsigned ampdu_cnt:8;
 unsigned channel:4; //which channel this packet in.
 unsigned:12;
};

struct SnifferPacket{
    struct RxControl rx_ctrl;
    uint8_t data[DATA_LENGTH];
    uint16_t cnt;
    uint16_t len;
};

// Declare each custom function (excluding built-in, such as setup and loop) before it will be called.
// https://docs.platformio.org/en/latest/faq.html#convert-arduino-file-to-c-manually
static void showMetadata(SnifferPacket *snifferPacket);
static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length);
static void printDataSpan(uint16_t start, uint16_t size, uint8_t* data);
static void getMAC(char *addr, uint8_t* data, uint16_t offset);
void channelHop();

static void showMetadata(SnifferPacket *snifferPacket) {

  unsigned int frameControl = ((unsigned int)snifferPacket->data[1] << 8) + snifferPacket->data[0];

  uint8_t version      = (frameControl & 0b0000000000000011) >> 0;
  uint8_t frameType    = (frameControl & 0b0000000000001100) >> 2;
  uint8_t frameSubType = (frameControl & 0b0000000011110000) >> 4;
  uint8_t toDS         = (frameControl & 0b0000000100000000) >> 8;
  uint8_t fromDS       = (frameControl & 0b0000001000000000) >> 9;

  // Only look for probe request packets
  if (frameType != TYPE_MANAGEMENT ||
      frameSubType != SUBTYPE_PROBE_REQUEST)
        return;


  Serial.print("RSSI: ");
  Serial.print(snifferPacket->rx_ctrl.rssi, DEC);

  Serial.print(" Ch: ");
  Serial.print(wifi_get_channel());

  char addr[] = "00:00:00:00:00:00";
  getMAC(addr, snifferPacket->data, 10);
  Serial.print(" Peer MAC: ");
  Serial.print(addr);

  uint8_t SSID_length = snifferPacket->data[25];
  Serial.print(" SSID: ");
  printDataSpan(26, SSID_length, snifferPacket->data);
//  Serial.print(" Time : ");

  
  Serial.println("");
//  if (count >= 5){
//    
//    count = 1;
//  }else if(count < 5 && frameType != TYPE_MANAGEMENT ){
//    count = 1 ;
//    return;
//  }
//  else if(count < 5  && frameSubType != SUBTYPE_PROBE_REQUEST){
//    count = 1 ;
//    return;
//  }else{
//    count = count + 1;
//  }
}

/**
 * Callback for promiscuous mode
 */
static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length) {
  struct SnifferPacket *snifferPacket = (struct SnifferPacket*) buffer;
  showMetadata(snifferPacket);
}

static void printDataSpan(uint16_t start, uint16_t size, uint8_t* data) {
  for(uint16_t i = start; i < DATA_LENGTH && i < start+size; i++) {
    Serial.write(data[i]);
  }
}

static void getMAC(char *addr, uint8_t* data, uint16_t offset) {
  sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", data[offset+0], data[offset+1], data[offset+2], data[offset+3], data[offset+4], data[offset+5]);
}

#define CHANNEL_HOP_INTERVAL_MS   2000
static os_timer_t channelHop_timer;

/**
 * Callback for channel hoping
 */
void channelHop()
{
  // hoping channels 1-13
    uint8 new_channel = wifi_get_channel() + 5;
//  if (new_channel > 11 ) {
    new_channel = 6;
  //}
//  Serial.print(" Start ch ");
//  Serial.println();
  wifi_set_channel(new_channel);
  
}
void get_time(){
    configTime(timezone, dst, "pool.ntp.org", "time.nist.gov");     //ดึงเวลาจาก Server
//    Serial.println("\nWaiting for time");
    while (!time(nullptr)) {
//      Serial.print(".");
      delay(1000);
    }
    configTime(timezone, dst, "pool.ntp.org", "time.nist.gov");    //ดีงเวลาปัจจุบันจาก Server อีกครั้ง
    time_t now = time(nullptr);
    Serial.print(ctime(&now));

}

#define DISABLE 0
#define ENABLE  1

void promiscuous_mode()
{
  wifi_set_opmode(STATION_MODE);
  wifi_set_channel(1);
  wifi_promiscuous_enable(DISABLE);
  delay(10);
  wifi_set_promiscuous_rx_cb(sniffer_callback);
  delay(10);
  wifi_promiscuous_enable(ENABLE);

  // setup the channel hoping callback timer
  os_timer_disarm(&channelHop_timer);
  os_timer_setfn(&channelHop_timer, (os_timer_func_t *) channelHop, NULL);
  os_timer_arm(&channelHop_timer, CHANNEL_HOP_INTERVAL_MS, 1);
//  wifi_promiscuous_enable(DISABLE);
//  Serial.println("DISABLE");
}

void sta_mode()
{
  wifi_promiscuous_enable(0);
  const char* ssid = "322";                  //ใส่ชื่อ SSID Wifi
  const char* password = "054223952";          //ใส่รหัสผ่าน
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
//  Serial.println("");
  while (WiFi.status() != WL_CONNECTED) {
//  Serial.print(".");
  delay(100);
  }
  
  Serial.println("");
  Serial.print("Time : ");
  get_time();
  Serial.println("Send HERE !!");
  Serial.println("---------------------------------------------------------------------------");
  WiFi.disconnect();
  
}


void setup() {
  Serial.begin(115200);
  const char* ssid = "322";                  //ใส่ชื่อ SSID Wifi
  const char* password = "054223952";          //ใส่รหัสผ่าน
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
  delay(100);
  configTime(timezone, dst, "pool.ntp.org", "time.nist.gov");     //ดึงเวลาจาก Server
  while (!time(nullptr)) {
      delay(1000);
    }
  configTime(timezone, dst, "pool.ntp.org", "time.nist.gov");    //ดีงเวลาปัจจุบันจาก Server อีกครั้ง
  time_t now = time(nullptr);
  delay(10);
  }
}

void loop() {
  promiscuous_mode();     // set the WiFi chip to "promiscuous" mode aka monitor mode
  delay(8000);
  sta_mode();             // set esp8266 to sta mode for connect to the internet
//  Serial.println("STOP !!");
  delay(500);
}
