#include <MIDI.h>
#include <HID.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_MPR121.h>

#define BPQN 24 // Ableton sends 24, VCV rack only one, by standard should be 24?

#define NOTEOffset 36
#define DRUMSHIFT 6
#define drumOffset 60

#define MINUTE 60000
#define INTERVAL 15						// How many minutes between autosave
#define MIDICLOCK 0xf8

#define MAXKEYS 48
#define MAXDPAD 7
#define MAXSTEP 64
#define MAXCHANNEL 6
#define NKEYS 12
#define NOCTAVES 4
#define NBITS 6

#define DEBOUNCE 100

MIDI_CREATE_DEFAULT_INSTANCE();
                                                      
typedef struct SequencerStep* link;

typedef struct SavePoint {
	int headAddr[MAXCHANNEL];
	int tailAddr[MAXCHANNEL];
} save_p;

typedef struct SequencerStep {
	int kboard_s[4];
	int dpad_s;
	unsigned short stepnumber;
	link next;
} step;

save_p saveH;

// PIN DECLARATIONS
int NOTE[NKEYS] = {            // Pins used to read each note (C is 0, B is 11)
  22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44 };
int OCTAVE[NOCTAVES] = {           // Pins associated to each OCTAVE's contact bar
  12, 9, 8, 10 };
int LEDS[NBITS] = {         // Pins used for leds
  5, 4, 2, 14, 16, 18 };
int OW = 3;					// Pin used for overwrite switch
int NEXT = 51;				// Pin used for next step switch
int DEL = 11;				// Capacitive button used for DELETE button
int PLUS = 10;				// Capacitive button used for PLUS button
int MINUS = 9;				// Capacitive button used for MINUS button
int ARP = 7;				// Capacitive button used for ARP button

		// USEFUL ITERABLES
int pentathonic[10] = {			// Used to quantize drum notes
	0, 2, 5, 7, 9, 12, 14, 17, 19, 21 };
int loadingDisplay[6] = {
	1, 3, 7, 15, 31, 63};

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
int arp[2];             		// arp[0] = OCTAVE, arp[1] = KEY (arp[0] for iterations, arp[1] for shifting)
int midiclock = 0;              // Used to sync with MIDI clock
bool arpeggiating = LOW;		// Goes HIGH if the user is requesting an arpeggio
bool plus_step = LOW;			// This is used to remember the addition of a step
bool minus_step = LOW;			// This is used to remember the deletion of a step
bool clear_step = LOW;			// This is used to remember the clearing of a step
bool chan_up = LOW;				// Only for now because I have few buttons :C
bool next_step = LOW;			// Used to wait for a full switch cycle
bool overwrite = LOW;
int sem_beat = 0;              // Basic semaphore used to sync with MIDI beat
int sem_gate = 0;				// Basic semaphore used for gate timing
unsigned long last_gate;			// Gate start time for last sequencer step
unsigned long last_next;
unsigned long last_save;
unsigned long gate_length = 200;        // ms of keypress if arpeggiator
bool dpadhit = LOW;				// If any drum pad has been hit in this cycle, this is true
int npressed;             	// Number of keys pressed, used to avoid doing anything when no keys are pressed
int kboard[4];            	// Last status of keyboard
int dpad;           		// Last status of Capacitive Buttons
int cap_read;
int difference = 0;			// Used in many places, might as well be a global variable

Adafruit_MPR121 cap = Adafruit_MPR121();

