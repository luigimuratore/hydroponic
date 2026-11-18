#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <math.h>
#include <stdlib.h>
#include "dashboard.h"

// Credenziali locali: copia config.example.h in config.h e compilalo.
#include "config.h"
// Attivare SOLO alla prima inizializzazione: cancella lo storico.
const bool FORMAT_STORICO = false;

// Verificare la logica del proprio modulo con la pompa scollegata.
const uint8_t PUMP_PIN = D1; // GPIO5
const uint8_t PUMP_ON = HIGH;
const uint8_t PUMP_OFF = LOW;
ESP8266WebServer server(80);
struct Config { uint32_t magic; uint16_t onMin; uint16_t everyHours; uint8_t automatic; uint8_t reserved; uint16_t checksum; } cfg;
const uint32_t MAGIC=0x49445232;
bool pump=false, mdnsStarted=false, waitingToStart=false;
unsigned long phaseStart=0, bootStart=0;
const unsigned long BOOT_OFF_MS=5000;
uint16_t checksum(const Config &c){return (uint16_t)(0x4382 ^ c.onMin ^ (c.everyHours<<1) ^ c.automatic ^ (c.magic & 0xffff) ^ (c.magic>>16));}
void setPump(bool on){pump=on;digitalWrite(PUMP_PIN,on?PUMP_ON:PUMP_OFF);}
void saveConfig(){cfg.magic=MAGIC;cfg.reserved=0;cfg.checksum=checksum(cfg);EEPROM.put(0,cfg);EEPROM.commit();}
void loadConfig(){EEPROM.begin(128);EEPROM.get(0,cfg);if(cfg.magic!=MAGIC||cfg.onMin<1||cfg.onMin>1440||cfg.everyHours<1||cfg.everyHours>168||cfg.onMin>=cfg.everyHours*60UL||cfg.automatic>1||cfg.checksum!=checksum(cfg)){cfg={MAGIC,15,4,0,0,0};saveConfig();}}
bool authorized(){if(server.authenticate(WEB_USER,WEB_PASSWORD))return true;server.requestAuthentication();return false;}
void headers(){server.sendHeader("Cache-Control","no-store");server.sendHeader("X-Content-Type-Options","nosniff");}
void status(){if(!authorized())return;unsigned long ms=millis(), remaining=0;if(cfg.automatic){if(waitingToStart && (unsigned long)(ms-bootStart)<BOOT_OFF_MS)remaining=(BOOT_OFF_MS-(unsigned long)(ms-bootStart)+999)/1000;else {unsigned long duration=(unsigned long)(pump?cfg.onMin:(cfg.everyHours*60UL-cfg.onMin))*60000UL;unsigned long passed=ms-phaseStart;remaining=passed>=duration?0:(duration-passed+999)/1000;}}String j="{\"mode\":\""+String(cfg.automatic?"auto":"manual")+"\",\"pump\":"+String(pump?"true":"false")+",\"on\":"+String(cfg.onMin)+",\"every\":"+String(cfg.everyHours)+",\"remaining\":"+String(remaining)+"}";headers();server.send(200,"application/json",j);}
bool parseMinutes(const String &v,uint16_t &out){if(v.length()==0||v.length()>4)return false;for(size_t i=0;i<v.length();i++)if(v[i]<'0'||v[i]>'9')return false;long n=v.toInt();if(n<1||n>1440)return false;out=(uint16_t)n;return true;}
bool fsReady=false;
String measurementPath(const String &type) {
  if (type=="level") return "/level.csv";
  if (type=="ph") return "/ph.csv";
  if (type=="ec") return "/ec.csv";
  if (type=="temp") return "/temp.csv";
  return "";
}
bool readMeasurement(const String &line, unsigned long &stamp, double &value) {
  int comma=line.indexOf(',');
  if(comma<=0 || comma>=int(line.length()-1)) return false;
  String t=line.substring(0,comma), v=line.substring(comma+1);
  if(t.length()!=10) return false;
  for(unsigned i=0;i<t.length();++i) if(t[i]<'0'||t[i]>'9') return false;
  char *end=nullptr; value=strtod(v.c_str(),&end);
  if(end==v.c_str() || *end!='\0' || !isfinite(value)) return false;
  stamp=strtoul(t.c_str(),nullptr,10);
  return stamp>=1577836800UL && stamp<=4102444799UL;
}
bool inRange(const String &type,double value){
  if(type=="level")return value>=0&&value<=100;
  if(type=="ph")return value>=0&&value<=14;
  if(type=="ec")return value>=0&&value<=50;
  if(type=="temp")return value>=-20&&value<=80;
  return false;
}
void getRecords(){
  if(!authorized())return;
  String path=measurementPath(server.arg("type"));
  if(path==""){server.send(400,"text/plain","Tipo non valido");return;}
  if(!fsReady){server.send(503,"text/plain","Memoria non disponibile");return;}
  headers();
  if(!LittleFS.exists(path)){server.send(200,"application/json","[]");return;}
  File f=LittleFS.open(path,"r");
  if(!f){server.send(500,"text/plain","Impossibile leggere lo storico");return;}
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"application/json","");
  server.sendContent("["); bool first=true; unsigned index=0;
  while(f.available()){
    String line=f.readStringUntil('\n'); line.trim();
    unsigned row=index++; unsigned long t; double v;
    if(!readMeasurement(line,t,v))continue;
    String item=String(first?"":",")+"{\"t\":"+String(t)+",\"v\":"+String(v,3)+",\"i\":"+String(row)+"}";
    server.sendContent(item);first=false;
    yield();
  }
  f.close();server.sendContent("]");server.sendContent("");
}
void addRecord(){
  if(!authorized())return;
  String type=server.arg("type"), path=measurementPath(type);
  if(path==""){server.send(400,"text/plain","Tipo non valido");return;}
  if(!fsReady){server.send(503,"text/plain","Memoria non disponibile");return;}
  String ts=server.arg("ts"), value=server.arg("value");
  if(value.length()>24 || ts.length()!=10){server.send(400,"text/plain","Dato non valido");return;}
  unsigned long stamp;double val;
  if(!readMeasurement(ts+","+value,stamp,val)||!inRange(type,val)){server.send(400,"text/plain","Dato non valido");return;}
  if(LittleFS.exists(path)){
    File r=LittleFS.open(path,"r"); if(!r){server.send(500,"text/plain","Lettura fallita");return;}
    unsigned count=0;while(r.available()){if(r.read()=='\n')count++; if(count>=500)break;yield();}r.close();
    if(count>=500){server.send(507,"text/plain","Limite di 500 misure per tipo raggiunto");return;}
  }
  File f=LittleFS.open(path,"a");if(!f){server.send(500,"text/plain","Scrittura fallita");return;}
  String line=String(stamp)+","+String(val,3)+"\n";
  size_t written=f.print(line);f.flush();f.close();
  if(written!=line.length()){server.send(500,"text/plain","Scrittura incompleta");return;}
  server.send(200,"text/plain","OK");
}


