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
#define MINUTE 60000

#include <CapacitiveSensor.h>
#include <MIDI.h>
#include <HID.h>

MIDI_CREATE_DEFAULT_INSTANCE();

typedef struct OctaveStatus {
	bool stat[12];
	int nOct;
} octst;

int note[12] = {
  C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B };  // Note Pins above
int octave[4] = {
  Oct1, Oct2, Oct3, Oct4 };           // Octave Pins above

int clock = 0;                  // Used if arp to cycle through notes
octst buff;
bool kboard[49];
bool raw;                   // Global Settings. RAW = signal is sent when key is detected
byte velocity = 100;
byte channel = 1;
byte midi_clock = 0xf8;
byte dataIn;
int bpm = 360;
unsigned long nextBeat = 0;
unsigned long gate = 50;                  //ms of keypress if arpeggiator
int npressed;
bool bu1, bu2, bu3;

CapacitiveSensor b1 = CapacitiveSensor(5, 6);
CapacitiveSensor b2 = CapacitiveSensor(4, 3);
CapacitiveSensor b3 = CapacitiveSensor(16, 17);

void setup() {
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		pinMode(octave[cOctave], OUTPUT);
	}
	for (int cNote = 0; cNote < 12; cNote++) {
		pinMode(note[cNote], INPUT);
	}
	MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200);
	nextBeat = millis() + (MINUTE / bpm);
	pinMode(2, INPUT_PULLUP);
	for (int cStat = 0; cStat < 49; cStat++) kboard[cStat] = LOW;
	nextBeat = 0;

	b1.set_CS_AutocaL_Millis(0xFFFFFFFF);
	b2.set_CS_AutocaL_Millis(0xFFFFFFFF);
	b3.set_CS_AutocaL_Millis(0xFFFFFFFF);
	bu1 = LOW;
	bu2 = LOW;
	bu3 = LOW;
}

void loop() {
	scanButtons();

	npressed = 0;
	raw = digitalRead(2);
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		digitalWrite(octave[cOctave], HIGH);
		npressed += eval(scan(cOctave));
		digitalWrite(octave[cOctave], LOW);
	}
	if (raw) return;
	if (npressed < 1) return;
	dataIn = Serial.read();
	if (dataIn == midi_clock) {
		clock++;
		while (kboard[clock] == LOW) {
			clock++;
			if (clock == 49) clock = 0;
		}
		playNote(clock, HIGH);
		delay(gate);
		playNote(clock, LOW);
	}
}


octst scan(int nOct) {          // This function reads the 12 note pins and returns a struct
	int c;                //      with 1 bool for each note
	octst output;

	output.nOct = nOct;

	for (c = 0; c < 12; c++) {
		output.stat[c] = digitalRead(note[c]);
	}
	return output;
}

int eval(octst input) {
	int pressed = 0;
	int snote = input.nOct * 12;

	for (int c = 0; c < 12; c++) {
		if (input.stat[c] ^ kboard[c + snote]) {
			if (raw) playNote(c + snote, input.stat[c]);
			kboard[c + snote] = input.stat[c];
		}
		if (kboard[c + snote] == HIGH) pressed++;
	}
	return pressed;
}

void serialDebug(octst input) {     // Prints on the Serial Monitor the 12 bits just read
	for (int c = 0; c < 12; c++) {
		Serial.print(input.stat[c]);
	}
	Serial.println("");
}

void playNote(int c, bool status) {
	byte n = c + noteOffset;
	if (status == HIGH) {
		MIDI.sendNoteOn(n, velocity, channel);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(n, velocity, channel);
	}
}

void scanButtons() {
	long sensor1 = b1.capacitiveSensor(1);
	long sensor2 = b2.capacitiveSensor(1);
	long sensor3 = b3.capacitiveSensor(1);

	if (sensor1 > 10) {
		if (!bu1) {
			MIDI.sendNoteOn(95, velocity, 7);
			bu1 = HIGH;
		}
	}
	else {
		if (bu1) {
			MIDI.sendNoteOff(95, velocity, 7);
			bu1 = LOW;
		}
	}


	if (sensor2 > 10) {
		if (!bu2) {
			MIDI.sendNoteOn(97, velocity, 7);
			bu2 = HIGH;
		}
	}
	else {
		if (bu2) {
			MIDI.sendNoteOff(97, velocity, 7);
			bu2 = LOW;
		}
	}


	if (sensor3 > 10) {
		if (!bu3) {
			MIDI.sendNoteOn(99, velocity, 7);
			bu3 = HIGH;
		}
	}
	else {
		if (bu3) {
			MIDI.sendNoteOff(99, velocity, 7);
			bu3 = LOW;
		}
	}
	/*bu1 = evalButton(b1, bu1, 95);
	bu2 = evalButton(b2, bu2, 97);
	bu3 = evalButton(b3, bu3, 99);*/
}

bool evalButton(CapacitiveSensor b, bool value, int note) {
	long sensor = b.capacitiveSensor(1);

	if (sensor > 15) {
		if (value) return HIGH;
		else {
			MIDI.sendNoteOn(note, velocity, 7);
			return HIGH;
		}
	}
	else {
		if (!value) return LOW;
		else {
			MIDI.sendNoteOff(note, velocity, 7);
			return LOW;
		}
	}
}