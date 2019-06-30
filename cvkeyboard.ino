#include <CapacitiveSensor.h>
#include <MIDI.h>
#include <HID.h>

#define BPQN 24 // Ableton sends 24, VCV rack only one, by standard should be 24?

#define NOTEOffset 36
#define DRUMSHIFT 6
#define drumOffset 60
#define MINUTE 60000
#define MIDICLOCK 0xf8
#define MAXKEYS 48
#define MAXDPAD 3
#define MAXSTEP 16

MIDI_CREATE_DEFAULT_INSTANCE();
                                                      
typedef struct SequencerStep* link;

typedef struct OCTAVEStatus {      // This struct is for an OCTAVE status. Each bool is for 1 NOTE
	bool stat[12];
	int nOct;
} octst;

typedef struct SequencerStep {
	bool clean = LOW;
	bool kboard_s[MAXKEYS];
	bool dpad_s[MAXDPAD];
	unsigned short stepnumber;
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
  6, 1, 17 };
int LEDS[4] = {         // Pins used for leds
  21, 20, 19, 18 };
int OW = 2;					// Pin used for overwrite switch
int DEL = -1;				// Pin used for delete button
int ADD = 14;				// Pin used for add button

		// GLOBAL SETTINGS
//bool overwrite;               // Step content is overwritten with pressed keys, could not be needed

		// PLACEHOLDERS
byte velocity = 100;          // 
int bpm = 360;              // 


		// SEQUENCER POINTERS AND RELATED ARRAYS
link head[6];
link current[6];
link previous;
unsigned short nstep[6];				// Keeps track of the sequencer steps
bool mute[6];
byte channel;           // Current selected channel. Drums are shifted of DRUMSHIFT channels (so channels can only be 6)

		// SYSTEM VARIABLES
int arp = 0;              // Keeps track of last played NOTE if arpeggiating
int midiclock = 0;              // Used to sync with MIDI clock
bool add_step = LOW;			// This is used to remember the addition of a step
bool del_step = LOW;			// This is used to remember the deletion of a step
bool chan_up = LOW;				// Only for now because I have few buttons :C
int sem_beat = 0;              // Basic semaphore used to sync with MIDI beat
int sem_gate = 0;				// Basic semaphore used for gate timing
unsigned long last_gate = 0;			// Gate start time for last sequencer step
unsigned long gate_length = 100;        // ms of keypress if arpeggiator
bool dpadhit = LOW;				// If any drum pad has been hit in this cycle, this is true
int npressed;             // Number of keys pressed, used to avoid doing anything when no keys are pressed
bool kboard[MAXKEYS];            // Last status of keyboard
bool dpad[MAXDPAD];           // Last status of Capacitive Buttons
CapacitiveSensor* bCap[MAXDPAD];


void setup() {
	for (int cOCTAVE = 0; cOCTAVE < 4; cOCTAVE++) pinMode(OCTAVE[cOCTAVE], OUTPUT);
	for (int cNOTE = 0; cNOTE < 12; cNOTE++) pinMode(NOTE[cNOTE], INPUT);
	for (int cLED = 0; cLED < 4; cLED++) pinMode(LEDS[cLED], OUTPUT);
	for (int cButton = 0; cButton < MAXDPAD; cButton++) {                 // Capacitive Buttons configuration
		bCap[cButton] = new CapacitiveSensor(SEND[cButton], RECEIVE[cButton]);  // Initialized
		bCap[cButton]->set_CS_AutocaL_Millis(0xFFFFFFFF);             // No recalibration
		bCap[cButton]->set_CS_Timeout_Millis(1);                  // Timeout set to 20ms (instead of 2s)
		dpad[cButton] = LOW;                          // Button starts LOW
	}

	for (int cStat = 0; cStat < MAXKEYS; cStat++) kboard[cStat] = LOW;         // All keyboard keys start LOW

	MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200);

	pinMode(OW, INPUT_PULLUP);                           // Used for overwrite switch
	pinMode(ADD, INPUT_PULLUP);                           // Used for overwrite switch

	for (int i = 0; i < 6; i++){
		current[i] = NULL;
		head[i] = NULL;
		nstep[i] = 0;
		mute[i] = LOW;
	}
	channel = (byte) 1;

	for (int i = 0; i < 16; i++) {						// Boot up fancyness!
		display(i);
		delay(200);
	}

	// ONLY FOR DEBUG
	for (int chan=1; chan <= 6; chan++) for (int i=0; i<16; i++) insertStep((byte) chan - 1);

	display(10);
}

