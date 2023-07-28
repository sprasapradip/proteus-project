// Define the analog pin for the gas/smoke sensor
const int gasSensorPin = A0;

// Threshold value to trigger an alarm. Adjust this according to your sensor's sensitivity
const int gasThreshold = 500; // Change this value based on your sensor's calibration

void setup() {
  // Initialize serial communication for debugging (optional)
  Serial.begin(9600);
  // Set the gas sensor pin as an input
  pinMode(gasSensorPin, INPUT);
}

void loop() {
  // Read the analog value from the gas sensor
  int gasValue = analogRead(gasSensorPin);

  // Print the raw analog value (optional, for debugging)
  Serial.print("Gas Value: ");
  Serial.println(gasValue);

