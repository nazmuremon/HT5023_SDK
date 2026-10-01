void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  Serial.println("HT5023 Serial ready");
}

void loop() {
  if (Serial.available() > 0) {
    int value = Serial.read();
    digitalToggle(LED_BUILTIN);
    Serial.print("RX: ");
    Serial.println((char)value);
  }
}
