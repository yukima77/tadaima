#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
//#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFiGeneric.h>
//#include <TridentTD_LineNotify.h> // ←ライブラリのインクルードから追加が必要
#define JST     3600* 9
//#define PIN_SENSOR  16
#define PIN_SENSOR  13
#define PIN_TEST    0

// WiFi
char* ssid     = "**********";
char* password = "**********";
const char* softap_ssid     = "tadaima";
const char* softap_password = "12345678";   // more then 8 characters.
ESP8266WebServer server(80); //Webサーバの待ち受けポートを標準的な80番として定義します

// IFTTT
const char* ifttt_host = "maker.ifttt.com";
const char* ifttt_event = "door_opened";
const char* ifttt_secretkey = "**********";

// LINE
//const char* line_host = "notify-api.line.me";
//const char* line_token = "**********";
//const char* line_message = "ただいま";

// 通知をして欲しい、曜日／時間の設定
int start_oclock = 11;
int start_minute = 0;
int end_oclock   = 18;
int end_minute   = 0;
bool set_week[7] = {false, true,  true,  true,  true,  true,  false};
//                  Sun,   Mon,   Tue,   Wed,   Thr,   Fri,   Sat

unsigned long count = 0;              // 1秒毎のカウンタ
bool detectedSensor = false;          // ドアのOPENを検出したフラグ
const int timeServerOff = 300;         // サーバー機能をOFFするまでの時間(秒)
const int delay_notified = 60000;     // 1度通知した後の待ち時間(msec)

// HTML用バッファ
#define BUFFER_SIZE 10240
uint8_t buf[BUFFER_SIZE];

void setup() {
  
  // シリアル設定
  Serial.begin(9600);
  Serial.println("");

  // IOの設定
  pinMode(PIN_SENSOR, INPUT_PULLUP);
  pinMode(PIN_TEST, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_SENSOR), handleInterruptSensor, FALLING); 

  // AP+STAモードの設定
  wifi_set_sleep_type(MODEM_SLEEP_T);
  WiFi.setOutputPower(20.5);
  WiFi.mode(WIFI_AP_STA);
  IPAddress myIP = WiFi.softAPIP();   // APとしてのIPアドレスを取得。デフォルトは 192.168.4.1
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  WiFi.softAP(softap_ssid, softap_password);    // APとして振る舞うためのSSIDとPW情報

  // WiFi接続
  wifiConnect();
  delay(1000);

  // NTP同期
  configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

  // サーバー機能
  server.on("/",                handleRoot);
  server.on("/index.html",      handleRoot);
  server.on("/set-date.html",   handleSetDate);     // 時刻の設定画面
  server.on("/set-wifi.html",   handleSetWifi);
  server.on("/set-ifttt.html",  handleSetIfttt);
  // 処理部
  server.on("/settingDate",   handleSettingDate);
  server.on("/settingWiFi",   handleSettingWiFi);
  server.on("/settingIfttt",  handleSettingIfttt);
  server.onNotFound(handleNotFound);        // エラー処理
  server.begin();
  Serial.println("HTTP server started");

  // ファイルの読み出しテスト
  SPIFFS.begin();
}

void loop() {

  // RSSI強度の表示
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("sensor: %d\n", digitalRead(PIN_SENSOR));

  // 現在の時間を取得
  time_t t;
  struct tm *tm;
  static const char *wd[7] = {"Sun","Mon","Tue","Wed","Thr","Fri","Sat"};
  
  t = time(NULL);
  tm = localtime(&t);
/*
  Serial.printf("%04d/%02d/%02d(%s) %02d:%02d:%02d\n",
        tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
        wd[tm->tm_wday],
        tm->tm_hour, tm->tm_min, tm->tm_sec);
*/

  // センサが搬送した場合の処理
  if(detectedSensor)
  {
    // 設定変更なども出来る様にWiFiをONにする
    WiFi.mode(WIFI_AP_STA);
    wifiConnect();
    configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

    // 曜日のチェック
    if (set_week[tm->tm_wday])
    {
      // 時間のチェック(24H表記)
      if((start_oclock < tm->tm_hour) && (tm->tm_hour < end_oclock))
      {
        sendIftttNotify();
      }
      else if(start_oclock == tm->tm_hour)
      {// 開始時刻の分の判定
        if(start_minute <= tm->tm_min)
        {
          sendIftttNotify();
        }
      }
      else if(end_oclock == tm->tm_hour)
      {// 終了時刻の分の判定
        if(end_minute >= tm->tm_min)
        {
          sendIftttNotify();
        }
      }
    }

    // カウンタをクリア
    count = 0;

    // フラグを下げる
    detectedSensor = false;
  }

  // WiFiをOFFにする処理
  if (count == timeServerOff)
  {
    // WiFiをOffにする
    WiFi.mode(WIFI_OFF);
    Serial.println("Turned WiFi OFF");
  } else if (count < timeServerOff)
  {
    // Webサーバの接続要求待ち
    server.handleClient();
  }
  
  count++;
  Serial.printf("count: %d\n", count);

  // テストピンのチェック
  if(LOW == digitalRead(PIN_TEST))
  {
    Serial.println("TEST NOTIFY");
    sendIftttNotify();
  }

  delay(1000);
}

// 割込み処理
void handleInterruptSensor()
{
  detectedSensor = true;
}