// Recover an interrupted replacement before opening the history.
bool recoverMeasurement(const String &path){
  String bak=path+".bak", tmp=path+".tmp";
  if(LittleFS.exists(bak)){
    if(!LittleFS.exists(path)){if(!LittleFS.rename(bak,path))return false;}
    else if(!LittleFS.remove(bak))return false;
  }
  if(LittleFS.exists(tmp)&&!LittleFS.remove(tmp))return false;
  return true;
}
void deleteRecord(){
  if(!authorized())return;
  String type=server.arg("type"),path=measurementPath(type),idx=server.arg("index"),ts=server.arg("ts"),value=server.arg("value");
  if(path==""||idx.length()==0||idx.length()>4||ts.length()!=10||value.length()>24){server.send(400,"text/plain","Dato non valido");return;}
  for(unsigned i=0;i<idx.length();i++)if(idx[i]<'0'||idx[i]>'9'){server.send(400,"text/plain","Indice non valido");return;}
  unsigned long stamp;double val;
  if(!readMeasurement(ts+","+value,stamp,val)||!inRange(type,val)){server.send(400,"text/plain","Dato non valido");return;}
  if(!fsReady){server.send(503,"text/plain","Memoria non disponibile");return;}
  if(!recoverMeasurement(path)){server.send(500,"text/plain","Recupero storico fallito");return;}
  File src=LittleFS.open(path,"r");if(!src){server.send(404,"text/plain","Misurazione non trovata");return;}
  String tmp=path+".tmp",bak=path+".bak";
  File dst=LittleFS.open(tmp,"w");if(!dst){src.close();server.send(500,"text/plain","Scrittura fallita");return;}
  unsigned long target=idx.toInt(),row=0;bool found=false,ok=true;
  while(src.available()){
    String line=src.readStringUntil('\n');String trimmed=line;trimmed.trim();
    if(row++==target){unsigned long t;double v;if(!readMeasurement(trimmed,t,v)||t!=stamp||String(v,3)!=String(val,3)){ok=false;break;}found=true;}
    else {line+="\n";if(dst.print(line)!=line.length()){ok=false;break;}}
    yield();
  }
  dst.flush();dst.close();src.close();
  if(!ok||!found){LittleFS.remove(tmp);server.send(ok?404:409,"text/plain",ok?"Misurazione non trovata":"Lo storico e cambiato; aggiorna la pagina");return;}
  if(!LittleFS.rename(path,bak)){LittleFS.remove(tmp);server.send(500,"text/plain","Salvataggio fallito");return;}
  if(!LittleFS.rename(tmp,path)){if(!LittleFS.rename(bak,path))Serial.println("Ripristino storico necessario al riavvio");server.send(500,"text/plain","Salvataggio fallito");return;}
  LittleFS.remove(bak);headers();server.send(200,"text/plain","OK");
}

