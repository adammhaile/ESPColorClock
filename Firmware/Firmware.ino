#include <ESPSerialWiFiManager.h>
#include <FastLED.h>
#include <EEPROM.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include "colortable.h"
#include "config.h"

#define numLEDs 64
#define SPI_DATA 13
#define SPI_CLOCK 14

CRGB leds[numLEDs];

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

time_t getNtpTime();
void sendNTPpacket(IPAddress &address);

ESPSerialWiFiManager esp = ESPSerialWiFiManager(512, sizeof(clock_config_t) + 2);

void fill_hrs(CRGB color){
    fill_solid(leds, 32, color);
}

void fill_min(CRGB color){
    fill_solid(leds + 32, 32, color);
}

void fill_all(CRGB color){
    fill_hrs(color);
    fill_min(color);
}

void show() {
    FastLED.show();
    //When used near WiFi commands, need to give it a chance to apply
    FastLED.delay(1);
}

void main_menu(){
    bool first_run = true;
    static const uint8_t _menu_size = 3;
    static String _menu[_menu_size] = {
        F("Clock Options"),
        F("WiFi Config"),
        F("Quit")
    };

    static int i;
    NL();
    OFL("Color Clock Setup");
    OFL("=================");

    while(true){
        OL(FastLED.getBrightness());
        if(esp.status() != WL_CONNECTED){
            first_run = false;
            fill_all(CRGB::Red); show();
            OFL("WiFi must be configured before continuing...");
            NL();
            esp.run_menu();
        }
        else{
            fill_all(CRGB::Green); show();
            i = _print_menu(_menu, _menu_size, first_run ? 10 : 0);
            if(i == -1) return; //timeout, exit
            first_run = false;
            switch(i){
                case 1:
                    clock_menu();
                    break;
                case 2:
                    esp.run_menu();
                    break;
                case 3:
                    return;
            }
        }
    }
}

void clock_menu(){
    while(true){
        NL();
        OFL(" Clock Config Options");
        OFL("Leave Blank for Default");
        OFL("=======================");
        String ntp_server = _prompt("NTP Server (" + String(clock_config.ntp_server) + ")");
        memcpy(clock_config.ntp_server, ntp_server.c_str(), sizeof(char) * ntp_server.length());
        String tz = _prompt("Timezone (" + String(clock_config.timezone) + ")");
        if(tz != ""){
            clock_config.timezone = tz.toInt();
        }
        String interval = _prompt("Sync Interval, Minutes (" + String(clock_config.sync_interval) + ")");
        if(interval.toInt()) clock_config.sync_interval = interval.toInt();
        String p;
        if(clock_config.mil_time)
            p = "24 Hour Time (y)";
        else
            p = "24 Hour Time (n)";
        String mil = _prompt(p);
        clock_config.mil_time = CHAROPT(mil[0], 'y');
        String bright = _prompt("Display Brightness (" + String(clock_config.brightness) + "%)");
        int b = bright.toInt();
        if(b > 0 && b < 100){
            clock_config.brightness = b;
        }

        b = int((float(clock_config.brightness) / 100.0) * 255);
        FastLED.setBrightness(b);
        write_clock_config();

        fill_all(CRGB::Yellow); show();
        Udp.begin(localPort);
        time_t t = getNtpTime();
        if(t == 0){
            OFL("Unable to get time from NTP server, please try another.");
        }
        else{
            break;
        }
    }

    EEPROM.commit();
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(0);
    Serial.setTimeout(1000);

    FastLED.addLeds<APA102, SPI_DATA, SPI_CLOCK, BGR>(leds, numLEDs);
    FastLED.setBrightness(12); //Start out low, before reading the config
    FastLED.clear();
    FastLED.show();

    fill_all(CRGB::Blue); show();

    esp.begin();

    read_clock_config();

    main_menu();

    FastLED.setBrightness(int((float(clock_config.brightness) / 100.0) * 255));

    fill_all(CRGB::Yellow); show();
    Udp.begin(localPort);
    setSyncProvider(getNtpTime);
    setSyncInterval(clock_config.sync_interval*60);
}

void printDigits(int digits)
{
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

void show_time(){
    Serial.print(hour());
    printDigits(minute());
    printDigits(second());
    Serial.print(" ");
    Serial.print(month());
    Serial.print(".");
    Serial.print(day());
    Serial.print(".");
    Serial.print(year());
    Serial.println();

    fill_hrs(color24h[hour()]);
    fill_min(colorMinSec[minute()]);
    show();
}

time_t prevDisplay = 0; // when the digital clock was displayed

void loop()
{
    if (timeStatus() != timeNotSet) {
        if (now() != prevDisplay) { //update the display only if time has changed
            prevDisplay = now();
            show_time();
        }
    }
    else{
        fill_all(CRGB::Red); show();
    }
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(clock_config.ntp_server, ntpServerIP);
  Serial.print(clock_config.ntp_server);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      time_t result = secsSince1900 - 2208988800UL + clock_config.timezone * SECS_PER_HOUR;
      Serial.print("New Time: ");
      Serial.println(result);
      return result;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
