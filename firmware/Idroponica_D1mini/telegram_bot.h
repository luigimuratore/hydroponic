#pragma once
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>
#include "telegram_ca.h"

// Gli ID chat Telegram possono essere più grandi di 32 bit: confronto come stringhe.
static BearSSL::X509List telegramTrust(TELEGRAM_CA);
static int64_t telegramOffset = 0;
static bool telegramPrimed = false;
static String telegramPendingReply;
static unsigned long telegramLastPoll = 0;
static const unsigned long TELEGRAM_POLL_MS = 10000;

bool telegramEnabled() {
  return TELEGRAM_BOT_TOKEN[0] && TELEGRAM_CHAT_ID[0];
}
String telegramUrl(const char *method) {
  return String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/" + method;
}
bool telegramSend(const String &text) {
  BearSSL::WiFiClientSecure client;
  client.setTrustAnchors(&telegramTrust);
  client.setTimeout(1800);
  HTTPClient http;
  http.setTimeout(1800);
  http.useHTTP10(true);
  if (!http.begin(client, telegramUrl("sendMessage"))) return false;
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<1024> doc;
  doc["chat_id"] = TELEGRAM_CHAT_ID;
  doc["text"] = text;
  JsonObject markup=doc.createNestedObject("reply_markup");
  markup["resize_keyboard"]=true;
  markup["is_persistent"]=true;
  JsonArray keyboard=markup.createNestedArray("keyboard");
  JsonArray first=keyboard.createNestedArray(); first.add("📊 /stato"); first.add("❔ /aiuto");
  JsonArray second=keyboard.createNestedArray(); second.add("▶️ /on"); second.add("⏹️ /off");
  JsonArray third=keyboard.createNestedArray(); third.add("👆 /manuale"); third.add("🔄 /auto");
  if (doc.overflowed()) {http.end();return false;}
  String body; serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  return code == 200;
}
String telegramStatus() {
  String result = String("GIGIPONICA\nModalita: ") + (cfg.automatic ? "automatica" : "manuale") +
    "\nPompa: " + (pump ? "accesa" : "spenta") +
    "\nProgramma: " + String(cfg.onMin) + " min ogni " + String(cfg.everyHours) + " ore";
  if (otaLocked) result += "\nComandi bloccati: riavvia dopo OTA.";
  result += "\nStato comandato: flusso e livello non sono misurati.";
  return result;
}
String telegramCommand(String command) {
  command.trim(); command.toLowerCase();
  if (command == "/start" || command == "/aiuto" || command == "/help")
    return "GIGIPONICA\n/stato - stato e programma\n/on - accendi in manuale\n/off - spegni e passa in manuale\n/manuale - passa in manuale, pompa spenta\n/auto - avvia il programma automatico";
  if (command == "/stato" || command == "/status") return telegramStatus();
  if (otaLocked) return "Comandi bloccati dopo OTA. Riavvia la scheda.";
  if (command == "/off" || command == "/manuale") {
    setPump(false); cfg.automatic=0; waitingToStart=false; saveConfig();
    return "Modalita manuale. Pompa spenta.";
  }
  if (command == "/on") {
    if (cfg.automatic) return "Passa prima in manuale con /manuale.";
    setPump(true); return "Pompa accesa in manuale. Controlla il livello dell'acqua.";
  }
  if (command == "/auto") {
    setPump(false); cfg.automatic=1; saveConfig();
    phaseStart=millis(); bootStart=phaseStart; waitingToStart=true;
    return "Modalita automatica attiva. La pompa parte dopo 5 secondi.";
  }
  return "Comando non riconosciuto. Usa /aiuto.";
}
void telegramPoll() {
  if (!telegramEnabled() || otaLocked || time(nullptr) < 1577836800) return;
  if (telegramPendingReply.length()) {
    String reply=telegramPendingReply; telegramPendingReply="";
    telegramSend(reply); return;
  }
  unsigned long nowMs=millis();
  if ((unsigned long)(nowMs-telegramLastPoll)<TELEGRAM_POLL_MS) return;
  telegramLastPoll=nowMs;
  BearSSL::WiFiClientSecure client;
  client.setTrustAnchors(&telegramTrust);
  client.setTimeout(1800);
  HTTPClient http;
  http.setTimeout(1800);
  http.useHTTP10(true);
  String url=telegramUrl("getUpdates") + "?timeout=0&limit=1&offset=" + (telegramPrimed ? String((long)telegramOffset) : String("-1"));
  if (!http.begin(client,url)) return;
  int code=http.GET();
  if (code!=200) {http.end(); return;}
  StaticJsonDocument<512> filter;
  filter["ok"]=true; filter["result"][0]["update_id"]=true;
  filter["result"][0]["message"]["chat"]["id"]=true;
  filter["result"][0]["message"]["chat"]["type"]=true;
  filter["result"][0]["message"]["date"]=true;
  filter["result"][0]["message"]["text"]=true;
  if (filter.overflowed()) {http.end(); return;}
  DynamicJsonDocument doc(1536);
  DeserializationError err=deserializeJson(doc,http.getStream(),DeserializationOption::Filter(filter));
  http.end();
  if (err || !doc["ok"].as<bool>()) return;
  JsonArray result=doc["result"].as<JsonArray>();
  if (result.isNull()) return;
  if (result.size()==0) {telegramPrimed=true;return;}
  JsonObject update=result[0];
  if (!update["update_id"].is<int32_t>()) return;
  int32_t id=update["update_id"].as<int32_t>();
  telegramOffset=(int64_t)id+1;
  if (!telegramPrimed) {telegramPrimed=true;return;}
  JsonObject msg=update["message"];
  if (msg.isNull() || strcmp(msg["chat"]["type"] | "", "private") != 0 || !msg["chat"]["id"].is<int64_t>()) return;
  int64_t chat=msg["chat"]["id"].as<int64_t>();
  char chatText[24]; snprintf(chatText,sizeof(chatText),"%lld",(long long)chat);
  if (strcmp(chatText,TELEGRAM_CHAT_ID)!=0) return;
  time_t timestamp=msg["date"] | 0; time_t current=time(nullptr);
  if (timestamp<current-60 || timestamp>current+30) return;
  const char *text=msg["text"] | "";
  // Accept only the exact labels of our keyboard or explicitly typed commands.
  struct ButtonCommand { const char *label; const char *command; };
  const ButtonCommand buttons[]={
    {"📊 /stato", "/stato"}, {"❔ /aiuto", "/aiuto"},
    {"▶️ /on", "/on"}, {"⏹️ /off", "/off"},
    {"👆 /manuale", "/manuale"}, {"🔄 /auto", "/auto"}
  };
  const char *command=text;
  if (text[0]!='/') {
    command=nullptr;
    for (const auto &button:buttons) if (strcmp(text,button.label)==0) {command=button.command;break;}
    if (!command) return;
  }
  telegramPendingReply=telegramCommand(command);
}
