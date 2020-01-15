#include "SoundEffects.h"

void SoundEffects::beep (int speakerPin, float noteFrequency, long noteDuration) {
    int x;
    float microsecondsPerWave = 1000000/noteFrequency;
    float millisecondsPerCycle = 1000/(microsecondsPerWave * 2);
    float loopTime = noteDuration * millisecondsPerCycle;
    if (noteFrequency == 0) {for (int j = 0; j < 100; j++) {delayMicroseconds(noteDuration);} }
    else {
        for (x=0;x<loopTime;x++) {
            digitalWrite(speakerPin,HIGH);
            delayMicroseconds(microsecondsPerWave);
            digitalWrite(speakerPin,LOW);
            delayMicroseconds(microsecondsPerWave);
        }
    }
}

void SoundEffects::uhoh(int speakerOut) {
    for (int i=1500; i<744; i=i*1.01) { beep(speakerOut,i,5);} 
    delay(200); 
    for (int i=2000; i>1800; i=i*.99) { beep(speakerOut,i,20);}
}
void SoundEffects::whawha(int speakerOut) {
    for(double wah=0; wah<4; wah+=6.541){beep(speakerOut, 880+wah, 50);}
    beep(speakerOut, 932.32, 100); delay(80);
    for(double wah=0; wah<5; wah+=4.939){beep(speakerOut, 415.305+wah, 50);}
    beep(speakerOut, 880.000, 100); delay(80);
    for(double wah=0; wah<5; wah+=4.662){beep(speakerOut, 391.995+wah, 50);}
    beep(speakerOut, 830.61, 100); delay(80);
    for(int j=0; j<7; j++){beep(speakerOut, 783.99, 70);beep(speakerOut, 830.61, 70);}
    delay(400);
}
void SoundEffects::score(int speakerOut) {
    beep(speakerOut, E5, 75);
    beep(speakerOut, G5, 75);
    beep(speakerOut, C6, 75);
    beep(speakerOut, E5, 75);
    beep(speakerOut, G5, 75);
    beep(speakerOut, C6, 75); 
}
void SoundEffects::oneUp(int speakerOut) {
    beep(speakerOut,E5,125); //delay(130);
    beep(speakerOut,G5,125); //delay(130);
    beep(speakerOut,E6,125); //delay(130);
    beep(speakerOut,C6,125); //delay(130);
    beep(speakerOut,D6,125); //delay(130);
    beep(speakerOut,G6,125); //delay(125);
}
