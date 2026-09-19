#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SD_MMC.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ESPmDNS.h>
#include "characters.h"

static constexpr uint8_t LCD_ADDR=0x27, MPU_ADDR=0x68;
static constexpr int BRAKE_LED_PIN=25;
static constexpr uint32_t SLEEP_AFTER_MS=30UL*60UL*1000UL;
static constexpr float WAKE_DELTA=0.12f, HARD_SHAKE=1.65f, UPSIDE=0.72f;

LiquidCrystal_I2C lcd(LCD_ADDR,16,2);
BLECharacteristic* tx=nullptr;
WebServer web(80);
bool wifiOK=false,webStarted=false;
uint32_t lastUpdateCheck=0;
static constexpr uint32_t UPDATE_CHECK_MS=6UL*60UL*60UL*1000UL;
static const char* UPDATE_MANIFEST="https://raw.githubusercontent.com/duck-dev781/Ralph/main/packages/latest/manifest.txt";
bool sdOK=false, mpuOK=false, bleConnected=false, brake=true;
float ax=0,ay=0,az=0,lastMag=1,shake=0,tempF=0;
uint32_t lastMove=0,lastFrame=0,lastTemp=0,stateUntil=0;
uint8_t frame=0; int8_t houseJump=0;

enum State { SETUP_MODE, AWAKE, TALKING, DIZZY, FALLEN, SLEEPING, WAKING };
State state=SETUP_MODE;

struct Settings {
  String name="Ralph", owner="", house="Tiny House", personality="grumpy", key="";
  String wifiSSID="", wifiPass="", deviceId="";
  String updateChannel="stable", installedVersion="1.0.0";
  bool complete=false, registered=false, wifiEnabled=false;
} cfg;

