#define noteOffset 36
#define DRUMNOTE 60
#define MINUTE 60000
#define MIDICLOCK 0xf8

#include <CapacitiveSensor.h>
#include <MIDI.h>
#include <HID.h>

MIDI_CREATE_DEFAULT_INSTANCE();

typedef struct OctaveStatus {      // This struct is for an octave status. Each bool is for 1 note
	bool stat[12];
	int nOct;
} octst;


// PIN DECLARATIONS
int note[12] = {            // Pins used to read each note (C is 0, B is 11)
  22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44 };
int octave[4] = {           // Pins associated to each octave's contact bar
  12, 9, 8, 10 };
int sendPin[3] = {            // Pins used as sender for capacitive touch buttons
  5, 4, 16 };
int receivePin[3] = {         // Pins used as receiver for capacitive touch buttons
  6, 3, 17 };

// GLOBAL SETTINGS
bool raw;               // Signal is sent when key is detected

	  // PLACEHOLDERS
byte velocity = 100;          // 
byte channel = 1;           // 
int bpm = 360;              // 
unsigned long gate = 50;        // ms of keypress if arpeggiator
unsigned long nextBeat = 0;       // Used to keep track of beats. Useless if receiving MIDI clock.

	  // SYSTEM VARIABLES
int arp = 0;              // Keeps track of last played note if arpeggiating
int midiclock = 0;              // Used to sync with MIDI clock
int semA = 0;              // Basic semaphore implementation with global counter
int semB = 0;
int npressed;             // Number of keys pressed, used to avoid doing anything when no keys are pressed
bool kboard[49];            // Last status of keyboard
bool bCapStat[3];           // Last status of Capacitive Buttons
CapacitiveSensor* bCap[3];


void setup() {
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		pinMode(octave[cOctave], OUTPUT);
	}
	for (int cNote = 0; cNote < 12; cNote++) {
		pinMode(note[cNote], INPUT);
	}
	for (int cButton = 0; cButton < 3; cButton++) {                 // Capacitive Buttons configuration
		bCap[cButton] = new CapacitiveSensor(sendPin[cButton], receivePin[cButton]);  // Initialized
		bCap[cButton]->set_CS_AutocaL_Millis(0xFFFFFFFF);             // No recalibration
		bCap[cButton]->set_CS_Timeout_Millis(200);                  // Timeout set to 200ms (instead of 2s)
		bCapStat[cButton] = LOW;                          // Button starts LOW
	}

	for (int cStat = 0; cStat < 49; cStat++) kboard[cStat] = LOW;         // All keyboard keys start LOW

	MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200);

	pinMode(2, INPUT_PULLUP);                           // Used for RAW switch
}

void loop() {
	sync();

	for (int cButton = 0; cButton < 3; cButton++) {
		bCapStat[cButton] = evalButton(bCap[cButton], bCapStat[cButton], DRUMNOTE + cButton);
	}
	npressed = 0;
	raw = digitalRead(2);
	for (int cOctave = 0; cOctave < 4; cOctave++) {
		digitalWrite(octave[cOctave], HIGH);
		npressed += eval(scan(cOctave));
		digitalWrite(octave[cOctave], LOW);
	}
	if (raw) return;
	if (npressed < 1) return;

	if (semA > 0) {
		semA--;
		arp++;
		while (kboard[arp] == LOW) {
			arp++;
			if (arp == 49) arp = 0;
		}
		playNote(arp, HIGH);
	}
	if (semB > 0) {
		semB--;
		playNote(arp, LOW);
	}
}


octst scan(int nOct) {          // This function reads the 12 note pins and returns a struct
	int c;                //       with 1 bool for each note
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

bool evalButton(CapacitiveSensor* b, bool value, byte note) {
	long sensor = b->capacitiveSensor(1);

	if (sensor > 15) {
		if (value) return HIGH;
		else {
			MIDI.sendNoteOn(note, velocity, (byte)7);
			return HIGH;
		}
	}
	else {
		if (!value) return LOW;
		else {
			MIDI.sendNoteOff(note, velocity, (byte)7);
			return LOW;
		}
	}
}

void sync() {
	if (Serial.available() && Serial.read() == MIDICLOCK) {
		midiclock++;
		if (midiclock == 11 && semA == 0) semA++;
		else if (midiclock == 5 && semB == 0) semB++;
		else if (midiclock == 12) midiclock = 0;
	}
}