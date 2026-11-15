#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// =================================
// 0. KONFIGURASI WIFI & MQTT (LANGSUNG DISINI)
// =================================
const char* ssid = "Kesya Regina";
const char* password = "kesya124";

const char* mqtt_server = "q044e283.ala.asia-southeast1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_user = "kelompokpenyu";
const char* mqtt_password = "kelompokpenyu123";

// =================================
// 1. PIN KOMPONEN
// =================================
#define LDR_PIN 34
#define DHT_PIN 32
#define PIR_PIN 27

#define LED_PIN 26
#define LED_HIJAU_PIN 14

#define BUZZER_PIN 13
#define SERVO_PIN 18
#define BUTTON_PIN 33

#define FLAME_PIN 5

// LCD I2C
#define SDA_PIN 21
#define SCL_PIN 22

// =================================
// 2. KONFIGURASI
// =================================
#define DHT_TYPE DHT22
#define SUHU_BATAS 30.0          

#define LOOP_DELAY 50

#define DARK_WHEN_LOW true
const int BATAS_GELAP = 2000;
const int HISTERESIS = 100;

#define FLAME_AUTO_CAL true
#define FLAME_ACTIVE_LOW true      

#define API_PICU_ALARM true
#define LED_IKUT_GELAP true

#define PIR_ACTIVE_HIGH true
const unsigned long WARMUP_PIR = 30000;         
const unsigned long DELAY_NAIK_PIR = 10000;    
const unsigned long DELAY_TURUN_PIR = 10000;    
const unsigned long MIN_BUKA = 5000;            

#define SERVO_TUTUP 0
#define SERVO_BUKA 90
const unsigned long REFRESH_SERVO = 1000;      

#define BUZZER_PASIF true
#define BUZZER_FREK_1 2700        
#define BUZZER_FREK_2 3600        
#define BUZZER_SIRENE true        
const unsigned long INTERVAL_SIRENE = 300;   
#define BUZZER_CHANNEL 6          

// =================================
// 3. OBJEK KOMPONEN & MQTT
// =================================
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiClientSecure espClient;
PubSubClient client(espClient);

// =================================
// 4. VARIABEL
// =================================
float suhu = 0;
float kelembapan = 0;

int nilaiLDR = 0;
int pinPIR = 0;
int pinApi = 0;
int flameNormal = HIGH;  

bool gelap = false;
bool suhuPanas = false;
bool adaApi = false;
bool adaGerakan = false;

bool pirSiap = false;
bool pirRawSebelumnya = false;
bool pirStabil = false;
unsigned long waktuPirBerubah = 0;

bool pintuTerbuka = false;
bool servoTerbuka = false;
unsigned long waktuPintuBuka = 0;
unsigned long waktuServo = 0;

bool alarmDiaktifkan = true;   
bool kondisiBahaya = false;
bool buzzerBunyi = false;
bool ledMerahNyala = false;
int frekBuzzerSekarang = 0;

int tombolSebelumnya = HIGH;
unsigned long waktuDebounce = 0;

int halamanLCD = 0;
const int JUMLAH_HALAMAN = 3;

bool gelapSebelumnya = false;
bool suhuPanasSebelumnya = false;
bool gerakanSebelumnya = false;
bool pintuSebelumnya = false;
bool apiSebelumnya = false;
bool buzzerSebelumnya = false;

unsigned long waktuBacaDHT = 0;
unsigned long waktuLCD = 0;
unsigned long waktuGantiHalaman = 0;
unsigned long waktuSerial = 0;
unsigned long waktuKirimMqtt = 0;

const unsigned long intervalDHT = 2000;
const unsigned long intervalLCD = 300;
const unsigned long intervalHalaman = 3000;
const unsigned long intervalSerial = 1000;
const unsigned long intervalMqtt = 5000;

// =================================
// 5. FUNGSI BANTU
// =================================
String waktuBerjalan() {
  unsigned long detik = millis() / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           detik / 3600, (detik / 60) % 60, detik % 60);
  return String(buf);
}

void logEvent(const char *pesan) {
  Serial.printf("[%s] >> %s\n", waktuBerjalan().c_str(), pesan);
}

int bacaLDRRataRata() {
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(LDR_PIN);
  }
  return total / 10;
}

void kalibrasiFlame() {
  int tinggi = 0, rendah = 0;
  for (int i = 0; i < 30; i++) {
    if (digitalRead(FLAME_PIN) == HIGH) tinggi++;
    else rendah++;
    delay(30);
  }
  flameNormal = (tinggi >= rendah) ? HIGH : LOW;
}

