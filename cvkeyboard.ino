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
#define offCounter 0
#define MINUTE 60000

#include <MIDI.h>
#include <HID.h>
MIDI_CREATE_DEFAULT_INSTANCE();

int note[12] = {
  C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B };  // Note Pins above
int octave[4] = {
  Oct1, Oct2, Oct3, Oct4 };         // Octave Pins above

int noteCounter[49] = { 0 };
boolean status[49] = { LOW };         
boolean flip[49] = { LOW };
boolean buffer = LOW;

int octBuffer;
byte noteBuffer;


byte velocity = 100;               // Placeholder. Will need something to change it
int channel = 7;                  // Placeholder. Will need something to change it
int bpm = 120;					// Placeholder. Will need something to change it
int gate = 25;					// Placeholder. Will need something to change it

int nextBeat = 0;
int step = 0;



void setup()
{
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		pinMode(octave[cOctave], OUTPUT);
	}
	for (int cNote = 0; cNote < 12; cNote++) {
		pinMode(note[cNote], INPUT);
	}
	MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200);
	nextBeat = millis() + (MINUTE / bpm);
}


void loop() {
	if (millis() < nextBeat) return;
	scan();
	arp();
}

void send() {
	for (int c = 48; c >= 0; c--) {
		if (flip[c] == HIGH) {
			flip[c] = LOW;
			if (noteCounter[c] > 0) {
				noteCounter[c]--;
			}
			else {
				noteCounter[c] = offCounter;
				noteBuffer = c + noteOffset;
				if (status[c] == HIGH) {
					MIDI.sendNoteOn(noteBuffer, velocity, channel);
				}
				else if (status[c] == LOW) {
					MIDI.sendNoteOff(noteBuffer, velocity, channel);
				}
			}
		}
	}
}

void playNote(int c, boolean status) {
	if (status == HIGH) {
		MIDI.sendNoteOn(c + noteOffset, velocity, channel);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(c + noteOffset, velocity, channel);
	}
}

void arp() {
	while (step < 49 && status[step] == LOW) {
		step++;
	}
	if (step == 49) {
		step = 0;
	}
	else {
		playNote(step, HIGH);
		if (gate < millis() - nextBeat) delay(gate - 5);		// 5 ms arbitrarily for the check. Need something more sofisticate
		playNote(step, LOW);
	}
	return;
}

void scan() {
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		octBuffer = 12 * cOctave;
		digitalWrite(octave[cOctave], HIGH);


		for (int cNote = 0; cNote < 12; cNote++) {
			buffer = digitalRead(note[cNote]);

			if (buffer ^ status[cNote + octBuffer]) {
				status[cNote + octBuffer] = buffer;
				flip[cNote + octBuffer] = HIGH;
			}
			else {
				flip[cNote + octBuffer] = LOW;
			}

		}
		digitalWrite(octave[cOctave], LOW);
	}

}

int nPressed() {
	int c, n = 0;
	for (c = 0; c < 49; c++) {
		if (status[c] == HIGH) {
			n++;
		}
	}
}
