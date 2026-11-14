#include <DHT.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =================================
// 1. PIN KOMPONEN
// =================================
#define LDR_PIN     34
#define DHT_PIN     32
#define PIR_PIN     27
#define LED_PIN     26
#define BUZZER_PIN  13
#define SERVO_PIN   18
#define BUTTON_PIN  33

// =================================
// 2. KONFIGURASI
// =================================
#define DHT_TYPE DHT22
#define SUHU_BATAS 30.0

// true: nilai LDR lebih rendah saat gelap
// false: nilai LDR lebih tinggi saat gelap
#define DARK_WHEN_LOW true

// =================================
// 3. OBJEK KOMPONEN
// =================================
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// =================================
// 4. VARIABEL
// =================================
float suhu = 0;
float kelembapan = 0;

int nilaiLDR = 0;
int batasGelap = 2000;

bool gelap = false;
bool adaGerakan = false;

bool halamanLCD = false;
bool statusServo = false;

int kondisiTombol = HIGH;
int pembacaanTombolSebelumnya = HIGH;

int jumlahKlik = 0;
bool tombolSedangDitekan = false;

unsigned long waktuBacaDHT = 0;
unsigned long waktuLCD = 0;
unsigned long waktuDebounce = 0;
unsigned long waktuKlikPertama = 0;

const unsigned long intervalDHT = 2000;
const unsigned long intervalLCD = 500;
const unsigned long jedaKlik = 400;

// =================================
// 5. SETUP
// =================================
void setup() {

  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Sensor DHT22
  dht.begin();

  // Servo
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(0);

  // LCD I2C
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("SMART HOME");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  delay(2000);
  lcd.clear();

  Serial.println("SMART HOME AKTIF");
}

// =================================
// 6. PROGRAM UTAMA
// =================================
void loop() {

  unsigned long sekarang = millis();

  // ---------------------------------
  // A. SENSOR LDR DAN LED
  // ---------------------------------

  nilaiLDR = analogRead(LDR_PIN);

  if (DARK_WHEN_LOW) {
    gelap = nilaiLDR < batasGelap;
  } else {
    gelap = nilaiLDR > batasGelap;
  }

  if (gelap) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  // ---------------------------------
  // B. SENSOR DHT22 DAN BUZZER
  // ---------------------------------

  if (sekarang - waktuBacaDHT >= intervalDHT) {

    waktuBacaDHT = sekarang;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {

      suhu = t;
      kelembapan = h;

      // Suhu >= 30 derajat
      if (suhu >= SUHU_BATAS) {
        digitalWrite(BUZZER_PIN, HIGH);
      } else {
        digitalWrite(BUZZER_PIN, LOW);
      }

    } else {
      Serial.println("Gagal membaca DHT22!");
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  // ---------------------------------
  // C. SENSOR PIR DAN SERVO
  // ---------------------------------

  adaGerakan = digitalRead(PIR_PIN) == HIGH;

  if (adaGerakan && !statusServo) {

    myServo.write(90);
    delay(500);
    myServo.write(0);
    statusServo = true;

    Serial.println("Gerakan terdeteksi - Pintu terbuka");
  }

  if (!adaGerakan && statusServo) {

    myServo.write(0);
    statusServo = false;

    Serial.println("Tidak ada gerakan - Pintu tertutup");
  }

  // ---------------------------------
  // D. PUSH BUTTON DUA KONDISI
  // ---------------------------------

  int pembacaanTombol = digitalRead(BUTTON_PIN);

  // Deteksi tombol ditekan
  if (pembacaanTombol == LOW &&
      pembacaanTombolSebelumnya == HIGH &&
      !tombolSedangDitekan &&
      sekarang - waktuDebounce > 50) {

    tombolSedangDitekan = true;
    jumlahKlik++;

    waktuDebounce = sekarang;

    if (jumlahKlik == 1) {
      waktuKlikPertama = sekarang;
    }
  }

  // Deteksi tombol dilepas
  if (pembacaanTombol == HIGH) {
    tombolSedangDitekan = false;
  }

  // Menentukan jumlah klik
  if (jumlahKlik > 0 &&
      sekarang - waktuKlikPertama >= jedaKlik) {

    if (jumlahKlik == 1) {

      // 1 KALI KLIK = HALAMAN KEDUA
      halamanLCD = true;

      Serial.println("LCD HALAMAN 2");

    } else if (jumlahKlik == 2) {

      // 2 KALI KLIK = HALAMAN PERTAMA
      halamanLCD = false;

      Serial.println("LCD HALAMAN 1");
    }

    lcd.clear();
    jumlahKlik = 0;
  }

  pembacaanTombolSebelumnya = pembacaanTombol;

  // ---------------------------------
  // E. TAMPILAN LCD
  // ---------------------------------

  if (sekarang - waktuLCD >= intervalLCD) {

    waktuLCD = sekarang;

    lcd.clear();

    if (!halamanLCD) {

      // HALAMAN PERTAMA
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(suhu, 1);
      lcd.print((char)223);
      lcd.print("C");

      lcd.setCursor(0, 1);
      lcd.print("Hum: ");
      lcd.print(kelembapan, 1);
      lcd.print("%");

    } else {

      // HALAMAN KEDUA
      lcd.setCursor(0, 0);
      lcd.print("Light: ");

      if (gelap) {
        lcd.print("DARK");
      } else {
        lcd.print("BRIGHT");
      }

      lcd.setCursor(0, 1);
      lcd.print("Motion: ");

      if (adaGerakan) {
        lcd.print("DETECTED");
      } else {
        lcd.print("NONE");
      }
    }
  }

  // ---------------------------------
  // F. SERIAL MONITOR
  // ---------------------------------

  Serial.print("Suhu: ");
  Serial.print(suhu);

  Serial.print(" C | Kelembapan: ");
  Serial.print(kelembapan);

  Serial.print(" % | LDR: ");
  Serial.print(nilaiLDR);

  Serial.print(" | Gerakan: ");
  Serial.println(adaGerakan ? "ADA" : "TIDAK ADA");

}