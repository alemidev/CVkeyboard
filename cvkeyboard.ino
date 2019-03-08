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
int bpm = 360;
unsigned long nextBeat = 0;
unsigned long gate = 50;                  //ms of keypress if arpeggiator
int npressed;

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
}

void loop() {
	npressed = 0;
	raw = digitalRead(2);
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		digitalWrite(octave[cOctave], HIGH);
		npressed += eval(scan(cOctave));
		digitalWrite(octave[cOctave], LOW);
	}
	if (raw) {
		nextBeat = millis();
		return;
	}
	if (npressed < 1) return;
	if (millis() >= nextBeat) {
		nextBeat += (MINUTE / bpm);
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

void playNote(int c, bool status) {
	byte n = c + noteOffset;
	if (status == HIGH) {
		MIDI.sendNoteOn(n, velocity, channel);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(n, velocity, channel);
	}
}