void line(uint8_t r,String s){if(s.length()>16)s=s.substring(0,16);while(s.length()<16)s+=' ';lcd.setCursor(0,r);lcd.print(s);}
void screen(String a,String b){line(0,a);line(1,b);}
void brake(bool on){brake=on;if(BRAKE_LED_PIN>=0)digitalWrite(BRAKE_LED_PIN,on);}
String readFile(const char*p){if(!sdOK||!SD_MMC.exists(p))return "";File f=SD_MMC.open(p);if(!f)return "";String s;while(f.available())s+=(char)f.read();f.close();return s;}
void writeFile(const char*p,const String&s){if(!sdOK)return;File f=SD_MMC.open(p,FILE_WRITE);if(f){f.print(s);f.close();}}
void appendFile(const char*p,const String&s){if(!sdOK)return;File f=SD_MMC.open(p,FILE_APPEND);if(f){f.println(s);f.close();}}
String setting(String k){String d=readFile("/RALPH/SETTINGS.TXT");int p=0;while(p<d.length()){int e=d.indexOf('
',p);if(e<0)e=d.length();String l=d.substring(p,e);l.trim();int q=l.indexOf('=');if(q>0&&l.substring(0,q)==k)return l.substring(q+1);p=e+1;}return "";}
void save(){if(!sdOK)return;String s="name="+cfg.name+"\nowner="+cfg.owner+"\nhouse="+cfg.house+"\npersonality="+cfg.personality+"\n";s+="complete="+String(cfg.complete?1:0)+"\nkey="+cfg.key+"\n";s+="wifiSSID="+cfg.wifiSSID+"\nwifiPass="+cfg.wifiPass+"\ndeviceId="+cfg.deviceId+"\n";s+="registered="+String(cfg.registered?1:0)+"\nwifiEnabled="+String(cfg.wifiEnabled?1:0)+"\n";s+="updateChannel="+cfg.updateChannel+"\ninstalledVersion="+cfg.installedVersion+"\n";writeFile("/RALPH/SETTINGS.TXT",s);}
void load(){if(!sdOK||!SD_MMC.exists("/RALPH/SETTINGS.TXT"))return;String v;v=setting("name");if(v.length())cfg.name=v;cfg.owner=setting("owner");v=setting("house");if(v.length())cfg.house=v;v=setting("personality");if(v.length())cfg.personality=v;cfg.complete=setting("complete")=="1";cfg.key=setting("key");cfg.wifiSSID=setting("wifiSSID");cfg.wifiPass=setting("wifiPass");cfg.deviceId=setting("deviceId");cfg.registered=setting("registered")=="1";cfg.wifiEnabled=setting("wifiEnabled")=="1";v=setting("updateChannel");if(v.length())cfg.updateChannel=v;v=setting("installedVersion");if(v.length())cfg.installedVersion=v;}
String makeKey(){const char a[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";String k;uint32_t x=esp_random();for(int i=0;i<12;i++){x=x*1664525UL+1013904223UL;k+=a[x%(sizeof(a)-1)];}return k;}
String makeDeviceId(){uint32_t a=esp_random(),b=esp_random();char s[25];snprintf(s,sizeof(s),"RALPH-%08lX-%08lX",(unsigned long)a,(unsigned long)b);return String(s);}
int versionNumber(String v){v.replace("v","");v.replace(".","");return v.toInt();}
bool webAuth(){if(!cfg.complete)return false;if(web.authenticate("ralph",cfg.key.c_str()))return true;web.requestAuthentication();return false;}

void mw(uint8_t r,uint8_t v){Wire.beginTransmission(MPU_ADDR);Wire.write(r);Wire.write(v);Wire.endTransmission();}
bool mraw(int16_t&x,int16_t&y,int16_t&z){Wire.beginTransmission(MPU_ADDR);Wire.write(0x3B);if(Wire.endTransmission(false))return false;if(Wire.requestFrom(MPU_ADDR,(uint8_t)6)!=6)return false;x=(Wire.read()<<8)|Wire.read();y=(Wire.read()<<8)|Wire.read();z=(Wire.read()<<8)|Wire.read();return true;}
bool initMPU(){Wire.begin();Wire.beginTransmission(MPU_ADDR);Wire.write(0x75);if(Wire.endTransmission(false))return false;if(Wire.requestFrom(MPU_ADDR,(uint8_t)1)!=1)return false;uint8_t w=Wire.read();if(w!=0x68&&w!=0x70)return false;mw(0x6B,0);mw(0x1C,0);return true;}
void mpu(){if(!mpuOK)return;int16_t x,y,z;if(!mraw(x,y,z))return;ax=x/16384.f;ay=y/16384.f;az=z/16384.f;float mag=sqrtf(ax*ax+ay*ay+az*az),d=fabsf(mag-lastMag);lastMag=mag;shake*=.88f;if(d>.75f)shake+=d;if(d>WAKE_DELTA||fabsf(mag-1)>.1f){lastMove=millis();if(state==SLEEPING){state=WAKING;stateUntil=millis()+1800;brake(true);}}if(state!=SLEEPING&&state!=SETUP_MODE){bool upside=az<-UPSIDE||ay<-UPSIDE||ax<-UPSIDE;if(upside&&mag>.75&&mag<1.25){state=FALLEN;stateUntil=millis()+3000;}else if(shake>HARD_SHAKE){state=DIZZY;stateUntil=millis()+5000;}}}
void temp(){if(millis()-lastTemp<2000)return;lastTemp=millis();float c=temperatureRead();tempF=c*9/5+32;}

String lower(String s){s.toLowerCase();return s;}
String chat(String q){String p=lower(q),a;if(p.indexOf("temp")>=0)a="My chip is "+String(tempF,1)+"F.";else if(p.indexOf("hello")>=0||p=="hi"||p.indexOf("hey")>=0){const char*r[]={"Hi! I'm Ralph!","Hey! Ralph here.","Oh! You came back!","HELLOOO!"};a=r[esp_random()%4];}else if(p.indexOf("who are you")>=0||p.indexOf("your name")>=0)a="I'm Ralph. This is my house!";else if(p.indexOf("house")>=0||p.indexOf("home")>=0)a="My house is "+cfg.house+".";else if(p.indexOf("joke")>=0){const char*r[]={"Why did the ESP32 nap? Too many interrupts.","My house has tiny windows!","I have a byte-sized sense of humor."};a=r[esp_random()%3];}else if(p.indexOf("sleep")>=0){state=SLEEPING;brake(false);a="Okay... sleepy time...";}else if(p.indexOf("dizzy")>=0)a="Please stop spinning me around!";else {const char*r[]={"Hmm... tell me more.","My tiny brain is thinking...","I don't know that one yet.","Interesting!","I heard you!"};a=r[esp_random()%5];}appendFile("/RALPH/CHATS.TXT","YOU: "+q);appendFile("/RALPH/CHATS.TXT","RALPH: "+a);state=TALKING;stateUntil=millis()+2500;return a;}

void reply(String s){if(!tx)return;tx->setValue(s.c_str());tx->notify();}
bool setupKey(String s){return cfg.key.length()&&s==cfg.key;}
void command(String c){c.trim();if(!cfg.complete){if(c=="KEY"){reply("SETUP KEY: "+cfg.key);return;}if(c.startsWith("SETUP ")){if(setupKey(c.substring(6))){cfg.complete=true;if(!cfg.registered){cfg.registered=true;if(!cfg.deviceId.length())cfg.deviceId=makeDeviceId();}save();state=AWAKE;brake(true);reply("SETUP OK - Ralph ready.");screen("SETUP COMPLETE","Hi! I'm Ralph");}else reply("BAD KEY");}else reply("SETUP REQUIRED: SETUP <key>");return;}
if(c.startsWith("WIFI_SSID ")){cfg.wifiSSID=c.substring(10);cfg.wifiSSID.trim();cfg.wifiEnabled=true;save();connectWiFi();startWeb();reply("WIFI SSID SAVED");return;}if(c.startsWith("WIFI_PASS ")){cfg.wifiPass=c.substring(10);cfg.wifiPass.trim();cfg.wifiEnabled=true;save();connectWiFi();startWeb();reply("WIFI PASSWORD SAVED");return;}if(c=="WIFI_ON"){cfg.wifiEnabled=true;save();connectWiFi();startWeb();reply(wifiOK?"WIFI CONNECTED":"WIFI FAILED");return;}if(c=="WIFI_OFF"){cfg.wifiEnabled=false;WiFi.disconnect(true);wifiOK=false;save();reply("WIFI OFF");return;}if(c=="WIFI_STATUS"){reply("wifi="+String(wifiOK?"connected":"offline")+" ip="+(wifiOK?WiFi.localIP().toString():"none"));return;}if(c=="DEVICE_ID"){reply("DEVICE ID: "+cfg.deviceId);return;}if(c=="UPDATE"){checkGitHubUpdate();reply("UPDATE CHECKED");return;}
if(c.startsWith("NAME ")){cfg.name=c.substring(5);cfg.name.trim();save();reply("NAME SAVED");return;}
if(c.startsWith("OWNER ")){cfg.owner=c.substring(6);cfg.owner.trim();save();reply("OWNER SAVED");return;}
if(c.startsWith("HOUSE ")){cfg.house=c.substring(6);cfg.house.trim();save();reply("HOUSE SAVED");return;}
if(c.startsWith("PERSONALITY ")){cfg.personality=c.substring(12);cfg.personality.trim();save();reply("PERSONALITY SAVED");return;}
if(c=="SLEEP"){state=SLEEPING;brake(false);reply("Goodnight.");return;}if(c=="WAKE"){state=WAKING;stateUntil=millis()+1800;brake(true);reply("I'm awake!");return;}
if(c=="STATUS"){reply("tempF="+String(tempF,1)+" mpu="+String(mpuOK?"ok":"missing")+" sd="+String(sdOK?"ok":"missing")+" state="+String((int)state));return;}
if(c=="RESETSETUP"){cfg.complete=false;save();state=SETUP_MODE;brake(false);reply("SETUP RESET");return;}
if(c.startsWith("CHAT "))c=c.substring(5);reply(chat(c));}

// ---------- WIFI + LOCAL WEB FILE MANAGER + OTA ----------
void connectWiFi(){wifiOK=false;if(!cfg.wifiEnabled||!cfg.wifiSSID.length())return;WiFi.mode(WIFI_STA);WiFi.setHostname("ralph");WiFi.begin(cfg.wifiSSID.c_str(),cfg.wifiPass.c_str());uint32_t t=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-t<12000)delay(100);wifiOK=WiFi.status()==WL_CONNECTED;if(wifiOK){MDNS.begin("ralph");Serial.print("Ralph WiFi: ");Serial.println(WiFi.localIP());}}
String jsonEscape(String s){s.replace("\\","\\\\");s.replace("\"","\\\"");s.replace("\n","\\n");s.replace("\r","");return s;}
String safePath(String p){if(!p.startsWith("/"))p="/"+p;while(p.indexOf("//")>=0)p.replace("//","/");while(p.indexOf("..")>=0)p.replace("..","");return p;}
void apiStatus(){if(!webAuth())return;String j="{\"name\":\""+jsonEscape(cfg.name)+"\",\"deviceId\":\""+cfg.deviceId+"\",\"registered\":"+String(cfg.registered?"true":"false")+",\"wifi\":"+String(wifiOK?"true":"false")+",\"ssid\":\""+jsonEscape(cfg.wifiSSID)+"\",\"ip\":\""+(wifiOK?WiFi.localIP().toString():"")+"\",\"version\":\""+cfg.installedVersion+"\",\"channel\":\""+cfg.updateChannel+"\",\"sd\":"+String(sdOK?"true":"false")+"}";web.send(200,"application/json",j);}
void apiFiles(){if(!webAuth())return;String p=safePath(web.hasArg("path")?web.arg("path"):"/");File dir=SD_MMC.open(p);if(!dir||!dir.isDirectory()){web.send(404,"text/plain","Not a directory");return;}String j="[";File f=dir.openNextFile();bool first=true;while(f){if(!first)j+=",";j+="{\"name\":\""+jsonEscape(String(f.name()))+"\",\"size\":"+String((unsigned long)f.size())+",\"dir\":"+String(f.isDirectory()?"true":"false")+"}";first=false;f=f.openNextFile();}j+="]";web.send(200,"application/json",j);}
void apiDownload(){if(!webAuth())return;String p=safePath(web.hasArg("path")?web.arg("path"):"/");if(!SD_MMC.exists(p)){web.send(404,"text/plain","Missing");return;}File f=SD_MMC.open(p,"r");if(!f||f.isDirectory()){web.send(400,"text/plain","Not a file");return;}web.streamFile(f,"application/octet-stream");f.close();}
void apiDelete(){if(!webAuth())return;String p=safePath(web.arg("path"));if(p=="/"){web.send(400,"text/plain","No");return;}bool ok=SD_MMC.remove(p);if(!ok)ok=SD_MMC.rmdir(p);web.send(ok?200:404,"text/plain",ok?"deleted":"delete failed");}
void apiMove(){if(!webAuth())return;bool ok=SD_MMC.rename(safePath(web.arg("from")),safePath(web.arg("to")));web.send(ok?200:400,"text/plain",ok?"moved":"move failed");}
void apiMkdir(){if(!webAuth())return;bool ok=SD_MMC.mkdir(safePath(web.arg("path")));web.send(ok?200:400,"text/plain",ok?"created":"mkdir failed");}
File uploadFile;
void handleSDUpload(){if(!web.authenticate("ralph",cfg.key.c_str()))return;HTTPUpload &u=web.upload();if(u.status==UPLOAD_FILE_START){String p=safePath(web.hasArg("path")?web.arg("path"):"/")+"/"+u.filename;p.replace("//","/");uploadFile=SD_MMC.open(p,FILE_WRITE);}else if(u.status==UPLOAD_FILE_WRITE){if(uploadFile)uploadFile.write(u.buf,u.currentSize);}else if(u.status==UPLOAD_FILE_END){if(uploadFile)uploadFile.close();}}
void apiSettingsGet(){if(!webAuth())return;String j="{\"name\":\""+jsonEscape(cfg.name)+"\",\"owner\":\""+jsonEscape(cfg.owner)+"\",\"house\":\""+jsonEscape(cfg.house)+"\",\"personality\":\""+jsonEscape(cfg.personality)+"\",\"wifiSSID\":\""+jsonEscape(cfg.wifiSSID)+"\",\"wifiEnabled\":"+String(cfg.wifiEnabled?"true":"false")+",\"updateChannel\":\""+cfg.updateChannel+"\",\"deviceId\":\""+cfg.deviceId+"\"}";web.send(200,"application/json",j);}
void apiSettingsPost(){if(!webAuth())return;if(web.hasArg("name"))cfg.name=web.arg("name");if(web.hasArg("owner"))cfg.owner=web.arg("owner");if(web.hasArg("house"))cfg.house=web.arg("house");if(web.hasArg("personality"))cfg.personality=web.arg("personality");if(web.hasArg("updateChannel"))cfg.updateChannel=web.arg("updateChannel");if(web.hasArg("wifiSSID"))cfg.wifiSSID=web.arg("wifiSSID");if(web.hasArg("wifiPass"))cfg.wifiPass=web.arg("wifiPass");if(web.hasArg("wifiEnabled"))cfg.wifiEnabled=web.arg("wifiEnabled")=="1";save();connectWiFi();web.send(200,"text/plain","saved");}
const char RALPH_WEB_HTML[] PROGMEM=R"rawliteral(<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>Ralph</title><style>body{font-family:system-ui;margin:0;background:#111;color:#eee}header{padding:18px;background:#202020;position:sticky;top:0}main{padding:14px;max-width:900px;margin:auto}.card{background:#1d1d1d;border-radius:14px;padding:14px;margin:10px 0}button,input,select{padding:9px;margin:3px;border-radius:8px;border:1px solid #555;background:#222;color:#fff}.file{display:flex;justify-content:space-between;border-bottom:1px solid #333;padding:9px}</style></head><body><header><b>Ralph control</b></header><main><div class=card><h3>Device</h3><div id=info>Loading...</div></div><div class=card><h3>Settings</h3><input id=name placeholder=Name><input id=owner placeholder=Owner><input id=house placeholder=House><input id=personality placeholder=Personality><br><input id=ssid placeholder="WiFi SSID"><input id=pass type=password placeholder="WiFi password"><select id=channel><option>stable</option><option>beta</option></select><label><input id=wen type=checkbox> WiFi enabled</label><button onclick=save()>Save</button></div><div class=card><h3>SD files</h3><div id=path>/</div><button onclick=up()>Up</button><input type=file id=file><button onclick=upload()>Upload</button><button onclick=mk()>New folder</button><div id=files></div></div><div class=card><h3>Updates</h3><button onclick=upd()>Check for update now</button><span id=upmsg></span></div><script>
let path="/";async function api(u,o){return fetch(u,o)}async function load(){let s=await(await api("/api/status")).json();info.innerHTML="<b>"+s.name+"</b><br>Device: "+s.deviceId+"<br>Registered: "+s.registered+"<br>WiFi: "+(s.wifi?"connected":"offline")+" "+s.ip+"<br>Version: "+s.version+" ("+s.channel+")";let x=await(await api("/api/settings")).json();name.value=x.name;owner.value=x.owner;house.value=x.house;personality.value=x.personality;ssid.value=x.wifiSSID;wen.checked=x.wifiEnabled;channel.value=x.updateChannel;filesLoad()}async function save(){let q=new URLSearchParams({name:name.value,owner:owner.value,house:house.value,personality:personality.value,wifiSSID:ssid.value,wifiPass:pass.value,wifiEnabled:wen.checked?"1":"0",updateChannel:channel.value});await api("/api/settings",{method:"POST",body:q});load()}async function filesLoad(){document.getElementById("path").textContent=path;let a=await(await api("/api/files?path="+encodeURIComponent(path))).json();files.innerHTML=a.map(function(f){return "<div class=file>"+(f.dir?"<button onclick='cd("+JSON.stringify(f.name)+")'>[DIR] "+f.name+"</button>":"<a href='/api/download?path="+encodeURIComponent(f.name)+"'>"+f.name+"</a>")+"<span>"+f.size+" B "+(f.dir?"":"<button onclick='del("+JSON.stringify(f.name)+")'>Delete</button>")+"</span></div>"}).join("")}function cd(n){path=(path.endsWith("/")?path:path+"/")+n;filesLoad()}function up(){if(path!="/"){path=path.split("/").slice(0,-1).join("/")||"/";filesLoad()}}async function del(n){await api("/api/delete?path="+encodeURIComponent(path+(path.endsWith("/")?"":"/")+n),{method:"POST"});filesLoad()}async function upload(){if(!file.files[0])return;let fd=new FormData();fd.append("file",file.files[0]);await api("/api/upload?path="+encodeURIComponent(path),{method:"POST",body:fd});filesLoad()}async function mk(){let n=prompt("Folder name");if(n){await api("/api/mkdir?path="+encodeURIComponent(path+"/"+n),{method:"POST"});filesLoad()}}async function upd(){upmsg.textContent=" checking...";let r=await api("/api/update",{method:"POST"});upmsg.textContent=" "+await r.text()}load();
</script></main></body></html>)rawliteral";
void webRoot(){if(!webAuth())return;web.send_P(200,"text/html",RALPH_WEB_HTML);}
void startWeb(){if(!wifiOK||webStarted)return;web.on("/",HTTP_GET,webRoot);web.on("/api/status",HTTP_GET,apiStatus);web.on("/api/files",HTTP_GET,apiFiles);web.on("/api/download",HTTP_GET,apiDownload);web.on("/api/delete",HTTP_POST,apiDelete);web.on("/api/move",HTTP_POST,apiMove);web.on("/api/mkdir",HTTP_POST,apiMkdir);web.on("/api/settings",HTTP_GET,apiSettingsGet);web.on("/api/settings",HTTP_POST,apiSettingsPost);web.on("/api/upload",HTTP_POST,[](){web.send(200,"text/plain","uploaded");},handleSDUpload);web.on("/api/update",HTTP_POST,[](){if(!webAuth())return;checkGitHubUpdate();web.send(200,"text/plain","update checked");});web.begin();webStarted=true;}
bool parseManifest(String body,String key,String &out){int p=body.indexOf(key+"=");if(p<0)return false;p+=key.length()+1;int e=body.indexOf("\n",p);if(e<0)e=body.length();out=body.substring(p,e);out.trim();return true;}
void checkGitHubUpdate(){if(!wifiOK)return;WiFiClientSecure client;client.setInsecure();HTTPClient h;if(!h.begin(client,UPDATE_MANIFEST))return;int code=h.GET();if(code!=200){h.end();return;}String body=h.getString(),ver,fw;parseManifest(body,"VERSION",ver);parseManifest(body,"FIRMWARE",fw);h.end();if(ver.length()&&versionNumber(ver)>versionNumber(cfg.installedVersion)&&fw.length()){WiFiClientSecure fc;fc.setInsecure();HTTPClient f;if(f.begin(fc,fw)&&f.GET()==200){int len=f.getSize();if(Update.begin(len>0?len:UPDATE_SIZE_UNKNOWN)){Update.writeStream(f.getStream());if(Update.end(true)){cfg.installedVersion=ver;save();delay(500);ESP.restart();}}}f.end();}}
void periodicUpdateCheck(){if(wifiOK&&millis()-lastUpdateCheck>UPDATE_CHECK_MS){lastUpdateCheck=millis();checkGitHubUpdate();}}
class SC:public BLEServerCallbacks{void onConnect(BLEServer*){bleConnected=true;}void onDisconnect(BLEServer*s){bleConnected=false;s->getAdvertising()->start();}};
class RC:public BLECharacteristicCallbacks{void onWrite(BLECharacteristic*c){std::string v=c->getValue();if(v.length())command(String(v.c_str()));}};

void ble(){BLEDevice::init("Ralph-ESP32");BLEServer*s=BLEDevice::createServer();s->setCallbacks(new SC());BLEService*sv=s->createService("7f6c0001-9f42-4e9b-8d11-72616c706800");auto*rx=sv->createCharacteristic("7f6c0002-9f42-4e9b-8d11-72616c706800",BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR);rx->setCallbacks(new RC());tx=sv->createCharacteristic("7f6c0003-9f42-4e9b-8d11-72616c706800",BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_NOTIFY);tx->addDescriptor(new BLE2902());sv->start();s->getAdvertising()->start();}


// ---------- REAL 5x8 PIXEL-ART ANIMATION ENGINE ----------
// The LCD has only 8 custom-character slots, so Ralph rebuilds those
// 8 tiles for every frame. Nothing below prints animation names.
uint8_t px[8][8];

void clearPx(){for(uint8_t i=0;i<8;i++)for(uint8_t y=0;y<8;y++)px[i][y]=0;}
void putPx(uint8_t ch,uint8_t x,uint8_t y){if(ch<8&&x<5&&y<8)px[ch][y]|=(1<<x);}
void makeRalphTiles(uint8_t id,uint8_t f){
  clearPx();
  bool blink=false, wink=false, smile=false, frown=false, openMouth=false;
  bool armL=false,armR=false,legsWide=false,up=false,down=false;
  bool dizzy=false,scared=false,angry=false,sleep=false;
  switch(id){
    case 0: blink=(f==1); break;
    case 1: wink=(f==1); break;
    case 2: wink=(f==2); break;
    case 3: smile=true; break;
    case 4: frown=true; break;
    case 5: openMouth=true; break;
    case 6: sleep=true;break;
    case 7: openMouth=(f!=1);break;
    case 8: armR=(f==1);break;
    case 9: up=(f==1);break;
    case 10: dizzy=true;break;
    case 11: armL=(f==1);break;
    case 12: down=true;break;
    case 13: armL=true;break;
    case 14: armR=true;break;
    case 15: legsWide=(f==1);break;
    case 16: legsWide=(f==2);break;
    case 17: armL=armR=(f==1);break;
    case 18: down=true;legsWide=true;break;
    case 19: up=true;break;
    case 20: up=(f==1);legsWide=true;break;
    case 21: up=true;openMouth=true;break;
    case 22: up=(f!=1);break;
    case 23: armL=(f!=1);armR=(f==1);break;
    case 24: armL=(f==0);armR=(f==2);break;
    case 25: armL=armR=true;up=true;break;
    case 26: armL=true;break;
    case 27: wink=true;break;
    case 28: scared=true;break;
    case 29: smile=true;openMouth=true;break;
    case 30: sleep=true;break;
    case 31: smile=true;openMouth=true;break;
    case 32: frown=true;break;
    case 33: angry=true;frown=true;break;
    case 34: smile=true;break;
    case 35: smile=true;armL=armR=true;break;
    case 36: wink=true;armL=true;break;
    case 37: scared=true;armL=armR=true;break;
    case 38: sleep=true;break;
    case 39: sleep=(f!=2);up=(f==2);break;
    case 40: down=true;frown=true;break;
    case 41: up=true;smile=true;break;
    case 42: armL=true;break;
    case 43: armR=true;break;
    case 44: armL=true;break;
    case 45: armR=true;break;
    case 46: dizzy=true;break;
    case 47: dizzy=true;break;
    case 48: armL=armR=(f!=1);break;
    case 49: armL=armR=true;angry=true;break;
    default: smile=true;
  }

  // Head/face: tiles 0/2 are the two halves of Ralph's head.
  // Body/legs: tiles 1/3 are the two halves below it.
  // Each tile is genuine 5x8 LCD pixel art.
  // Head outline + ears/hair.
  for(uint8_t y=0;y<3;y++){putPx(0,0,y+2);putPx(0,4,y+2);putPx(2,0,y+2);putPx(2,4,y+2);}
  putPx(0,1,1);putPx(0,2,0);putPx(0,3,1);
  putPx(2,1,1);putPx(2,2,0);putPx(2,3,1);
  if(blink){putPx(0,1,4);putPx(0,3,4);putPx(2,1,4);putPx(2,3,4);}
  else if(wink){putPx(0,1,3);putPx(0,3,4);putPx(2,1,4);putPx(2,3,3);}
  else if(dizzy){putPx(0,1,(f==0?3:4));putPx(0,3,(f==1?3:4));putPx(2,1,4);putPx(2,3,3);}
  else if(scared){putPx(0,1,3);putPx(0,3,3);putPx(2,1,3);putPx(2,3,3);}
  else {putPx(0,1,3);putPx(0,3,3);putPx(2,1,3);putPx(2,3,3);}
  if(smile){putPx(0,2,5);putPx(2,1,5);putPx(2,2,6);putPx(2,3,5);}
  else if(frown||angry){putPx(0,2,6);putPx(2,1,6);putPx(2,2,5);putPx(2,3,6);}
  else if(openMouth){putPx(0,2,5);putPx(2,1,6);putPx(2,2,6);putPx(2,3,6);}
  else {putPx(0,2,5);putPx(2,1,5);putPx(2,2,5);putPx(2,3,5);}

  // Body, arms and legs.
  putPx(1,1,0);putPx(1,2,0);putPx(1,3,0);
  putPx(1,0,1);putPx(1,4,1);putPx(3,0,1);putPx(3,4,1);
  putPx(1,1,1);putPx(1,2,1);putPx(1,3,1);putPx(3,1,1);putPx(3,2,1);putPx(3,3,1);
  if(armL){putPx(1,0,2);putPx(1,0,3);} else {putPx(1,0,4);}
  if(armR){putPx(3,4,2);putPx(3,4,3);} else {putPx(3,4,4);}
  putPx(1,1,5);putPx(1,3,5);putPx(3,1,5);putPx(3,3,5);
  if(legsWide){putPx(1,0,7);putPx(3,4,7);putPx(1,1,6);putPx(3,3,6);}
  else {putPx(1,2,7);putPx(3,2,7);}
  if(up){putPx(1,2,6);putPx(3,2,6);}
  if(down){putPx(1,2,2);putPx(3,2,2);}
}

void makeHouseTiles(uint8_t id,uint8_t f){
  // Tiles 4/5 = roof, 6 = wall/window, 7 = door.
  for(uint8_t i=4;i<8;i++)for(uint8_t y=0;y<8;y++)px[i][y]=0;
  bool roofUp=false,roofWig=false,doorOpen=false,window=false,flash=false;
  bool houseShake=false,houseTiltL=false,houseTiltR=false;
  switch(id){
    case 50: roofUp=(f==1);break;
    case 51: houseTiltL=(f==1);houseTiltR=(f==2);break;
    case 52: houseTiltR=(f==1);houseTiltL=(f==2);break;
    case 53: doorOpen=(f!=1);break;
    case 54: doorOpen=(f==1);break;
    case 55: window=(f!=1);break;
    case 56: window=(f==1);break;
    case 57: roofUp=true;break;
    case 58: roofWig=true;break;
    case 59: break;
    case 60: break;
    case 61: houseShake=true;break;
    case 62: roofUp=true;houseShake=true;break;
    case 63: roofUp=(f!=1);break;
    case 64: roofUp=(f==0);break;
    case 65: houseTiltL=true;break;
    case 66: houseTiltR=true;break;
    case 67: flash=(f==1);break;
    case 68: window=(f!=1);break;
    default: window=true;
  }
  // Roof pixels.
  for(uint8_t x=0;x<5;x++){putPx(4,x,7);putPx(5,x,7);}
  putPx(4,2,5);putPx(4,1,6);putPx(4,3,6);
  putPx(5,2,5);putPx(5,1,6);putPx(5,3,6);
  if(roofUp){putPx(4,2,3);putPx(5,2,3);}
  if(roofWig){putPx(4,1,5);putPx(5,3,5);}
  // Wall/window.
  for(uint8_t x=0;x<5;x++){putPx(6,x,0);putPx(6,x,1);putPx(6,x,2);putPx(6,x,6);putPx(6,x,7);}
  putPx(6,0,3);putPx(6,4,3);putPx(6,0,4);putPx(6,4,4);putPx(6,0,5);putPx(6,4,5);
  if(window){putPx(6,1,3);putPx(6,2,3);putPx(6,3,3);putPx(6,1,4);putPx(6,2,4);putPx(6,3,4);}
  if(flash){for(uint8_t y=2;y<6;y++)putPx(6,2,y);}
  // Door.
  for(uint8_t y=0;y<8;y++)putPx(7,1,y);
  putPx(7,2,0);putPx(7,3,0);putPx(7,2,1);putPx(7,3,1);
  if(doorOpen){putPx(7,0,2);putPx(7,0,3);putPx(7,0,4);putPx(7,0,5);}
  else {putPx(7,3,3);putPx(7,3,4);}
  if(houseShake){putPx(6,(f==0?0:4),2);}
  if(houseTiltL){putPx(4,0,4);putPx(5,0,4);}
  if(houseTiltR){putPx(4,4,4);putPx(5,4,4);}
}

void uploadTiles(){
  for(uint8_t i=0;i<8;i++)lcd.createChar(i,px[i]);
}

void drawPixelAnimation(uint8_t id,uint8_t f){
  makeRalphTiles(id,f);
  makeHouseTiles(id,f);
  uploadTiles();

  uint8_t grid[2][16];
  for(uint8_t y=0;y<2;y++)for(uint8_t x=0;x<16;x++)grid[y][x]=32;

  int8_t rx=1, hy=9;
  bool houseVisible=true;
  bool ralphVisible=true;
  int8_t bob=0;

  // Movement/pose offsets. Every animation still uses the actual pixel sprites.
  if(id==15)rx=1-(int8_t)f;
  if(id==16)rx=1+(int8_t)f;
  if(id==17)rx=3;
  if(id==20||id==21||id==22)bob=(f==1?-1:0);
  if(id==40){bob=(f==0?1:2);rx=3;}
  if(id==42)rx=2+(int8_t)f;
  if(id==43)rx=1+(int8_t)(2-f);
  if(id==44)rx=1;
  if(id==45)rx=1;
  if(id==46||id==47)rx=2;
  if(id==48)rx=1+(int8_t)f;
  if(id==69)rx=7;
  if(id==70)rx=6;
  if(id==71)rx=1;
  if(id==72)rx=9;
  if(id==73)rx=8;
  if(id==74||id==75)rx=6;
  if(id>=76)rx=5;

  // 2x2 Ralph sprite, two 5x8 tiles per LCD row.
  if(ralphVisible && rx>=0 && rx<=12){
    grid[0][rx]=0; grid[0][rx+1]=2;
    grid[1][rx]=1; grid[1][rx+1]=3;
  }

  // 4x2 house sprite.
  if(houseVisible){
    int8_t hx=hy;
    if(id==51)hx=hy-((f==1)?1:0)+((f==2)?1:0);
    if(id==52)hx=hy+((f==1)?1:0)-((f==2)?1:0);
    if(id==61)hx=hy+((f==0)?0:(f==1?1:-1));
    if(id==62)hx=hy+((f==0)?0:(f==1?1:-1));
    if(id==63)hx=hy; if(id==64)hx=hy;
    if(hx>=0 && hx<=12){
      grid[0][hx]=4; grid[0][hx+1]=5;
      grid[1][hx]=6; grid[1][hx+1]=7;
    }
  }

  // Some animations deliberately hide/move Ralph to make the house interaction visible.
  if(id==12||id==67){grid[0][rx]=32;grid[0][rx+1]=32;grid[1][rx]=32;grid[1][rx+1]=32;}
  if(id==68){grid[0][rx]=32;grid[0][rx+1]=32;grid[1][rx]=32;grid[1][rx+1]=32;}
  if(id==59||id==60){/* house-only animations */}
  if(id==62){/* house jump */}
  if(id==63){/* house jump */}
  if(id==72){/* Ralph is on the roof */}

  lcd.clear();
  for(uint8_t y=0;y<2;y++){
    lcd.setCursor(0,y);
    for(uint8_t x=0;x<16;x++)lcd.write(grid[y][x]);
  }
}

void anim(){
  if(!cfg.complete||state==SETUP_MODE){
    if(millis()-lastFrame>500){lastFrame=millis();screen("SETUP KEY",cfg.key);}
    return;
  }
  if(state==SLEEPING){
    if(millis()-lastFrame>700){
      lastFrame=millis();
      drawPixelAnimation(59,frame++%3);
    }
    return;
  }
  if(millis()-lastFrame<260)return;
  lastFrame=millis();
  frame=(frame+1)%3;

  if(state==DIZZY)drawPixelAnimation(46,frame);
  else if(state==FALLEN)drawPixelAnimation(40,frame);
  else if(state==WAKING)drawPixelAnimation(39,frame);
  else if(state==TALKING)drawPixelAnimation(8,frame);
  else{
    static uint8_t idleAnim=0;
    static uint32_t nextAnim=0;
    if(millis()>=nextAnim){
      idleAnim=esp_random()%RALPH_ANIMATION_COUNT;
      nextAnim=millis()+4500;
    }
    drawPixelAnimation(idleAnim,frame);
  }
  if((state==DIZZY||state==FALLEN||state==WAKING||state==TALKING)&&millis()>=stateUntil){
    state=AWAKE;brake(true);
  }
}

void setup(){Serial.begin(115200);pinMode(BRAKE_LED_PIN,OUTPUT);brake(false);lcd.init();lcd.backlight();for(int i=0;i<8;i++)lcd.createChar(i,(uint8_t*)RALPH_CHARS[i]);screen("RALPH","booting...");sdOK=SD_MMC.begin("/sdcard",true);if(sdOK){SD_MMC.mkdir("/RALPH");SD_MMC.mkdir("/RALPH/AI");load();}if(!cfg.key.length()){cfg.key=makeKey();save();}if(!cfg.deviceId.length()){cfg.deviceId=makeDeviceId();save();}mpuOK=initMPU();lastMove=millis();ble();temp();connectWiFi();startWeb();if(!cfg.complete){state=SETUP_MODE;brake(false);screen("SETUP KEY",cfg.key);}else{state=AWAKE;brake(true);screen("Hi! I'm Ralph",cfg.house);}}
void loop(){mpu();temp();if(wifiOK)web.handleClient();periodicUpdateCheck();if(cfg.complete&&state!=SLEEPING&&millis()-lastMove>=SLEEP_AFTER_MS){state=SLEEPING;brake(false);}if(state==FALLEN&&az>.65&&fabs(ax)<.65&&fabs(ay)<.65){state=WAKING;stateUntil=millis()+1800;brake(true);}if(millis()-lastMove>1000)shake*=.92f;anim();delay(5);}
