void setup() {
  Serial.begin(115200);
  pinMode(4, INPUT_PULLUP);
}

void loop() {
  Serial.println(digitalRead(4));
  delay(200);
}