void reconnectMqtt() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke EMQX Cloud...");
    String clientId = "ESP32SmartHome-";
    clientId += String(random(0xffff), HEX);
    
    espClient.setInsecure();

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("TERHUBUNG!");
    } else {
      Serial.print("gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

void buzzerInit() {
  if (!BUZZER_PASIF) {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, 2000, 10);
  ledcWriteTone(BUZZER_PIN, 0);
#else
  ledcSetup(BUZZER_CHANNEL, 2000, 10);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWriteTone(BUZZER_CHANNEL, 0);
#endif
}

void buzzerTulisNada(int frek) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, frek);
#else
  ledcWriteTone(BUZZER_CHANNEL, frek);
#endif
}

void buzzerSet(bool nyala) {
  if (!BUZZER_PASIF) {
    digitalWrite(BUZZER_PIN, nyala ? HIGH : LOW);
    return;
  }

  if (!nyala) {
    if (frekBuzzerSekarang != 0) {
      buzzerTulisNada(0);
      frekBuzzerSekarang = 0;
    }
    return;
  }

  int frek = BUZZER_FREK_1;
  if (BUZZER_SIRENE) {
    frek = ((millis() / INTERVAL_SIRENE) % 2 == 0) ? BUZZER_FREK_1 : BUZZER_FREK_2;
  }

  if (frek != frekBuzzerSekarang) {
    buzzerTulisNada(frek);
    frekBuzzerSekarang = frek;
  }
}

void cetakStatus() {
  unsigned long sisaWarmup = (millis() < WARMUP_PIR) ? (WARMUP_PIR - millis()) / 1000 : 0;
  Serial.println();
  Serial.println("+==================================+");
  Serial.printf( "|   SMART HOME STATUS     [%s] |\n", waktuBerjalan().c_str());
  Serial.println("+==================================+");
  Serial.printf( "| Suhu        : %.1f C %s\n", suhu, suhuPanas ? "(PANAS)" : "(normal)");
  Serial.printf( "| Kelembapan  : %.1f %%\n", kelembapan);
  Serial.printf( "| Cahaya      : %s (LDR=%d)\n", gelap ? "GELAP" : "TERANG", nilaiLDR);
  Serial.printf( "| Pintu       : %s\n", pintuTerbuka ? "TERBUKA" : "TERTUTUP");
  Serial.printf( "| Api         : %s\n", adaApi ? "!!! ADA API !!!" : "AMAN");
  Serial.println("+----------------------------------+");
}

// =================================
// 6. SETUP
// =================================
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_HIJAU_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(FLAME_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_HIJAU_PIN, LOW);

  analogReadResolution(12);  
  dht.begin();

  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(SERVO_TUTUP);

  buzzerInit();

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("SMART HOME");
  lcd.setCursor(0, 1);
  lcd.print("Menghubungkan WiFi");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");

  client.setServer(mqtt_server, mqtt_port);

  if (FLAME_AUTO_CAL) {
    kalibrasiFlame();
  } else {
    flameNormal = FLAME_ACTIVE_LOW ? HIGH : LOW;
  }

  delay(1000);
  lcd.clear();
  Serial.println("Sistem Smart Home Aktif + MQTT EMQX!");
}

