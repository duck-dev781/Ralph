#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SD_MMC.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include "characters.h"

static constexpr uint8_t LCD_ADDR=0x27, MPU_ADDR=0x68;
static constexpr int BRAKE_LED_PIN=25;
static constexpr uint32_t SLEEP_AFTER_MS=30UL*60UL*1000UL;
static constexpr float WAKE_DELTA=0.12f, HARD_SHAKE=1.65f, UPSIDE=0.72f;

LiquidCrystal_I2C lcd(LCD_ADDR,16,2);
BLECharacteristic* tx=nullptr;
bool sdOK=false, mpuOK=false, bleConnected=false, brake=true;
float ax=0,ay=0,az=0,lastMag=1,shake=0,tempF=0;
uint32_t lastMove=0,lastFrame=0,lastTemp=0,stateUntil=0;
uint8_t frame=0; int8_t houseJump=0;

enum State { SETUP_MODE, AWAKE, TALKING, DIZZY, FALLEN, SLEEPING, WAKING };
State state=SETUP_MODE;

struct Settings {
  String name="Ralph", owner="", house="Tiny House", personality="friendly", key="";
  bool complete=false;
} cfg;

void line(uint8_t r,String s){if(s.length()>16)s=s.substring(0,16);while(s.length()<16)s+=' ';lcd.setCursor(0,r);lcd.print(s);}
void screen(String a,String b){line(0,a);line(1,b);}
void brake(bool on){brake=on;if(BRAKE_LED_PIN>=0)digitalWrite(BRAKE_LED_PIN,on);}
String readFile(const char*p){if(!sdOK||!SD_MMC.exists(p))return "";File f=SD_MMC.open(p);if(!f)return "";String s;while(f.available())s+=(char)f.read();f.close();return s;}
void writeFile(const char*p,const String&s){if(!sdOK)return;File f=SD_MMC.open(p,FILE_WRITE);if(f){f.print(s);f.close();}}
void appendFile(const char*p,const String&s){if(!sdOK)return;File f=SD_MMC.open(p,FILE_APPEND);if(f){f.println(s);f.close();}}
String setting(String k){String d=readFile("/RALPH/SETTINGS.TXT");int p=0;while(p<d.length()){int e=d.indexOf('
',p);if(e<0)e=d.length();String l=d.substring(p,e);l.trim();int q=l.indexOf('=');if(q>0&&l.substring(0,q)==k)return l.substring(q+1);p=e+1;}return "";}
void save(){if(!sdOK)return;String s="name="+cfg.name+"\nowner="+cfg.owner+"\nhouse="+cfg.house+"\npersonality="+cfg.personality+"\ncomplete="+String(cfg.complete?1:0)+"\nkey="+cfg.key+"\n";writeFile("/RALPH/SETTINGS.TXT",s);}
void load(){if(!sdOK||!SD_MMC.exists("/RALPH/SETTINGS.TXT"))return;String v;v=setting("name");if(v.length())cfg.name=v;cfg.owner=setting("owner");v=setting("house");if(v.length())cfg.house=v;v=setting("personality");if(v.length())cfg.personality=v;cfg.complete=setting("complete")=="1";cfg.key=setting("key");}
String makeKey(){const char a[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";String k;uint32_t x=esp_random();for(int i=0;i<12;i++){x=x*1664525UL+1013904223UL;k+=a[x%(sizeof(a)-1)];}return k;}

void mw(uint8_t r,uint8_t v){Wire.beginTransmission(MPU_ADDR);Wire.write(r);Wire.write(v);Wire.endTransmission();}
bool mraw(int16_t&x,int16_t&y,int16_t&z){Wire.beginTransmission(MPU_ADDR);Wire.write(0x3B);if(Wire.endTransmission(false))return false;if(Wire.requestFrom(MPU_ADDR,(uint8_t)6)!=6)return false;x=(Wire.read()<<8)|Wire.read();y=(Wire.read()<<8)|Wire.read();z=(Wire.read()<<8)|Wire.read();return true;}
bool initMPU(){Wire.begin();Wire.beginTransmission(MPU_ADDR);Wire.write(0x75);if(Wire.endTransmission(false))return false;if(Wire.requestFrom(MPU_ADDR,(uint8_t)1)!=1)return false;uint8_t w=Wire.read();if(w!=0x68&&w!=0x70)return false;mw(0x6B,0);mw(0x1C,0);return true;}
void mpu(){if(!mpuOK)return;int16_t x,y,z;if(!mraw(x,y,z))return;ax=x/16384.f;ay=y/16384.f;az=z/16384.f;float mag=sqrtf(ax*ax+ay*ay+az*az),d=fabsf(mag-lastMag);lastMag=mag;shake*=.88f;if(d>.75f)shake+=d;if(d>WAKE_DELTA||fabsf(mag-1)>.1f){lastMove=millis();if(state==SLEEPING){state=WAKING;stateUntil=millis()+1800;brake(true);}}if(state!=SLEEPING&&state!=SETUP_MODE){bool upside=az<-UPSIDE||ay<-UPSIDE||ax<-UPSIDE;if(upside&&mag>.75&&mag<1.25){state=FALLEN;stateUntil=millis()+3000;}else if(shake>HARD_SHAKE){state=DIZZY;stateUntil=millis()+5000;}}}
void temp(){if(millis()-lastTemp<2000)return;lastTemp=millis();float c=temperatureRead();tempF=c*9/5+32;}

String lower(String s){s.toLowerCase();return s;}
String chat(String q){String p=lower(q),a;if(p.indexOf("temp")>=0)a="My chip is "+String(tempF,1)+"F.";else if(p.indexOf("hello")>=0||p=="hi"||p.indexOf("hey")>=0){const char*r[]={"Hi! I'm Ralph!","Hey! Ralph here.","Oh! You came back!","HELLOOO!"};a=r[esp_random()%4];}else if(p.indexOf("who are you")>=0||p.indexOf("your name")>=0)a="I'm Ralph. This is my house!";else if(p.indexOf("house")>=0||p.indexOf("home")>=0)a="My house is "+cfg.house+".";else if(p.indexOf("joke")>=0){const char*r[]={"Why did the ESP32 nap? Too many interrupts.","My house has tiny windows!","I have a byte-sized sense of humor."};a=r[esp_random()%3];}else if(p.indexOf("sleep")>=0){state=SLEEPING;brake(false);a="Okay... sleepy time...";}else if(p.indexOf("dizzy")>=0)a="Please stop spinning me around!";else {const char*r[]={"Hmm... tell me more.","My tiny brain is thinking...","I don't know that one yet.","Interesting!","I heard you!"};a=r[esp_random()%5];}appendFile("/RALPH/CHATS.TXT","YOU: "+q);appendFile("/RALPH/CHATS.TXT","RALPH: "+a);state=TALKING;stateUntil=millis()+2500;return a;}

void reply(String s){if(!tx)return;tx->setValue(s.c_str());tx->notify();}
bool setupKey(String s){return cfg.key.length()&&s==cfg.key;}
void command(String c){c.trim();if(!cfg.complete){if(c=="KEY"){reply("SETUP KEY: "+cfg.key);return;}if(c.startsWith("SETUP ")){if(setupKey(c.substring(6))){cfg.complete=true;save();state=AWAKE;brake(true);reply("SETUP OK - Ralph ready.");screen("SETUP COMPLETE","Hi! I'm Ralph");}else reply("BAD KEY");}else reply("SETUP REQUIRED: SETUP <key>");return;}
if(c.startsWith("NAME ")){cfg.name=c.substring(5);cfg.name.trim();save();reply("NAME SAVED");return;}
if(c.startsWith("OWNER ")){cfg.owner=c.substring(6);cfg.owner.trim();save();reply("OWNER SAVED");return;}
if(c.startsWith("HOUSE ")){cfg.house=c.substring(6);cfg.house.trim();save();reply("HOUSE SAVED");return;}
if(c.startsWith("PERSONALITY ")){cfg.personality=c.substring(12);cfg.personality.trim();save();reply("PERSONALITY SAVED");return;}
if(c=="SLEEP"){state=SLEEPING;brake(false);reply("Goodnight.");return;}if(c=="WAKE"){state=WAKING;stateUntil=millis()+1800;brake(true);reply("I'm awake!");return;}
if(c=="STATUS"){reply("tempF="+String(tempF,1)+" mpu="+String(mpuOK?"ok":"missing")+" sd="+String(sdOK?"ok":"missing")+" state="+String((int)state));return;}
if(c=="RESETSETUP"){cfg.complete=false;save();state=SETUP_MODE;brake(false);reply("SETUP RESET");return;}
if(c.startsWith("CHAT "))c=c.substring(5);reply(chat(c));}

class SC:public BLEServerCallbacks{void onConnect(BLEServer*){bleConnected=true;}void onDisconnect(BLEServer*s){bleConnected=false;s->getAdvertising()->start();}};
class RC:public BLECharacteristicCallbacks{void onWrite(BLECharacteristic*c){std::string v=c->getValue();if(v.length())command(String(v.c_str()));}};

void ble(){BLEDevice::init("Ralph-ESP32");BLEServer*s=BLEDevice::createServer();s->setCallbacks(new SC());BLEService*sv=s->createService("7f6c0001-9f42-4e9b-8d11-72616c706800");auto*rx=sv->createCharacteristic("7f6c0002-9f42-4e9b-8d11-72616c706800",BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR);rx->setCallbacks(new RC());tx=sv->createCharacteristic("7f6c0003-9f42-4e9b-8d11-72616c706800",BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_NOTIFY);tx->addDescriptor(new BLE2902());sv->start();s->getAdvertising()->start();}

void anim(){if(!cfg.complete||state==SETUP_MODE){if(millis()-lastFrame>500){lastFrame=millis();screen("Ralph setup",cfg.key);}return;}if(state==SLEEPING){if(millis()-lastFrame>1000){lastFrame=millis();screen("      zZz","    sleep...");}return;}if(millis()-lastFrame<300)return;lastFrame=millis();frame++;lcd.clear();if(state==DIZZY){screen("@_@  DIZZY!"," HOUSE JUMP!");}else if(state==FALLEN){screen("Ralph fell!","    o__");}else if(state==WAKING){screen("Huh...?", "I'm awake!");}else{if(shake>.75&&millis()-lastMove<1000)houseJump=(frame%3)-1;else houseJump=0;int x=9+houseJump;if(x<0)x=0;if(x>14)x=14;lcd.setCursor(1,0);lcd.write(frame%4);lcd.setCursor(2,0);lcd.write((frame+1)%4);lcd.setCursor(5,0);lcd.print(String(tempF,0)+"F");lcd.setCursor(x,1);lcd.write(4);lcd.setCursor(x+1,1);lcd.write(5);}if((state==DIZZY||state==FALLEN||state==WAKING||state==TALKING)&&millis()>=stateUntil){state=AWAKE;brake(true);}}

void setup(){Serial.begin(115200);pinMode(BRAKE_LED_PIN,OUTPUT);brake(false);lcd.init();lcd.backlight();for(int i=0;i<8;i++)lcd.createChar(i,(uint8_t*)RALPH_CHARS[i]);screen("RALPH","booting...");sdOK=SD_MMC.begin("/sdcard",true);if(sdOK){SD_MMC.mkdir("/RALPH");SD_MMC.mkdir("/RALPH/AI");load();}if(!cfg.key.length()){cfg.key=makeKey();save();}mpuOK=initMPU();lastMove=millis();ble();temp();if(!cfg.complete){state=SETUP_MODE;brake(false);screen("SETUP KEY",cfg.key);}else{state=AWAKE;brake(true);screen("Hi! I'm Ralph",cfg.house);}}
void loop(){mpu();temp();if(cfg.complete&&state!=SLEEPING&&millis()-lastMove>=SLEEP_AFTER_MS){state=SLEEPING;brake(false);}if(state==FALLEN&&az>.65&&fabs(ax)<.65&&fabs(ay)<.65){state=WAKING;stateUntil=millis()+1800;brake(true);}if(millis()-lastMove>1000)shake*=.92f;anim();delay(5);}
