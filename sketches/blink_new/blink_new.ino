void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println("HT5023 Blink: LED on PIN_LED1, one second per state");
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("LED on");
  delay(100);

  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("LED off");
  delay(100);
}