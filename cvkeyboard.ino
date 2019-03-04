#define C 22
#define Db 24
#define D 26
#define Eb 28
#define E 30
#define F 32
#define Gb 34
#define G 36
#define Ab 38
#define A 40
#define Bb 42
#define B 44
#define testLed 13

#define Oct1 12
#define Oct2 9
#define Oct3 8
#define Oct4 10

#define noteOffset 36

//#include <MIDI.h>
//#include <HID.h>
//MIDI_CREATE_DEFAULT_INSTANCE();

typedef struct OctaveStatus {
	bool stat[12];
	int nOct;
} octst;

int note[12] = {
  C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B };  // Note Pins above
int octave[4] = {
  Oct1, Oct2, Oct3, Oct4 };         // Octave Pins above
int ledPins[12]{
  19, 18, 17, 16, 15, 14, 2, 3, 4, 5, 6, 0 };

int clock = 0;
octst buff;

void setup() {
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		pinMode(octave[cOctave], OUTPUT);
	}
	for (int cNote = 0; cNote < 12; cNote++) {
		pinMode(note[cNote], INPUT);
	}
	for (int cLed = 0; cLed < 12; cLed++) {
		pinMode(ledPins[cLed], OUTPUT);
	}
	//  MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200);
	//  nextBeat = millis() + (MINUTE / bpm);
	for (int cLed = 0; cLed < 12; cLed++) {
		digitalWrite(ledPins[cLed], HIGH);
		delay(100);
	}
	for (int cLed = 0; cLed < 12; cLed++) {
		digitalWrite(ledPins[cLed], LOW);
		delay(100);
	}
	pinMode(50, OUTPUT);
}

void loop() {
	for (clock = 0; clock < 4; clock++) {
		digitalWrite(octave[clock], HIGH);
		buff = scan(clock);
		digitalWrite(octave[clock], LOW);
		//clean = clearOct(buff[0], buff[1], buff[2], buff[3], buff[4]);
		debug(buff);
		serialDebug(buff);
	}
}

bool debouncedRead(int pin) {
	if (digitalRead(pin) == HIGH) {
		if (digitalRead(pin) == HIGH) {
			if (digitalRead(pin) == HIGH) {
				if (digitalRead(pin) == HIGH) {
					if (digitalRead(pin) == HIGH) {
						return HIGH;
					}
				}
			}
		}
	}
	return LOW;
}
octst scan(int nOct) {
	int c;
	octst output;

	output.nOct = nOct;

	for (c = 0; c < 12; c++) {
		output.stat[c] = digitalRead(note[c]);
		//   delay(50);
	}
	return output;
}

/*octst clearOct(octst o1, octst o2, octst o3, octst o4, octst o5) {
  octst output;

  output.nOct = o1.nOct;
  for (int c = 0; c < 12; +c++) {
	if (o1.stat[c] && o2.stat[c] && o3.stat[c] && o4.stat[c] && o5.stat[c]) output.stat[c] = HIGH;
	else output.stat[c] = LOW;
  }
  return output;
}*/

void debug(octst input) {
	int c;
	for (c = 0; c < 12; c++) {
		digitalWrite(ledPins[c], input.stat[c]);
	}
	delay(5);
	for (c = 0; c < 12; c++) {
		digitalWrite(ledPins[c], LOW);
	}
}

void serialDebug(octst input) {
	for (int c = 0; c < 12; c++) {
		Serial.print(input.stat[c]);
	}
	Serial.println("");
}

/* debugLed(int c) {
  switch (c) {
  case 0: digitalWrite(2, HIGH);
  break;
  case 1: digitalWrite(3, HIGH);
  break;
  case 2: digitalWrite(4, HIGH);
  break;
  case 3: digitalWrite(2, LOW);
  digitalWrite(3, LOW);
  digitalWrite(4, LOW);
  break;
  }
}*/