void setup() {
	display(loadingDisplay[0]);
	for (int cOCTAVE = 0; cOCTAVE < NOCTAVES; cOCTAVE++) pinMode(OCTAVE[cOCTAVE], OUTPUT);
	for (int cNOTE = 0; cNOTE < NKEYS; cNOTE++) pinMode(NOTE[cNOTE], INPUT);
	for (int cLED = 0; cLED < NBITS; cLED++) pinMode(LEDS[cLED], OUTPUT);
	pinMode(OW, INPUT_PULLUP);                           // Used for overwrite switch
	pinMode(NEXT, INPUT_PULLUP);
	display(loadingDisplay[1]);
	MIDI.begin(MIDI_CHANNEL_OFF);
	Serial.begin(115200); 								// Uncomment this if you use Hairless and set baud rate
	display(loadingDisplay[2]);
	for (int i = 0; i < 6; i++){
		current[i] = NULL;
		head[i] = NULL;
		nstep[i] = 0;
		mute[i] = LOW;
	}
	display(loadingDisplay[3]);
	for (int cOCTAVE = 0; cOCTAVE < NOCTAVES; cOCTAVE++) kboard[cOCTAVE] = 0;
	dpad = 0;
	arp[0] = 0;
	arp[1] = 0;
	cap_read = 0;
	channel = (byte) 1;
	display(loadingDisplay[4]);
	while (!cap.begin(0x5A)) delay(10);					// If MPR121 is not ready, wait for it
	display(loadingDisplay[5]);
	loadAll();
	last_save = millis();
	last_gate = millis();
	last_next = millis();
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

	plus_step = plus_step || (bool) ((cap_read >> PLUS) & 1);
	minus_step = minus_step || (bool) ((cap_read >> MINUS) & 1);
	clear_step = clear_step || (bool) ((cap_read >> DEL) & 1);
	arpeggiating = (bool) ((cap_read >> ARP) & 1);
	overwrite = digitalRead(OW);

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
			if (arpeggiating) playNote((arp[0]*NKEYS)+arp[1], LOW, channel);
			for (int chan = 0; chan < 6; chan++) {
				if (current[chan] == NULL) continue;
				for (int i = 0; i < NOCTAVES; i++)
					for (int j = 0; j < NKEYS; j++) // IF note was played AND user is not playing on this channel AND this note is not kept played
						if (((current[chan]->kboard_s[i] >> j) & 1) && !(chan+1 != channel && ((kboard[i]>>j) & 1)) && !((current[chan]->next->kboard_s[i] >> j) & 1))
							playNote((i*NKEYS)+j, LOW, (byte) chan+1);
			
				for (int i = 0; i < MAXDPAD; i++)
					if (((current[chan]->dpad_s >> i) & 1) && !(chan+1 != channel && ((dpad>>i) & 1)))
						playDrum(i, LOW, (byte) chan+1);
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
				for (int i = 0; i < NOCTAVES; i++) current[channel-1]->kboard_s[i] = 0;
				current[channel-1]->dpad_s = 0;
			}
		}		

		nextStep();								// ALL STEPS INCREMENTED
		display(current[channel-1]->stepnumber);

		if (arpeggiating) {
			while (npressed > 0) {
				arp[1]++;
				if (arp[1] == NKEYS) {
					arp[1] = 0;
					arp[0]++;
				}
				if (arp[0] == NOCTAVES) arp[0] = 0;

				if ((kboard[arp[0]] >> arp[1]) & 1) {
					playNote((arp[0]*NKEYS)+arp[1], HIGH, channel);
					if (overwrite && current[channel-1] != NULL) {
						for (int i=0; i<NOCTAVES; i++) current[channel-1]->kboard_s[i] = 0;
						current[channel-1]->kboard_s[arp[0]] = current[channel-1]->kboard_s[arp[0]] | (1 << arp[1]);
					}
					break;
				}
			}
		}

		for (int chan = 0; chan < 6; chan++) {
			if (mute[chan]) continue;
			if (current[chan] != NULL) { // PLAY all step notes in all unmuted channels
				for (int i = 0; i < NOCTAVES; i++)
					for (int j = 0; j < NKEYS; j++)
						if (((current[chan]->kboard_s[i] >> j) & 1) && !(chan+1 == channel && npressed > 0))
							playNote((i*NKEYS)+j, HIGH, (byte) chan+1);

				for (int i = 0; i < MAXDPAD; i++)	// Drums are played nonetheless because drums already layered won't overrule
					if ((current[chan]->dpad_s >> i) & 1)
						playDrum(i, HIGH, (byte) chan+1);
			}
		}
		last_gate = millis();
		sem_gate++;
	}
	
	if (sem_gate > 0 && (millis() - last_gate) > gate_length) {
		sem_gate--;
		if (arpeggiating) playNote((arp[0]*NKEYS)+arp[1], LOW, channel);
		for (int chan = 0; chan < 6; chan++) {
			if (current[chan] == NULL) continue;
			for (int i = 0; i < NOCTAVES; i++)
				for (int j = 0; j < NKEYS; j++)
					if (((current[chan]->kboard_s[i] >> j) & 1) && !(chan+1 != channel && ((kboard[i]>>j) & 1)))
						playNote((i*NKEYS)+j, LOW, (byte) chan+1);
			
			for (int i = 0; i < MAXDPAD; i++)
				if (((current[chan]->dpad_s >> i) & 1) && !(chan+1 != channel && ((dpad>>i) & 1)))
					playDrum(i, LOW, (byte) chan+1);
		}		
	}

	dpadhit = LOW;
	difference = dpad ^ cap_read;
	for (int c = 0; c < MAXDPAD; c++) {
		if ((difference>>c) & 1) playDrum(c, ((cap_read>>c) & 1), channel);
		if (dpadhit || ((cap_read>>c) & 1)) dpadhit = HIGH;
		if (difference != 0)  dpad = cap_read;
	}

	npressed = 0;
	for (int cOCTAVE = 0; cOCTAVE < 4; cOCTAVE++) {
		digitalWrite(OCTAVE[cOCTAVE], HIGH);
		npressed += eval(scan(), cOCTAVE);
		digitalWrite(OCTAVE[cOCTAVE], LOW);
	}

	if (current[channel-1] != NULL && overwrite) {
		if (!arpeggiating && npressed > 0)
			for (int i = 0; i < NOCTAVES; i++) {
				difference = kboard[i] ^ current[channel-1]->kboard_s[i];
				if (difference != 0) current[channel-1]->kboard_s[i] = kboard[i];
			}
		if (dpadhit) current[channel-1]->dpad_s = current[channel-1]->dpad_s | dpad; // Drum hits aren't exclusive!
	}
}

