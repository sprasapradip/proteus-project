// Gas/Smoke Detector - Unique Style

const int GAS_SENSOR_PIN = A0;
const int GAS_THRESHOLD = 500;

const int BUZZER_PIN = 9;
const int LED_PIN = 13;

bool isGasDetected = false;

void setup() {
  pinMode(GAS_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  Serial.begin(9600);
  Serial.println("Gas/Smoke Detector - Unique Style");
}

void loop() {
  int gasValue = analogRead(GAS_SENSOR_PIN);

  Serial.print("Gas Value: ");
  Serial.println(gasValue);

  if (gasValue > GAS_THRESHOLD) {
    if (!isGasDetected) {
      isGasDetected = true;
      activateAlarm();
      Serial.println("Gas/Smoke detected!");
    }
  } else {
    if (isGasDetected) {
      isGasDetected = false;
      deactivateAlarm();
      Serial.println("Gas/Smoke cleared.");
    }
  }

  delay(1000);
}

void activateAlarm() {
  tone(BUZZER_PIN, 2000, 1000);
  digitalWrite(LED_PIN, HIGH);
}

void deactivateAlarm() {
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
}