// =================================
// 7. PROGRAM UTAMA
// =================================
void loop() {
  unsigned long sekarang = millis();

  if (!client.connected()) {
    reconnectMqtt();
  }
  client.loop();

  // LDR
  nilaiLDR = bacaLDRRataRata();
  if (DARK_WHEN_LOW) {
    if (!gelap && nilaiLDR < BATAS_GELAP - HISTERESIS) gelap = true;
    else if (gelap && nilaiLDR > BATAS_GELAP + HISTERESIS) gelap = false;
  } else {
    if (!gelap && nilaiLDR > BATAS_GELAP + HISTERESIS) gelap = true;
    else if (gelap && nilaiLDR < BATAS_GELAP - HISTERESIS) gelap = false;
  }

  // DHT22
  if (sekarang - waktuBacaDHT >= intervalDHT) {
    waktuBacaDHT = sekarang;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      suhu = t;
      kelembapan = h;
    }
  }
  suhuPanas = (suhu > SUHU_BATAS);

  // Flame
  pinApi = digitalRead(FLAME_PIN);
  adaApi = (pinApi != flameNormal);
  digitalWrite(LED_HIJAU_PIN, adaApi ? LOW : HIGH);   

  // Button
  int pembacaanTombol = digitalRead(BUTTON_PIN);
  if (pembacaanTombol == LOW && tombolSebelumnya == HIGH && sekarang - waktuDebounce > 80) {
    alarmDiaktifkan = !alarmDiaktifkan;
    waktuDebounce = sekarang;
  }
  tombolSebelumnya = pembacaanTombol;

  // LED & Buzzer
  ledMerahNyala = suhuPanas || (LED_IKUT_GELAP && gelap) || (API_PICU_ALARM && adaApi);
  digitalWrite(LED_PIN, ledMerahNyala ? HIGH : LOW);
  kondisiBahaya = (gelap && suhuPanas) || (API_PICU_ALARM && adaApi);
  buzzerBunyi = alarmDiaktifkan && kondisiBahaya;
  buzzerSet(buzzerBunyi);

  // PIR & Servo
  pinPIR = digitalRead(PIR_PIN);
  bool pirRaw = PIR_ACTIVE_HIGH ? (pinPIR == HIGH) : (pinPIR == LOW);
  if (pirRaw != pirRawSebelumnya) {
    pirRawSebelumnya = pirRaw;
    waktuPirBerubah = sekarang;
  }
  if (pirRaw != pirStabil) {
    unsigned long delayDipakai = pirRaw ? DELAY_NAIK_PIR : DELAY_TURUN_PIR;
    if (sekarang - waktuPirBerubah >= delayDipakai) pirStabil = pirRaw;
  }
  if (!pirSiap) {
    if (sekarang >= WARMUP_PIR) pirSiap = true;
    adaGerakan = false;   
  } else {
    adaGerakan = pirStabil;
  }

  if (adaGerakan && !pintuTerbuka) {
    pintuTerbuka = true;
    waktuPintuBuka = sekarang;
  }
  if (!adaGerakan && pintuTerbuka && sekarang - waktuPintuBuka >= MIN_BUKA) {
    pintuTerbuka = false;
  }

  if (pintuTerbuka != servoTerbuka) {
    servoTerbuka = pintuTerbuka;
    myServo.write(servoTerbuka ? SERVO_BUKA : SERVO_TUTUP);
    waktuServo = sekarang;
  } else if (sekarang - waktuServo >= REFRESH_SERVO) {
    waktuServo = sekarang;
    myServo.write(servoTerbuka ? SERVO_BUKA : SERVO_TUTUP);
  }

  // Kirim MQTT
  if (sekarang - waktuKirimMqtt >= intervalMqtt) {
    waktuKirimMqtt = sekarang;
    String payload = "{";
    payload += "\"suhu\":" + String(suhu, 1) + ",";
    payload += "\"kelembapan\":" + String(kelembapan, 1) + ",";
    payload += "\"ldr\":" + String(nilaiLDR) + ",";
    payload += "\"gelap\":" + String(gelap ? 1 : 0) + ",";
    payload += "\"adaApi\":" + String(adaApi ? 1 : 0) + ",";
    payload += "\"pintuTerbuka\":" + String(pintuTerbuka ? 1 : 0) + ",";
    payload += "\"alarm\":" + String(alarmDiaktifkan ? 1 : 0);
    payload += "}";

    client.publish("smarthome/telemetri", payload.c_str());
  }

  // LCD Display
  if (sekarang - waktuGantiHalaman >= intervalHalaman) {
    waktuGantiHalaman = sekarang;
    halamanLCD = (halamanLCD + 1) % JUMLAH_HALAMAN;
    lcd.clear();
  }

  if (sekarang - waktuLCD >= intervalLCD) {
    waktuLCD = sekarang;
    if (halamanLCD == 0) {
      lcd.setCursor(0, 0); lcd.print("Temp: "); lcd.print(suhu, 1); lcd.print((char)223); lcd.print("C    ");
      lcd.setCursor(0, 1); lcd.print("Hum: "); lcd.print(kelembapan, 1); lcd.print("%     ");
    } else if (halamanLCD == 1) {
      lcd.setCursor(0, 0); lcd.print("Light: "); lcd.print(gelap ? "DARK  " : "BRIGHT");
      lcd.setCursor(0, 1); lcd.print("Flame: "); lcd.print(adaApi ? "DETECTED!" : "SAFE     ");
    } else {
      lcd.setCursor(0, 0); lcd.print("Door : "); lcd.print(pintuTerbuka ? "OPEN  " : "CLOSED");
      lcd.setCursor(0, 1); lcd.print("Alarm: "); lcd.print(alarmDiaktifkan ? "ON " : "OFF");
    }
  }

  delay(LOOP_DELAY);
}