// Hardware specific functions

int scan() {          // This function reads the 12 NOTE pins and returns a struct
	int output = 0;
	for (int c = 0; c < NKEYS; c++) {
		if (digitalRead(NOTE[c])) output = output | (1<<c);
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

int eval(int input, int nOct) {
	int pressed = 0;
	int sNOTE = nOct * 12;
	difference = kboard[nOct] ^ input;

	for (int c = 0; c < 12; c++) {
		if (!arpeggiating && ((difference>>c) & 1)) playNote(c + sNOTE, ((input>>c) & 1), channel);
		if (((input>>c) & 1)) pressed++;
	}
	if (difference != 0) kboard[nOct] = input;
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

	if (millis() > last_save + (unsigned long) MINUTE*INTERVAL) {
		saveAll();
		last_save = millis();
	}

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
			if (midiclock == BPQN) {
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

	if (newS == NULL) {
		display(63);
		delay(500);
		return LOW;
	}

	for (int i = 0; i < NOCTAVES; i++) newS->kboard_s[i] = 0;
	newS->dpad_s = 0;

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
	
	int c = 0;
	buffer = head[chan];

	buffer->stepnumber = c;
	c++;
	buffer = buffer->next;
	while(buffer != head[chan]) {
		buffer->stepnumber = c;
		c++;
		buffer = buffer->next;
	}
	nstep[chan] = c;

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
	current[chan] = buffer;										// Current step becomes previous step

	int c = 0;
	buffer = head[chan];

	buffer->stepnumber = c;
	c++;
	buffer = buffer->next;
	while(buffer != head[chan]) {
		buffer->stepnumber = c;
		c++;
		buffer = buffer->next;
	}
	nstep[chan] = c;
	return HIGH;
}

// SAVING FUNCTIONS

void saveAll() {
	int currAddr = (int) sizeof(save_p);
	link buffer;

	for (int c=0; c<MAXCHANNEL; c++) {
		display(loadingDisplay[c]);
		if (current[c] == NULL) {
			saveH.headAddr[c] = -1;
			saveH.tailAddr[c] = -1;
			continue;
		}
		buffer = head[c];
		saveH.headAddr[c] = currAddr;
		currAddr = saveStep(buffer, currAddr);
		buffer = buffer->next;
		while (buffer != head[c]) {
			currAddr = saveStep(buffer, currAddr);
			buffer = buffer->next;
		}
		saveH.tailAddr[c] = currAddr;
	}
	saveHead(saveH);
}

void loadAll() {
	saveH = loadHead();
	int currAddr = saveH.headAddr[0];
	link buffer;
	for (int c=0; c<MAXCHANNEL; c++) {
		display(loadingDisplay[c]);
		if (saveH.headAddr[c] < 0) continue;
		head[c] = newStep();
		current[c] = head[c];
		currAddr = saveH.headAddr[c];
		currAddr = loadStep(head[c], currAddr);
		buffer = head[c];
		while (currAddr < saveH.tailAddr[c]) {
			link newS = newStep();
			currAddr = loadStep(newS, currAddr);
			buffer->next = newS;
			buffer = newS;
		}
		buffer->next = head[c];
	}
}

save_p loadHead() {
	save_p save;
	byte* pointer = (byte*) (void*) &save;
	int addr = 0;
	for (int i=0; i < (int) sizeof(save_p); i++) {
		*pointer = EEPROM.read(addr);
		addr++;
		pointer++;
	}
	return save;
}

void saveHead(save_p save) {
	byte* pointer = (byte*) (void*) &save;
	int addr = 0;
	for (int i=0; i < (int) sizeof(save_p); i++){
		EEPROM.update(addr, *pointer);
		addr++;
		pointer++;
	}
}

int saveStep(link curr_step, int addr) {
	step buffer = *curr_step;
	buffer.next = (link) (addr + (int) sizeof(SequencerStep));
	byte* pointer = (byte*) (void*) &buffer;
	for (int i=0; i < (int) sizeof(SequencerStep); i++) {
		EEPROM.update(addr, *pointer);
		pointer++;
		addr++;
	}
	return addr;
}

int loadStep(link step, int addr) {
	byte* pointer = (byte*) (void*) step;
	for (int i=0; i<(int) sizeof(SequencerStep); i++) {
		*pointer = EEPROM.read(addr);
		pointer++;
		addr++;
	}
	return addr;
}