void setup(){digitalWrite(PUMP_PIN,PUMP_OFF);pinMode(PUMP_PIN,OUTPUT);setPump(false);bootStart=millis();phaseStart=bootStart;Serial.begin(115200);loadConfig();waitingToStart=cfg.automatic;LittleFSConfig fsConfig;fsConfig.setAutoFormat(false);LittleFS.setConfig(fsConfig);if(FORMAT_STORICO) LittleFS.format();fsReady=LittleFS.begin();if(fsReady){const char *types[]={"level","ph","ec","temp"};for(const char *type:types)if(!recoverMeasurement(measurementPath(type)))fsReady=false;}if(!fsReady)Serial.println("Memoria LittleFS non disponibile; vedere LEGGIMI.txt");WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
server.on("/",HTTP_GET,[]{if(!authorized())return;headers();server.sendHeader("Content-Encoding","gzip");server.send_P(200,"text/html; charset=utf-8",(const char*)DASHBOARD_GZ,DASHBOARD_GZ_LEN);});
server.on("/api/status",HTTP_GET,status);
server.on("/api/mode",HTTP_POST,[]{if(!authorized())return;String v=server.arg("mode");if(v!="manual"&&v!="auto"){server.send(400,"text/plain","Modalita non valida");return;}setPump(false);cfg.automatic=(v=="auto");phaseStart=millis();bootStart=millis();waitingToStart=cfg.automatic;saveConfig();server.send(200,"text/plain","OK");});
server.on("/api/pump",HTTP_POST,[]{if(!authorized())return;if(cfg.automatic){server.send(409,"text/plain","Passa prima alla modalita manuale");return;}String v=server.arg("on");if(v!="0"&&v!="1"){server.send(400,"text/plain","Comando non valido");return;}setPump(v=="1");server.send(200,"text/plain","OK");});
server.on("/api/settings",HTTP_POST,[]{if(!authorized())return;uint16_t on,every;if(!parseMinutes(server.arg("on"),on)||!parseMinutes(server.arg("every"),every)||every>168||on>=every*60UL){server.send(400,"text/plain","Intervallo non valido");return;}cfg.onMin=on;cfg.everyHours=every;setPump(false);bootStart=millis();phaseStart=bootStart;waitingToStart=cfg.automatic;saveConfig();server.send(200,"text/plain","OK");});
server.on("/api/records",HTTP_GET,getRecords);
server.on("/api/records",HTTP_POST,addRecord);
server.on("/api/records/delete",HTTP_POST,deleteRecord);
server.onNotFound([](){server.send(404,"text/plain","Non trovato");});server.begin();Serial.println("\nAvvio idroponica: pompa OFF");}
void loop(){server.handleClient();unsigned long now=millis();if(cfg.automatic){if(waitingToStart && (unsigned long)(now-bootStart)<BOOT_OFF_MS){if(pump)setPump(false);}else if(waitingToStart){waitingToStart=false;setPump(true);phaseStart=now;}else {unsigned long duration=(unsigned long)(pump?cfg.onMin:(cfg.everyHours*60UL-cfg.onMin))*60000UL;if((unsigned long)(now-phaseStart)>=duration){setPump(!pump);phaseStart=now;}}}if(WiFi.status()==WL_CONNECTED){if(!mdnsStarted){Serial.print("Dashboard: http://");Serial.println(WiFi.localIP());mdnsStarted=MDNS.begin("idroponica");}if(mdnsStarted)MDNS.update();}else mdnsStarted=false;yield();}
