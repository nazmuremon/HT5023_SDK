// SerialCommand -- a small command interpreter, and a working demonstration of
// the Arduino API this core supports.
//
// Type a command into the Flasher's send box (or any serial terminal at
// 115200) and press Enter:
//
//   help              list the commands
//   led on|off|blink  drive the user LED
//   echo <text>       send <text> back
//   add <a> <b>       add two numbers
//   uptime            milliseconds since reset
//
// It leans on String, Stream and Print exactly the way a third-party library
// would, so if this runs, ordinary Arduino code will too.
//
// Note on this part's UART: the receiver holds a single byte and has no FIFO.
// Printing while characters are still arriving loses them, so the line is read
// first and answered afterwards -- worth copying in your own sketches.

static String line;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  line.reserve(64);

  Serial.println();
  Serial.println("HT5023 SerialCommand ready -- type 'help'");
}

static void printHelp() {
  Serial.println("commands: help | led on|off|blink | echo <text> | "
                 "add <a> <b> | uptime");
}

static void blinkTwice() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(120);
    digitalWrite(LED_BUILTIN, LOW);
    delay(120);
  }
}

static void handle(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.equalsIgnoreCase("help")) {
    printHelp();
  } else if (cmd.startsWith("led ")) {
    const String what = cmd.substring(4);
    if (what == "on") {
      digitalWrite(LED_BUILTIN, HIGH);
      Serial.println("led: on");
    } else if (what == "off") {
      digitalWrite(LED_BUILTIN, LOW);
      Serial.println("led: off");
    } else if (what == "blink") {
      blinkTwice();
      Serial.println("led: blinked twice");
    } else {
      Serial.println("led: expected on, off or blink");
    }
  } else if (cmd.startsWith("echo ")) {
    Serial.print("echo: ");
    Serial.println(cmd.substring(5));
  } else if (cmd.startsWith("add ")) {
    const String rest = cmd.substring(4);
    const int sp = rest.indexOf(' ');
    if (sp < 0) {
      Serial.println("add: needs two numbers");
    } else {
      const long a = rest.substring(0, sp).toInt();
      const long b = rest.substring(sp + 1).toInt();
      Serial.print("sum: ");
      Serial.println(a + b);
    }
  } else if (cmd.equalsIgnoreCase("uptime")) {
    Serial.print("uptime: ");
    Serial.print(millis());
    Serial.println(" ms");
  } else {
    Serial.print("unknown command: ");
    Serial.println(cmd);
    printHelp();
  }
}

void loop() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (line.length() > 0) {
        const String cmd = line;
        line = "";
        handle(cmd);
      }
    } else if (line.length() < 120) {
      line += c;
    }
  }
}
