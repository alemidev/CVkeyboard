#include <MIDI.h>
#include <HID.h>
#include <Wire.h>
#include <Adafruit_MPR121.h>

#define BPQN 24 // Ableton sends 24, VCV rack only one, by standard should be 24?

#define NOTEOffset 36
#define DRUMSHIFT 6
#define drumOffset 60
#define MINUTE 60000
#define MIDICLOCK 0xf8
#define MAXKEYS 48
#define MAXDPAD 3
#define MAXSTEP 64
#define NBITS 6
#define DEBOUNCE 100

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
int LEDS[NBITS] = {         // Pins used for leds
  5, 4, 3, 14, 16, 18 };
int OW = 2;					// Pin used for overwrite switch
int NEXT = 51;				// Pin used for next step switch
int DEL = 11;				// Capacitive button used for DELETE button
int PLUS = 10;				// Capacitive button used for PLUS button
int MINUS = 9;				// Capacitive button used for MINUS button

		// GLOBAL SETTINGS
int pentathonic[10] = {			// Used to quantize drum notes
	0, 2, 5, 7, 9, 12, 14, 17, 19, 21 };

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
bool plus_step = LOW;			// This is used to remember the addition of a step
bool minus_step = LOW;			// This is used to remember the deletion of a step
bool clear_step = LOW;			// This is used to remember the clearing of a step
bool chan_up = LOW;				// Only for now because I have few buttons :C
bool next_step = LOW;			// Used to wait for a full switch cycle
int sem_beat = 0;              // Basic semaphore used to sync with MIDI beat
int sem_gate = 0;				// Basic semaphore used for gate timing
unsigned long last_gate = 0;			// Gate start time for last sequencer step
unsigned long gate_length = 1000;        // ms of keypress if arpeggiator
unsigned long last_next = 0;
bool dpadhit = LOW;				// If any drum pad has been hit in this cycle, this is true
int npressed;             // Number of keys pressed, used to avoid doing anything when no keys are pressed
bool kboard[MAXKEYS];            // Last status of keyboard
bool dpad[MAXDPAD];           // Last status of Capacitive Buttons
int cap_read = 0;

Adafruit_MPR121 cap = Adafruit_MPR121();

void setup() {
	for (int cOCTAVE = 0; cOCTAVE < 4; cOCTAVE++) pinMode(OCTAVE[cOCTAVE], OUTPUT);
	for (int cNOTE = 0; cNOTE < 12; cNOTE++) pinMode(NOTE[cNOTE], INPUT);
	for (int cLED = 0; cLED < NBITS; cLED++) pinMode(LEDS[cLED], OUTPUT);
	while (!cap.begin(0x5A)) delay(10);					// If MPR121 is not ready, wait for it
	for (int cStat = 0; cStat < MAXKEYS; cStat++) kboard[cStat] = LOW;         // All keyboard keys start LOW
	MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200); 								// Uncomment this if you use Hairless and set baud rate
	pinMode(OW, INPUT_PULLUP);                           // Used for overwrite switch
	pinMode(NEXT, INPUT_PULLUP);                           // Used for overwrite switch
	for (int i = 0; i < 6; i++){
		current[i] = NULL;
		head[i] = NULL;
		nstep[i] = 0;
		mute[i] = LOW;
	}
	channel = (byte) 1;
	for (int i=0; i<NBITS; i++) {
		digitalWrite(LEDS[i], HIGH);
		delay(10);
	}
	last_gate = millis();
	last_next = millis();
	display(0);
}

