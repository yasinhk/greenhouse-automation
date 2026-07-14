#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

const char* ssid     = "Yasin";
const char* password = "yasinhakan";

#define RXD2 16
#define TXD2 17

WebServer server(80);

float sicaklik  = 0.0;
float nem       = 0.0;
float toprakNem = 0.0;
float suSeviye  = 0.0;
int   fanDurum  = 0;
int   pompaDurum = 0;
String ipAdresi = "";

String getDashboardHTML() {
  // Raw String Literal (R"=====()=====") başlatıyoruz. 
  // Bu sayede içindeki tırnak işaretleri (', ") kodu bozmaz, her satır için += yazmaya gerek kalmaz.
  String html = R"=====(
<!DOCTYPE html>
<html lang='tr'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Sera Otomasyonu</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', sans-serif; background: linear-gradient(135deg, #0f2027, #203a43, #2c5364); min-height: 100vh; color: #fff; padding: 20px; }
    h1 { text-align: center; font-size: 1.8em; margin-bottom: 8px; color: #4ecca3; letter-spacing: 2px; }
    .subtitle { text-align: center; font-size: 0.85em; color: #aaa; margin-bottom: 25px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; max-width: 900px; margin: 0 auto 25px auto; }
    .kart { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12); border-radius: 16px; padding: 22px 18px; text-align: center; }
    .kart .ikon { font-size: 2.2em; margin-bottom: 8px; }
    .kart .baslik { font-size: 0.8em; color: #aaa; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 6px; }
    .kart .deger { font-size: 2em; font-weight: bold; color: #4ecca3; }
    .kart .birim { font-size: 0.8em; color: #888; margin-top: 2px; }
    .role-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; max-width: 900px; margin: 0 auto 20px auto; }
    .role-kart { border-radius: 16px; padding: 18px; text-align: center; font-weight: bold; border: 2px solid transparent; }
    .role-acik { background: rgba(78,204,163,0.15); border-color: #4ecca3; color: #4ecca3; }
    .role-kapali { background: rgba(255,255,255,0.05); border-color: #444; color: #666; }
    .role-kart .ikon { font-size: 1.8em; margin-bottom: 6px; }
    .guncelleme { text-align: center; font-size: 0.82em; color: #4ecca3; margin-bottom: 20px; }
    .footer { text-align: center; font-size: 0.78em; color: #555; margin-top: 10px; }
  </style>
</head>
<body>
  <h1>🌿 Sera Otomasyonu</h1>
  <div class='subtitle'>STM32 + ESP32 | Gercek Zamanli Izleme</div>
  <div class='guncelleme' id='guncelleme'>Son guncelleme: --</div>
  
  <div class='grid'>
    <div class='kart'><div class='ikon'>🌡️</div><div class='baslik'>Sicaklik</div><div class='deger' id='sicaklik'>--</div><div class='birim'>°C</div></div>
    <div class='kart'><div class='ikon'>💧</div><div class='baslik'>Hava Nemi</div><div class='deger' id='nem'>--</div><div class='birim'>%</div></div>
    <div class='kart'><div class='ikon'>🌱</div><div class='baslik'>Toprak Nemi</div><div class='deger' id='toprak'>--</div><div class='birim'>%</div></div>
    <div class='kart'><div class='ikon'>🪣</div><div class='baslik'>Su Seviyesi</div><div class='deger' id='su'>--</div><div class='birim'>%</div></div>
  </div>
  
  <div class='role-grid'>
    <div class='role-kart role-kapali' id='fan-kart'><div class='ikon'>🌀</div><div id='fan-durum'>Fan: --</div></div>
    <div class='role-kart role-kapali' id='pompa-kart'><div class='ikon'>⚙️</div><div id='pompa-durum'>Pompa: --</div></div>
  </div>
  
  <div class='footer'>ESP32 IP: %IP_ADRESI% | Otomatik yenileme: 3 sn</div>
  
  <script>
    function veriGuncelle(){
      fetch('/veri').then(r=>r.json()).then(d=>{
        document.getElementById('sicaklik').textContent=d.temp.toFixed(1);
        document.getElementById('nem').textContent=d.hum.toFixed(1);
        document.getElementById('toprak').textContent=d.soil.toFixed(1);
        document.getElementById('su').textContent=d.water.toFixed(0);
        
        var fk=document.getElementById('fan-kart');
        var pk=document.getElementById('pompa-kart');
        
        if(d.fan===1){
          fk.className='role-kart role-acik';
          document.getElementById('fan-durum').textContent='Fan: CALISIYOR';
        } else {
          fk.className='role-kart role-kapali';
          document.getElementById('fan-durum').textContent='Fan: KAPALI';
        }
        
        if(d.pump===1){
          pk.className='role-kart role-acik';
          document.getElementById('pompa-durum').textContent='Pompa: CALISIYOR';
        } else {
          pk.className='role-kart role-kapali';
          document.getElementById('pompa-durum').textContent='Pompa: KAPALI';
        }
        
        var now=new Date();
        document.getElementById('guncelleme').textContent='Son guncelleme: '+now.toLocaleTimeString('tr-TR');
      }).catch(function(){
        document.getElementById('guncelleme').textContent='Baglanti hatasi...';
      });
    }
    
    veriGuncelle();
    setInterval(veriGuncelle,3000);
  </script>
</body>
</html>
)=====";

  // HTML içindeki %IP_ADRESI% etiketini bulup, sistemin gerçek IP adresiyle tek seferde değiştiriyoruz.
  html.replace("%IP_ADRESI%", ipAdresi);
  
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getDashboardHTML());
}

void handleVeri() {
  StaticJsonDocument<200> doc;
  doc["temp"]  = sicaklik;
  doc["hum"]   = nem;
  doc["soil"]  = toprakNem;
  doc["water"] = suSeviye;
  doc["fan"]   = fanDurum;
  doc["pump"]  = pompaDurum;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void STM32VeriOku() {
  if (Serial2.available()) {
    String satir = Serial2.readStringUntil('\n');
    satir.trim();
    if (satir.startsWith("{")) {
      StaticJsonDocument<512> doc;
      DeserializationError hata = deserializeJson(doc, satir);
      if (!hata) {
        sicaklik   = doc["temp"]  | 0.0f;
        nem        = doc["hum"]   | 0.0f;
        toprakNem  = doc["soil"]  | 0.0f;
        suSeviye   = doc["water"] | 0.0f;
        fanDurum   = doc["fan"]   | 0;
        pompaDurum = doc["pump"]  | 0;
        Serial.println("Veri alindi: " + satir);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  WiFi.begin(ssid, password);
  Serial.print("WiFi baglanıyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  ipAdresi = WiFi.localIP().toString();
  Serial.println("\nWiFi baglandi! IP: http://" + ipAdresi);

  server.on("/",     handleRoot);
  server.on("/veri", handleVeri);
  server.begin();
  Serial.println("Web sunucu basladi.");
}

void loop() {
  STM32VeriOku();
  server.handleClient();
}