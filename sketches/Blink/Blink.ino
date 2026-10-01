// Blink -- the smallest Arduino sketch that proves the board is alive.
//
// Open this in the Flasher (or the Arduino IDE) and press Compile & Upload.
// The LED blinks and the reason for each state change is printed, so the same
// sketch also confirms the serial link.
//
// Serial is UART2 on PC11 (TX) and PC12 (RX) -- the pins the USB adapter uses.

void setup() {
  pinMode(PA7, OUTPUT);
  Serial.begin(115200);
  Serial.println("HT5023 Blink: LED on PIN_LED1, one second per state");
}

void loop() {
  digitalWrite(PA7, HIGH);
  Serial.println("LED on");
  delay(1000);

  digitalWrite(PA7, LOW);
  Serial.println("LED off");
  delay(1000);
}
