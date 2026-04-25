/*
  totem_slow_phase

  Slow phase of 4 LEDs for totem base cube.

  D0 to D3 drive LEDs on each of the 4 panels, slowly phasing brightness around the cube.
*/

int d0, d1, d2, d3 = 0;
int d0_dir, d1_dir, d2_dir, d3_dir = 1;


void step_phase() {
  d0 += d0_dir;
  d1 += d1_dir;
  d2 += d2_dir;
  d3 += d3_dir;

  analogWrite(D0, d0);
  analogWrite(D1, d1);
  analogWrite(D2, d2);
  analogWrite(D3, d3);

  // analogWrite(LED_RED, d0);
  // analogWrite(LED_GREEN, d1);
  // analogWrite(LED_BLUE, d2);

  if (d0 == 0 || d0 == 255) d0_dir = -d0_dir;
  if (d1 == 0 || d1 == 255) d1_dir = -d1_dir;
  if (d2 == 0 || d2 == 255) d2_dir = -d2_dir;
  if (d3 == 0 || d3 == 255) d3_dir = -d3_dir;
}

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  pinMode(D0, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D3, OUTPUT);

  // Init phase (all increasing)
  d0 = 0;
  d1 = 64;
  d2 = 128;
  d3 = 192;
  d0_dir = 1;
  d1_dir = 1;
  d2_dir = 1;
  d3_dir = 1;

  // Onboard LED off
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

// the loop function runs over and over again forever
void loop() {
  // digitalWrite(D0, HIGH);
  // digitalWrite(D1, HIGH);
  // digitalWrite(D2, HIGH);
  // digitalWrite(D3, HIGH);
  //delay(100);                      // wait for a second

  // digitalWrite(LED_RED, LOW);   // change state of the LED by setting the pin to the LOW voltage level
  // digitalWrite(LED_GREEN, LOW);   // change state of the LED by setting the pin to the LOW voltage level
  // digitalWrite(LED_BLUE, LOW);   // change state of the LED by setting the pin to the LOW voltage level
  //delay(100);                      // wait for a second

  step_phase();
  delay(10);
}
