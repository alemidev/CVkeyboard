#include <CapacitiveSensor.h>
#include <MIDI.h>
#include <HID.h>

#define NOTEOffset 36
#define drumOffset 60
#define MINUTE 60000
#define MIDICLOCK 0xf8
#define MAXKEYS 48
#define MAXDPAD 3

MIDI_CREATE_DEFAULT_INSTANCE();
                                                      
typedef struct SequencerStep* link;

typedef struct OCTAVEStatus {      // This struct is for an OCTAVE status. Each bool is for 1 NOTE
	bool stat[12];
	int nOct;
} octst;

typedef struct SequencerStep {
	bool kboard_s[MAXKEYS];
	bool dpad_s[MAXDPAD];
	link next;
} step;

// PIN DECLARATIONS
int NOTE[12] = {            // Pins used to read each note (C is 0, B is 11)
  22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44 };
int OCTAVE[4] = {           // Pins associated to each OCTAVE's contact bar
  12, 9, 8, 10 };
int SEND[3] = {            // Pins used as sender for capacitive touch buttons
  5, 4, 16 };
int RECEIVE[3] = {         // Pins used as receiver for capacitive touch buttons
  6, 3, 17 };
int OW = 2;					// Pin used for overwrite switch
int DEL = -1;				// Pin used for delete button
int ADD = 14;				// Pin used for add button

		// GLOBAL SETTINGS
//bool overwrite;               // Step content is overwritten with pressed keys, could not be needed

		// PLACEHOLDERS
byte velocity = 100;          // 
byte channel = 1;           // 
int bpm = 360;              // 


		// SEQUENCER POINTERS
link head, tail, current;

		// SYSTEM VARIABLES
int nstep = 0;				// Keeps track of the sequencer steps
int arp = 0;              // Keeps track of last played NOTE if arpeggiating
int midiclock = 0;              // Used to sync with MIDI clock
int sem_beat = 0;              // Basic semaphore used to sync with MIDI beat
int sem_gate = 0;				// Basic semaphore used for gate timing
unsigned long last_gate = 0;			// Gate start time for last sequencer step
unsigned long gate_length = 200;        // ms of keypress if arpeggiator
bool dpadhit = LOW;				// If any drum pad has been hit in this cycle, this is true
int npressed;             // Number of keys pressed, used to avoid doing anything when no keys are pressed
bool kboard[MAXKEYS];            // Last status of keyboard
bool dpad[MAXDPAD];           // Last status of Capacitive Buttons
CapacitiveSensor* bCap[MAXDPAD];


void setup() {
	for (int cOCTAVE = 0; cOCTAVE < 4; cOCTAVE++) {
		pinMode(OCTAVE[cOCTAVE], OUTPUT);
	}
	for (int cNOTE = 0; cNOTE < 12; cNOTE++) {
		pinMode(NOTE[cNOTE], INPUT);
	}
	for (int cButton = 0; cButton < MAXDPAD; cButton++) {                 // Capacitive Buttons configuration
		bCap[cButton] = new CapacitiveSensor(SEND[cButton], RECEIVE[cButton]);  // Initialized
		bCap[cButton]->set_CS_AutocaL_Millis(0xFFFFFFFF);             // No recalibration
		bCap[cButton]->set_CS_Timeout_Millis(200);                  // Timeout set to 200ms (instead of 2s)
		dpad[cButton] = LOW;                          // Button starts LOW
	}

	for (int cStat = 0; cStat < MAXKEYS; cStat++) kboard[cStat] = LOW;         // All keyboard keys start LOW

	MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200);

	pinMode(OW, INPUT_PULLUP);                           // Used for overwrite switch
	pinMode(ADD, INPUT_PULLUP);                           // Used for overwrite switch
}