void wifiConnect() {
  Serial.print("Connecting to " + String(ssid));

  //WiFi接続開始
  WiFi.begin(ssid, password);
  
  //接続状態になるまで待つ
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  //接続に成功。IPアドレスを表示
  Serial.println();
  Serial.println("Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


void readHtml(String filename)
{
  // HTMLファイルの読出し
  File htmlFile = SPIFFS.open(filename, "r");
  if (!htmlFile) {
    Serial.println("Failed to open index.html");
  }
  size_t size = htmlFile.size();
  if(size >= BUFFER_SIZE){
    Serial.print("File Size Error:");
    Serial.println((int)size);
  }else{
    Serial.print("File Size OK:");
    Serial.println((int)size);
  }
  memset(buf, 0, sizeof(buf));
  htmlFile.read(buf,size);
  htmlFile.close();
}

void handleRoot() {
  Serial.println("Accessed handleRoot");
  readHtml("/index.html");
  server.send(200, "text/html", (char *)buf); 
}

// 時間の設定
void handleSetDate() {
  readHtml("/set-date.html");
  server.send(200, "text/html", (char *)buf); 
}

void handleSetWifi() {
  readHtml("/set-wifi.html");
  server.send(200, "text/html", (char *)buf); 
}

void handleSetIfttt() {
  readHtml("/set-ifttt.html");
  server.send(200, "text/html", (char *)buf); 
}

// 設定処理
void handleSettingDate() {
  String set_times[2] = {"\0"};
  String temp_start = server.arg("start");
  String temp_end = server.arg("end");
  Serial.print("time_start:  ");
  Serial.println(temp_start);
  Serial.print("time_end:  ");
  Serial.println(temp_end);

  Serial.print("Sun : ");
  Serial.println(server.arg("Sun"));
  Serial.print("Mon : ");
  Serial.println(server.arg("Mon"));
  Serial.print("Tue : ");
  Serial.println(server.arg("Tue"));
  Serial.print("Wed : ");
  Serial.println(server.arg("Wed"));
  Serial.print("Thr : ");
  Serial.println(server.arg("Thr"));
  Serial.print("Fri : ");
  Serial.println(server.arg("Fri"));
  Serial.print("Sat : ");
  Serial.println(server.arg("Sat"));

  set_week[0] = server.arg("Sun") == "1"? true : false;
  set_week[1] = server.arg("Mon") == "1"? true : false;
  set_week[2] = server.arg("Tue") == "1"? true : false;
  set_week[3] = server.arg("Wed") == "1"? true : false;
  set_week[4] = server.arg("Thr") == "1"? true : false;
  set_week[5] = server.arg("Fri") == "1"? true : false;
  set_week[6] = server.arg("Sat") == "1"? true : false;

  Serial.print("Sun : ");
  Serial.println(set_week[0]);
  Serial.print("Mon : ");
  Serial.println(set_week[1]);
  Serial.print("Tue : ");
  Serial.println(set_week[2]);
  Serial.print("Wed : ");
  Serial.println(set_week[3]);
  Serial.print("Thr : ");
  Serial.println(set_week[4]);
  Serial.print("Fri : ");
  Serial.println(set_week[5]);
  Serial.print("Sat : ");
  Serial.println(set_week[6]);
  
  // 時間の設定処理
  if (temp_start.length() > 0 && temp_end.length() > 0) {

    // 開始時刻の処理
    split(temp_start, ':', set_times);
    start_oclock = set_times[0].toInt();
    start_minute = set_times[1].toInt();

    // 終了時刻の処理
    split(temp_end, ':', set_times);
    end_oclock = set_times[0].toInt();
    end_minute = set_times[1].toInt();

    Serial.println("Setting Done!");
    Serial.print("Start Time: ");
    Serial.println(temp_start);
    Serial.print("End Time: ");
    Serial.println(temp_end);    
    server.send(200, "text/html", "Setting Done!");
  } else {
    server.send(200, "text/html", "Setting Fault...");
  }
}

void handleSettingWiFi()
{
}

void handleSettingIfttt()
{
}

// アクセスのエラー処理
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void sendIftttNotify() {
  const char* host = ifttt_host;

  WiFiClient client;
  Serial.println("Try");
  if (!client.connect(host, 80)) {
    Serial.println("connection failed");
    return;
  }

  // We now create a URI for the request
  String url = "/trigger/";
  url += ifttt_event;
  url += "/with/key/";
  url += ifttt_secretkey;

  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  // Read all the lines of the reply from server and print them to Serial
  while (client.connected())
  {
    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }
  }
  client.stop();
  delay(delay_notified);
}

/*
// LINEへの通知処理
void sendLineNotify() {

  LINE.setToken(line_token);
  LINE.notify(line_message); 
  Serial.println("SendLineNotiry");
  delay(delay_notified);
}
*/

// 文字列の分割処理
int split(String data, char delimiter, String *dst){
    int index = 0;
    int arraySize = (sizeof(data)/sizeof((data)[0]));  
    int datalength = data.length();
    for (int i = 0; i < datalength; i++) {
        char tmp = data.charAt(i);
        if ( tmp == delimiter ) {
            index++;
            if ( index > (arraySize - 1)) return -1;
        }
        else dst[index] += tmp;
    }
    return (index + 1);
}