void loop() {
	sync();
	cap_read = cap.touched();

	if ((cap_read >> 8) & 1) {				// Only for now!
		for (int i=0; i<NBITS; i++) digitalWrite(LEDS[i], LOW);
		digitalWrite(LEDS[channel-1], HIGH);
	} 
	else if (current[channel-1] == NULL) display(analogRead(channel));
	else display(current[channel-1]->stepnumber);

	plus_step = plus_step || ((cap_read >> PLUS) & 1);
	minus_step = minus_step || ((cap_read >> MINUS) & 1);
	clear_step = clear_step || ((cap_read >> DEL) & 1);

	if (chan_up != (bool) ((cap_read >> 8) & 1)) { // Used to increase channel with a button because I don't have a rotary switch (yet!)
		chan_up = (bool) ((cap_read >> 8) & 1);
		if (chan_up == HIGH) {
			channel++;
			if (channel > 6) channel = (byte) 1;
		}
	}

	if (sem_beat > 0) {
		sem_beat--;

		if (sem_gate > 0) {		// If step was shorter than GATE, close all open notes before next step
			sem_gate--;
			for (int chan = 0; chan < 6; chan++) {
				if (mute[chan]) continue;
				for (int i = 0; i < MAXKEYS; i++)
					if (current[chan]->kboard_s[i] && !kboard[i] && !current[chan]->next->kboard_s[i])
						playNote(i, !current[chan]->kboard_s[i], (byte) chan+1);
				for (int i = 0; i < MAXDPAD; i++)
					if (current[chan]->dpad_s[i] && !dpad[i])
						playDrum(i, !current[chan]->dpad_s[i], (byte) chan+1);
			}		
		}

		if (plus_step && minus_step) {
			plus_step = LOW;
			minus_step = LOW;
		}
		if (plus_step) {
			plus_step = LOW;
			if (nstep[channel-1] < MAXSTEP) insertStep(channel-1);
		}
		if (minus_step) {
			minus_step = LOW;
			if (nstep[channel-1] > 0) deleteStep(channel-1);
		}
		if (clear_step) {
			clear_step = LOW;
			if (current[channel-1] != NULL) {
				for (int i = 0; i < MAXKEYS; i++) current[channel-1]->kboard_s[i] = LOW;
				for (int i = 0; i < MAXDPAD; i++) current[channel-1]->dpad_s[i] = LOW;
			}
		}		

		nextStep();								// ALL STEPS INCREMENTED
		display(current[channel-1]->stepnumber);

		for (int chan = 0; chan < 6; chan++) {
			if (mute[chan]) continue;
			if (current[chan] != NULL) { // PLAY all step notes in all unmuted channels
				if (!(npressed > 0 && chan == (int) channel-1))
					for (int i = 0; i < MAXKEYS; i++)
						if (current[chan]->kboard_s[i] && !kboard[i])
							playNote(i, current[chan]->kboard_s[i], (byte) chan+1);
				for (int i = 0; i < MAXDPAD; i++) // Drums are played nonetheless because drums already layered won't overrule
					if (current[chan]->dpad_s[i] && !dpad[i])
						playDrum(i, current[chan]->dpad_s[i], (byte) chan+1);
			}
		}
		last_gate = millis();
		sem_gate++;
	}
	
	if (sem_gate > 0 && (millis() - last_gate) > gate_length) {
		sem_gate--;
		for (int chan = 0; chan < 6; chan++) {
			if (mute[chan]) continue;
			for (int i = 0; i < MAXKEYS; i++)
				if (current[chan]->kboard_s[i] && !kboard[i])
					playNote(i, !current[chan]->kboard_s[i], (byte) chan+1);
			for (int i = 0; i < MAXDPAD; i++)
				if (current[chan]->dpad_s[i] && !dpad[i])
					playDrum(i, !current[chan]->dpad_s[i], (byte) chan+1);
		}		
	}

	dpadhit = LOW;
	for (int cButton = 0; cButton < MAXDPAD; cButton++) {
		if (( 1 & (cap_read >> cButton)) ^ dpad[cButton]) {
			dpad[cButton] = (bool) 1 & (cap_read >> cButton);
			playDrum(cButton, dpad[cButton], channel);
		}
		dpadhit = (dpad[cButton] || dpadhit);
	}

	npressed = 0;
	for (int cOCTAVE = 0; cOCTAVE < 4; cOCTAVE++) {
		digitalWrite(OCTAVE[cOCTAVE], HIGH);
		npressed += eval(scan(cOCTAVE));
		digitalWrite(OCTAVE[cOCTAVE], LOW);
	}

	if (current[channel-1] != NULL && digitalRead(OW)) {
		if (npressed > 0) for (int i = 0; i < MAXKEYS; i++)
			current[channel-1]->kboard_s[i] = kboard[i];
		if (dpadhit) for (int i = 0; i < MAXDPAD; i++)
			current[channel-1]->dpad_s[i] = dpad[i] || current[channel-1]->dpad_s[i]; // Drum hits aren't exclusive!
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
	for(int i = 0; i < NBITS; i++) {
		digitalWrite(LEDS[i], number & (unsigned short) 1);
		number = number >> 1;
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
	// The note is first quantized to a pentathonic and then scaled up to start at C4.
	byte n = (byte) (pentathonic[c] + drumOffset);
	if (status == HIGH) {
		MIDI.sendNoteOn(n, velocity, chan + (byte) DRUMSHIFT);
	}
	else if (status == LOW) {
		MIDI.sendNoteOff(n, velocity, chan + (byte) DRUMSHIFT);
	}
}

// Sync functions

void sync() {
	if (next_step != (bool) !digitalRead(NEXT)) { // Used to increase channel with a button because I don't have a rotary switch (yet!)
		next_step = (bool) !digitalRead(NEXT);
		if (millis() > last_next+DEBOUNCE && next_step == HIGH) {
			last_next = millis();
			sem_beat++;
		}
	}
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
	// Creates a new enpty step and places it as next step in the channel passed as argument
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
		return HIGH;
	}

	newS->stepnumber = current[chan]->stepnumber +1;
	buffer = current[chan]->next;
	current[chan]->next = newS;
	newS->next = buffer;
	nstep[chan]++;
	while (buffer != head[chan]) {
		buffer->stepnumber++;
		buffer = buffer->next;
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

	if (nstep[chan] == 1) {
		free(current[chan]);
		head[chan] = NULL;
		current[chan] = NULL;
		return HIGH;
	}
	
	link buffer = current[chan];
	while (buffer->next != current[chan]) buffer = buffer->next; // Search for previous step
	buffer->next = current[chan]->next;							// Skip step which is being deleted
	if (current[chan] == head[chan]) head[chan] = head[chan]->next;	// If deleting head, head moves forward
	free(current[chan]);										// Step is actually deleted
	nstep[chan]--;												// Decreased the counter
	current[chan] = buffer;										// Current step becomes previous step
	buffer = buffer->next;										// Skip the current step which was just before deleted step
	while(buffer != head[chan]) {								// If this is not the head,
		buffer->stepnumber--;									// 	decrease counter
		buffer = buffer->next;									//	and move on
	}
	return HIGH;
}