void loop() {
	sync();
	// add_step = (add_step || !digitalRead(ADD));
	// del_step = (del_step || !digitalRead(DEL));
	chan_up = (chan_up || !digitalRead(ADD));

	if (sem_beat > 0) {
		sem_beat--;

		if (sem_gate > 0) {		// If step was shorter than gate, close all open notes before next step
			sem_gate--;
			for (int chan = 0; chan < 6; chan++) {
				if (mute[chan]) continue;
				for (int i = 0; i < MAXKEYS; i++) if (current[chan]->kboard_s[i] && !kboard[i]) playNote(i, !current[chan]->kboard_s[i], (byte) chan+1);
				for (int i = 0; i < MAXDPAD; i++) if (current[chan]->dpad_s[i] && !dpad[i]) playDrum(i, !current[chan]->dpad_s[i], (byte) chan+1);
			}		
		}

		if (add_step && !del_step) {
			add_step = LOW;
			if (nstep[channel-1] < MAXSTEP) insertStep(channel-1);
		}
		if (del_step && !add_step) {
			del_step = LOW;
			if (nstep[channel-1] < MAXSTEP) deleteStep(channel-1);
		}
		if (add_step && del_step) {
			add_step = LOW;
			del_step = LOW;
		}

		// ONLY FOR NOW because I don't have enough buttons :C
		if (chan_up) {
			chan_up = LOW;
			channel++;
			if (channel > 3) channel = (byte) 1;
		}
		
		nextStep();
		display(current[channel-1]->stepnumber);
		for (int chan = 0; chan < 6; chan++) {
			if (mute[chan]) continue;
			if (current[chan] != NULL) { // Play all step notes and begin counting for gate
				for (int i = 0; i < MAXKEYS; i++) if (current[chan]->kboard_s[i] && !kboard[i]) playNote(i, current[chan]->kboard_s[i], (byte) chan+1);
				for (int i = 0; i < MAXDPAD; i++) if (current[chan]->dpad_s[i] && !dpad[i]) playDrum(i, current[chan]->dpad_s[i], (byte) chan+1);
			}
		}
		last_gate = millis();
		sem_gate++;
	}
	
	if (sem_gate > 0 && (millis() - last_gate) > gate_length) {
		sem_gate--;
		for (int chan = 0; chan < 6; chan++) {
			if (mute[chan]) continue;
			for (int i = 0; i < MAXKEYS; i++) if (current[chan]->kboard_s[i] && !kboard[i]) playNote(i, !current[chan]->kboard_s[i], (byte) chan+1);
			for (int i = 0; i < MAXDPAD; i++) if (current[chan]->dpad_s[i] && !dpad[i]) playDrum(i, !current[chan]->dpad_s[i], (byte) chan+1);
		}		
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
		if (npressed > 0) for (int i = 0; i < MAXKEYS; i++) current[channel-1]->kboard_s[i] = kboard[i];
		if (dpadhit) for (int i = 0; i < MAXDPAD; i++) current[channel-1]->dpad_s[i] = dpad[i];
		current[channel-1]->clean = LOW;
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

void display(int number){
	for(int i = 0; i < 4; i++) {
		digitalWrite(LEDS[i], number & (unsigned short) 1);
		number = number >> 1;
	}
}

bool evalButton(CapacitiveSensor* b, bool value, int note_number) {
	long sensor = b->capacitiveSensor(1);
	// Serial.println(sensor);

	if (sensor > 15) {
		if (value) return HIGH;
		else {
			playDrum(note_number, HIGH, channel);
			return HIGH;
		}
	}
	else {
		if (!value) return LOW;
		else {
			playDrum(note_number, LOW, channel);
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
			playNote(c + sNOTE, input.stat[c], channel);
			kboard[c + sNOTE] = input.stat[c];
		}
		if (kboard[c + sNOTE] == HIGH) pressed++;
	}
	return pressed;
}

void playNote(int c, bool status, byte chan) {
	byte n = c + NOTEOffset;
	if (status == HIGH) {
		MIDI.sendNoteOn(n, velocity, chan);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(n, velocity, chan);
	}
}

void playDrum(int c, bool status, byte chan) {
	byte n = c + drumOffset;
	if (status == HIGH) {
		MIDI.sendNoteOn(n, velocity, chan + (byte) DRUMSHIFT);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(n, velocity, chan + (byte) DRUMSHIFT);
	}
}

// Sync functions

void sync() {
	if (Serial.available()) {
		if (Serial.read() == MIDICLOCK) {
			//sem_beat++;
			midiclock++;
			if (midiclock == BPQN){
				midiclock = 0;
				sem_beat++;
			}
		}
	}
}

// List management functions

link newStep() {
	return (link)malloc(sizeof(struct SequencerStep));
}

bool insertStep(byte chan) {
	link newS = newStep();
	link buffer;

	if (newS == NULL) return LOW;

	for (int i = 0; i < MAXKEYS; i++) newS->kboard_s[i] = LOW;
	for (int i = 0; i < MAXDPAD; i++) newS->dpad_s[i] = LOW;

	if (head[chan] == NULL) {
		newS->next = newS;
		newS->stepnumber = (unsigned short) 0;
		current[chan] = newS;
		head[chan] = newS;
		nstep[chan] = 1;
	}
	else {
		newS->stepnumber = nstep[chan];
		buffer = current[chan];
		while (buffer->next != head[chan]) buffer = buffer->next;
		buffer->next = newS;
		newS->next = head[chan];
		nstep[chan]++;
	}
	return HIGH;
}

void nextStep() {
	for (int chan=0; chan < 6; chan++) {
		if (head[chan] == NULL) continue;
		current[chan] = current[chan]->next;
	}
}

bool deleteStep(byte chan) {
	if (nstep[chan] < 1) return LOW;

	if (!current[chan]->clean) {
		for (int i = 0; i < MAXKEYS; i++) current[chan]->kboard_s[i] = LOW;
		for (int i = 0; i < MAXDPAD; i++) current[chan]->dpad_s[i] = LOW;
		current[chan]->clean = HIGH;
		return LOW;
	}

	if (nstep[chan] == 1) {
		free(current[chan]);
		head[chan] = NULL;
		current[chan] = NULL;
	}
	else {
		link buffer = current[chan];
		while (buffer->next != current[chan]) buffer = buffer->next;
		buffer->next = current[chan]->next;
		if (current[chan] == head[chan]) {
			head[chan] = head[chan]->next;
			int i = 0;
			buffer = head[chan];
			do {
				buffer->stepnumber = i;
				buffer = buffer->next;
				i++;
			} while (buffer != head[chan]);
		}
		else {
			buffer = buffer->next;
			while (buffer != head[chan]) {
				buffer->stepnumber--;
				buffer = buffer->next;
			}
		}
		free(current[chan]);
		buffer = buffer->next;
	}
	nstep[chan]--;
	return HIGH;
}