#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SparkFun_AS3935.h>

// WiFi credentials
const char* ssid = "SomeWifiNetwork";
const char* password = "ILikeOnions123";

// Server URL
const char* serverUrl = "https://prod.monitman.com/dashboard/receive.php?sensor=lightning";

// AS3935 settings - Connect SI to VDD (3.3v+) to use I2C instead of SPI
#define AS3935_ADDR 0x03  // Default address 0x03, may have a jumper for using address 0x02
#define IRQ_PIN 2
SparkFun_AS3935 lightning(AS3935_ADDR);

// Interrupt register values
#define NOISE_LEVEL_INT 0x01
#define DISTURBER_INT_VAL 0x04
#define LIGHTNING_INT_VAL 0x08

// Timing
unsigned long lastUpload = 0;
const unsigned long uploadInterval = 60000; // 1 minute

// Lightning data
volatile bool lightningDetected = false;
int lightningCount = 0;
int lastDistance = 0;
String lastEventType = "none";
unsigned long lastStrikeTime = 0;
uint32_t lastEnergy = 0;

void IRAM_ATTR lightningISR() {
  lightningDetected = true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("AS3935 Lightning Sensor + ESP32-C6");
  
  // Initialize I2C
  Wire.begin(6, 7); // SDA=GPIO6, SCL=GPIO7 for ESP32-C6
  
  // Initialize AS3935
  pinMode(IRQ_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(IRQ_PIN), lightningISR, RISING);
  
  if (!lightning.begin()) {
    Serial.println("AS3935 not detected! Check wiring.");
    while (1);
  }
  Serial.println("AS3935 initialized!");
  
  // Configure sensor
  lightning.setIndoorOutdoor(OUTDOOR); // or INDOOR
  lightning.setNoiseLevel(2);          // 0-7, higher = less sensitive
  lightning.watchdogThreshold(2);      // 0-10
  lightning.spikeRejection(2);         // 1-11
  lightning.lightningThreshold(1);     // 1-10 strikes before IRQ
  
  // Wait for sensor to calibrate
  delay(1000);
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Check for lightning interrupt
  if (lightningDetected) {
    lightningDetected = false;
    delay(2); // Let sensor settle
    
    uint8_t intVal = lightning.readInterruptReg();
    
    if (intVal == NOISE_LEVEL_INT) {
      Serial.println("Noise detected");
      lastEventType = "noise";
    }
    else if (intVal == DISTURBER_INT_VAL) {
      Serial.println("Disturber detected");
      lastEventType = "disturber";
    }
    else if (intVal == LIGHTNING_INT_VAL) {
      lastDistance = lightning.distanceToStorm();
      lastEnergy = lightning.lightningEnergy();
      lightningCount++;
      lastStrikeTime = millis();
      lastEventType = "lightning";
      
      Serial.println("⚡ Lightning detected!");
      Serial.print("Distance: ");
      Serial.print(lastDistance);
      Serial.println(" km");
      Serial.print("Energy: ");
      Serial.println(lastEnergy);
      Serial.print("Total strikes: ");
      Serial.println(lightningCount);
    }
  }
  
  // Upload data every minute
  if (millis() - lastUpload >= uploadInterval) {
    uploadData();
    lastUpload = millis();
  }
}

void uploadData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected!");
    return;
  }
  
  // Build JSON string manually
  String jsonString = "{";
  jsonString += "\"device\":\"ESP32-C6\",";
  jsonString += "\"sensor\":\"AS3935\",";
  jsonString += "\"timestamp\":" + String(millis()) + ",";
  jsonString += "\"lightning_count\":" + String(lightningCount) + ",";
  jsonString += "\"last_distance_km\":" + String(lastDistance) + ",";
  jsonString += "\"last_energy\":" + String(lastEnergy) + ",";
  jsonString += "\"last_event\":\"" + lastEventType + "\",";
  
  unsigned long timeSinceStrike = 0;
  if (lastStrikeTime > 0) {
    timeSinceStrike = millis() - lastStrikeTime;
  }
  jsonString += "\"time_since_strike_ms\":" + String(timeSinceStrike) + ",";
  
  // Get current settings
  jsonString += "\"noise_level\":" + String(lightning.readNoiseLevel()) + ",";
  jsonString += "\"watchdog_threshold\":" + String(lightning.readWatchdogThreshold());
  jsonString += "}";
  
  Serial.println("\n--- Uploading Data ---");
  Serial.println(jsonString);
  
  // Send HTTP POST
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.print("Error: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
  Serial.println("----------------------\n");
}
