
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