void loop() {
	sync();
	if (sem_beat > 0) {
		sem_beat--;
		if (sem_gate > 0) {		// If step was shorter than gate, close all open notes before next step
			sem_gate--;
			for (int i = 0; i < MAXKEYS; i++) if (current->kboard_s[i]) playNOTE(i, !current->kboard_s[i]);
			for (int i = 0; i < MAXDPAD; i++) if (current->dpad_s[i]) playDrum(i, !current->dpad_s[i]);
		}
		if (digitalRead(ADD) && digitalRead(OW)) insertStep();
		if (digitalRead(ADD) && !digitalRead(OW)) deleteStep(); // Placeholder because I miss a button
		nextStep();
		if (current != NULL) { // Play all step notes and begin counting for gate
			for (int i = 0; i < MAXKEYS; i++) if (current->kboard_s[i]) playNOTE(i, current->kboard_s[i]);
			for (int i = 0; i < MAXDPAD; i++) if (current->dpad_s[i]) playDrum(i, current->dpad_s[i]);
			last_gate = millis();
			sem_gate++;
		}
	}
	if (sem_gate > 0 && (millis() - last_gate) > gate_length) {
		sem_gate--;
		for (int i = 0; i < MAXKEYS; i++) if (current->kboard_s[i]) playNOTE(i, !current->kboard_s[i]);
		for (int i = 0; i < MAXDPAD; i++) if (current->dpad_s[i]) playDrum(i, !current->dpad_s[i]);
	}
	dpadhit = LOW;
	for (int cButton = 0; cButton < MAXDPAD; cButton++) {
		dpad[cButton] = evalButton(bCap[cButton], dpad[cButton], cButton);
		dpadhit = (dpad[cButton] || dpadhit);
	}

	npressed = 0;
	for (int cOCTAVE = 0; cOCTAVE < 4; cOCTAVE++) {
		digitalWrite(OCTAVE[cOCTAVE], HIGH);
		npressed += eval(scan(cOCTAVE));
		digitalWrite(OCTAVE[cOCTAVE], LOW);
	}

	if (digitalRead(OW)) {
		if (npressed > 0) for (int i = 0; i < MAXKEYS; i++) current->kboard_s[i] = kboard[i];
		if (dpadhit) for (int i = 0; i < MAXDPAD; i++) current->dpad_s[i] = dpad[i];
	}
}

// Hardware specific functions

octst scan(int nOct) {          // This function reads the 12 NOTE pins and returns a struct
	int c;                //       with 1 bool for each NOTE
	octst output;

	output.nOct = nOct;

	for (c = 0; c < 12; c++) {
		output.stat[c] = digitalRead(NOTE[c]);
	}
	return output;
}

bool evalButton(CapacitiveSensor* b, bool value, int note_number) {
	long sensor = b->capacitiveSensor(1);

	if (sensor > 15) {
		if (value) return HIGH;
		else {
			playDrum(note_number, HIGH);
			return HIGH;
		}
	}
	else {
		if (!value) return LOW;
		else {
			playDrum(note_number, LOW);
			return LOW;
		}
	}
}

// NOTE Functions

int eval(octst input) {
	int pressed = 0;
	int sNOTE = input.nOct * 12;

	for (int c = 0; c < 12; c++) {
		if (input.stat[c] ^ kboard[c + sNOTE]) {
			playNOTE(c + sNOTE, input.stat[c]);
			kboard[c + sNOTE] = input.stat[c];
		}
		if (kboard[c + sNOTE] == HIGH) pressed++;
	}
	return pressed;
}

void playNOTE(int c, bool status) {
	byte n = c + NOTEOffset;
	if (status == HIGH) {
		MIDI.sendNoteOn(n, velocity, channel);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(n, velocity, channel);
	}
}

void playDrum(int c, bool status) {
	byte n = c + drumOffset;
	if (status == HIGH) {
		MIDI.sendNoteOn(n, velocity, (byte)7);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(n, velocity, (byte)7);
	}
}

// Sync functions

void sync() {
	if (Serial.available() && Serial.read() == MIDICLOCK) {
		midiclock++;
		if (midiclock == 0 && sem_beat == 0) sem_beat++;
		else if (midiclock == 24) midiclock = 0;
	}
}

// List management functions

link newStep() {
	return (link)malloc(sizeof(struct SequencerStep));
}

bool insertStep() {
	link newS = newStep();
	if (newS == NULL) {
		free(newS);
		return LOW;
	}

	for (int i = 0; i < MAXKEYS; i++) newS->kboard_s[i] = kboard[i];
	for (int i = 0; i < MAXDPAD; i++) newS->dpad_s[i] = dpad[i];

	if (nstep == 0) {
		newS->next = newS;
		current = newS;
		head = newS;
	}
	else {
		newS->next = current->next;
		current->next = newS;
	}
	nstep++;
	return HIGH;
}

void nextStep() {
	current = current->next;
}

bool deleteStep() {
	if (nstep < 1) return LOW;

	if (nstep == 1) {
		free(current);
		head = NULL;
		current = NULL;
	}
	else {
		link buffer = current->next->next;
		free(current->next);
		current->next = buffer;
	}
	nstep--;
	return HIGH;
}