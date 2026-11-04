/* Exact production menu gate + submit wrapper; SDL queue is a bounded sink. */
#include <SDL.h>
#include "audio_fade.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { SDL_AUDIO_RATE=48000, SDL_AUDIO_BUF_SAMPLES=256, BLACK_FADE_FRAMES=960 };
static int g_audio_initialized=1, accept=1, submissions, tails, clears;
static SDL_AudioDeviceID g_audio_device=1;
static pthread_mutex_t fade_lock=PTHREAD_MUTEX_INITIALIZER;
static BlackAudioFade fade={BLACK_FADE_FRAMES,0,{0,0}};
static atomic_int fading,closed;
static int menu_muted;
static unsigned menu_fade_in;
static int16_t output[4096];
static uint32_t queue_size;
static int fake_queue(SDL_AudioDeviceID device,const void *data,Uint32 bytes)
{ (void)device; if(bytes>sizeof output)abort();memcpy(output,data,bytes);queue_size+=bytes;++tails;return 0; }
static void fake_clear(SDL_AudioDeviceID device) { (void)device;queue_size=0;++clears; }
static Uint32 fake_size(SDL_AudioDeviceID device) { (void)device;return queue_size; }
static int black_SDLSubmitSamples(const int16_t *data,int frames)
{ ++submissions;if(accept){if(frames*4>sizeof output)abort();memcpy(output,data,frames*4);}return accept; }
#define SDL_QueueAudio fake_queue
#define SDL_ClearQueuedAudio fake_clear
#define SDL_GetQueuedAudioSize fake_size
#include BLACK_AUDIO_MENU_SOURCE
#define CHECK(x) do {if(!(x)){fprintf(stderr,"audio menu line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    unsetenv("RECOMP_AUDIO_DUMP");unsetenv("RECOMP_AUDIO_DIAG");
    int16_t speech[512];for(int i=0;i<256;++i){speech[i*2]=12000;speech[i*2+1]=-8000;}
    CHECK(xa2_submit_samples(speech,256)==1 && output[0]==12000 && fade.last[0]==12000);
    black_AudioSetMenuOpen(1);
    CHECK(tails==1 && menu_muted && output[0]>0 && output[1918]<20);
    black_AudioSetMenuOpen(1);CHECK(tails==1); /* Idempotent open. */
    for(int k=0;k<16;++k){
        CHECK(xa2_submit_samples(speech,256)==1);
        for(int i=0;i<512;++i)CHECK(output[i]==0);
    }
    CHECK(submissions==17 && speech[0]==12000 && speech[1]==-8000);
    black_AudioSetMenuOpen(0);CHECK(clears==1 && !queue_size);
    accept=0;CHECK(xa2_submit_samples(speech,256)==0 && menu_fade_in==960);
    accept=1;CHECK(xa2_submit_samples(speech,256)==1 && output[0]==0 && output[510]>0 && output[510]<12000);
    for(int k=0;k<4;++k)CHECK(xa2_submit_samples(speech,256)==1);
    CHECK(!menu_fade_in && output[0]==12000 && output[1]==-8000);
    /* A second open is silent too; final process fade still runs safely. */
    black_AudioSetMenuOpen(1);atomic_store(&fading,1);
    CHECK(xa2_submit_samples(speech,256)==1 && output[0]==0 && output[510]==0);
    atomic_store(&closed,1);CHECK(xa2_submit_samples(speech,256)==0);
    puts("Production menu audio: bounded tail, paced silence, queue flush/resume ramp, rejection and quit: PASS");
    return 0;
}
