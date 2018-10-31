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
#define offCounter 100

#include <MIDI.h>
#include <HID.h>
MIDI_CREATE_DEFAULT_INSTANCE();

int note[12] = {
  C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B };  // Pin delle note : 0 -> C , 11 -> B
int octave[4] = {
  Oct1, Oct2, Oct3, Oct4 };         // Pin delle ottave : 0 -> 2 , 3 -> 5
int noteCounter[49] = { 0 };
boolean status[49] = { LOW };         // Array di stato, aggiornato durante ogni ciclo. 0 -> C2 , 11 -> C3 , 23 -> C4 -> , 35 -> C5 , 48 -> C6
boolean flip[49] = { LOW };
boolean buffer = LOW;             // Usato come buffer per lo stato di ogni pin, per non chiamare una lettura ogni volta.
int octBuffer;                 // Usato per non ripetere l'aritmetica ad ogni accesso all'array di stato.
byte noteBuffer;
byte velocity = 100;               // Placeholder.
int channel = 7;                  // Placeholder.


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
}


void loop() {
	scan();
}

void scan() {
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		octBuffer = 12 * cOctave;
		digitalWrite(octave[cOctave], HIGH);


		for (int cNote = 0; cNote < 12; cNote++) {
			if (noteCounter[cNote + octBuffer] > 0) {
				noteCounter[cNote + octBuffer]--;
			}
			else {
				noteCounter[cNote + octBuffer] = offCounter;

				buffer = digitalRead(note[cNote]);

				if (buffer ^ status[cNote + octBuffer]) {
					status[cNote + octBuffer] = buffer;
					flip[cNote + octBuffer] = HIGH;
					noteBuffer = cNote + octBuffer + noteOffset;
					if (buffer == HIGH) {
						MIDI.sendNoteOn(noteBuffer, velocity, 1);
					}
					if (buffer == LOW) {
						MIDI.sendNoteOff(noteBuffer, velocity, 1);
					}
				}
				else {
					flip[cNote + octBuffer] = LOW;
				}
			}

		}
		digitalWrite(octave[cOctave], LOW);
	}
}
