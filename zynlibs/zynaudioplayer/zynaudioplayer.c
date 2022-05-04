/*  Audio file player library for Zynthian
    Copyright (C) 2021 Brian Walton <brian@riban.co.uk>
    License: LGPL V3
*/

#include "zynaudioplayer.h"

#include <stdio.h> //provides printf
#include <string.h> //provides strcmp, memset
#include <jack/jack.h> //provides interface to JACK
#include <jack/midiport.h> //provides JACK MIDI interface
#include <sndfile.h> //provides sound file manipulation
#include <samplerate.h> //provides samplerate conversion
#include <pthread.h> //provides multithreading
#include <unistd.h> //provides usleep
#include <stdlib.h> //provides exit

#define DPRINTF(fmt, args...) if(g_bDebug) printf(fmt, ## args)

enum playState {
	STOPPED		= 0,
	STARTING	= 1,
	PLAYING		= 2,
	STOPPING	= 3
};

enum seekState {
    IDLE        = 0,
    SEEKING     = 1,
    LOADING     = 2
};

#define AUDIO_BUFFER_SIZE 200000 // 100000 is approx. 1s of audio

struct AUDIO_BUFFER {
    size_t size;
    size_t end;
    size_t startPos; // Position within overal stream of buffer start in frames
    uint8_t isEmpty;
    float data[AUDIO_BUFFER_SIZE];
};

size_t g_nBufferPos = 0; // Postion within buffer of read cursor
size_t g_nActiveBuffer = 0; // Index of the currently active buffer

jack_client_t* g_pJackClient = NULL;
jack_port_t* g_pJackOutA = NULL;
jack_port_t* g_pJackOutB = NULL;
jack_port_t * g_pJackMidiIn = NULL;


uint8_t g_bDebug = 0;
uint8_t g_bFileOpen = 0; // 1 whilst file is open - used to flag thread to close file
uint8_t g_bMore = 0; // 1 if there is more data to read from file, i.e. not at end of file or looping
uint8_t g_nSeek = IDLE; //!@todo Can we conbine seek state with play state?// Seek state
uint8_t g_nPlayState = STOPPED;
uint8_t g_bLoop = 0; // 1 to loop at end of song
jack_nframes_t g_nSamplerate = 44100;
struct SF_INFO  g_sf_info; // Structure containing currently loaded file info
pthread_t g_threadFile; // ID of file reader thread
struct AUDIO_BUFFER g_audioBuffer[2]; // Double-buffer for transfering audio from file to player
size_t g_nChannelB = 0; // Offset of samples for channel B (0 for mono source or 1 for multi-channel)
jack_nframes_t g_nPlaybackPosSeconds = 0;
jack_nframes_t g_nPlaybackPosFrames = 0; // Current playback position in frames since start of audio
uint32_t g_nXruns = 0;
unsigned int g_nSrcQuality = SRC_SINC_FASTEST;
char g_sFilename[128];
float g_fLevel = 1.0; // Audio level (volume) 0..1

/*** Public functions exposed as external C functions in header ***/

void enableDebug(uint8_t bEnable) {
    printf("libaudioplayer setting debug mode %s\n", bEnable?"on":"off");
    g_bDebug = bEnable;
}

uint8_t open(const char* filename) {
    closeFile();
    strcpy(g_sFilename, filename);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    int rc = pthread_create(&g_threadFile, &attr, fileThread, NULL);
    if(rc) {
        fprintf(stderr, "Failed to create file reading thread\n");
        closeFile();
        return 0;
    }
    return 1;
}

float getFileDuration(const char* filename) {
    SF_INFO info;
    info.format = 0;
    info.samplerate = 0;
    SNDFILE* pFile = sf_open(filename, SFM_READ, &info);
    sf_close(pFile);
    if(info.samplerate)
        return (float)info.frames / info.samplerate;
    return 0.0f;
}

void closeFile() {
    stopPlayback();
    g_bFileOpen = 0;
    void* status;
    pthread_join(g_threadFile, &status);
    g_sFilename[0] = '\0';
}

uint8_t save(const char* filename) {
    //!@todo Implement save
    return 0;
}

const char* getFilename() {
    return g_sFilename;
}

float getDuration() {
    if(g_sf_info.samplerate)
        return (float)g_sf_info.frames / g_sf_info.samplerate;
    return 0.0f;
}

void setPosition(float time) {
    g_nPlaybackPosFrames = time * g_nSamplerate;
    g_nPlaybackPosSeconds = time;
    g_nBufferPos = 0;
    g_nSeek = SEEKING;
}

float getPosition() {
    return (float)g_nPlaybackPosFrames / g_nSamplerate;
}

void setLoop(uint8_t bLoop) {
	g_bLoop = bLoop;
	g_bMore = 1;
}

void startPlayback() {
	if(!g_pJackClient)
		return;
	g_nPlayState = STARTING;
}

void stopPlayback() {
	if(g_nPlayState == STOPPED)
		return;
	g_nPlayState = STOPPING;
}

uint8_t getPlayState() {
	return g_nPlayState;
}

int getSamplerate() {
    return g_sf_info.samplerate;
}

int getChannels() {
    return g_sf_info.channels;
}

int getFrames() {
    return g_sf_info.frames;
}

int getFormat() {
    return g_sf_info.format;
}

/*** Private functions not exposed as external C functions (not declared in header) ***/

void end() {
    if(g_pJackClient)
        closeFile();
        jack_client_close(g_pJackClient);
}

// Handle JACK process callback
static int onJackProcess(jack_nframes_t nFrames, void *notused) {
    jack_default_audio_sample_t *pOutA = (jack_default_audio_sample_t*)jack_port_get_buffer(g_pJackOutA, nFrames);
    jack_default_audio_sample_t *pOutB = (jack_default_audio_sample_t*)jack_port_get_buffer(g_pJackOutB, nFrames);
    for(size_t nOffset = 0; nOffset < nFrames; ++nOffset) {
        // Silence period incase there is insufficient data to fill output buffers
        pOutA[nOffset] = 0.0;
        pOutB[nOffset] = 0.0;
        if(g_nSeek == IDLE) { // Don't process any frames whilst seeking
            if(g_nPlayState == STARTING)
                g_nPlayState = PLAYING;
            if(g_nPlayState == PLAYING) {
                if(g_audioBuffer[0].isEmpty && g_audioBuffer[1].isEmpty) {
                    g_nPlayState = STOPPED;
                    DPRINTF("zynaudioplayer both buffers empty so stopping\n");
                    break;
                }
                if(g_nBufferPos >= g_audioBuffer[g_nActiveBuffer].end) {
                    g_audioBuffer[g_nActiveBuffer].isEmpty = 1;
                    g_nActiveBuffer = g_nActiveBuffer?0:1;
                    DPRINTF("zynaudioplayer switched playback buffer to %d\n", g_nActiveBuffer);
                    g_nBufferPos = 0;
                    if(g_audioBuffer[g_nActiveBuffer].end == 0) {
                        // Run out of data so assume at end of track and stop
                        g_audioBuffer[g_nActiveBuffer].isEmpty = 1;
                        g_nPlayState = STOPPED;
                        g_nActiveBuffer = 0; //!@todo Check that resetting active buffer is appropriate here
                        g_audioBuffer[0].isEmpty = 1;
                        g_audioBuffer[1].isEmpty = 1;
                        DPRINTF("zynaudioplayer run out of data so assuming end of track and stopping\n");
                        break;
                    }
                }
                pOutA[nOffset] = g_fLevel * g_audioBuffer[g_nActiveBuffer].data[g_nBufferPos];
                pOutB[nOffset] = g_fLevel * g_audioBuffer[g_nActiveBuffer].data[g_nBufferPos + g_nChannelB];
                g_nBufferPos += g_sf_info.channels;
                ++g_nPlaybackPosFrames;
            }
        }
    }

    // Process MIDI input
    void* pMidiBuffer = jack_port_get_buffer(g_pJackMidiIn, nFrames);
    jack_midi_event_t midiEvent;
    jack_nframes_t nCount = jack_midi_get_event_count(pMidiBuffer);
    for(jack_nframes_t i = 0; i < nCount; i++)
    {
        jack_midi_event_get(&midiEvent, pMidiBuffer, i);
        if((midiEvent.buffer[0] & 0xF0) == 0xB0)
        {
            switch(midiEvent.buffer[1])
            {
                case 7:
                    g_fLevel = (float)midiEvent.buffer[2] / 100.0;
                    break;
                case 68:
                    if(midiEvent.buffer[2] > 63)
                        startPlayback();
                    else
                        stopPlayback();
                    break;
                case 69:
                    setLoop(midiEvent.buffer[2] > 63);
                    break;
            }
        }
    }
    /*
    if(g_nPlayState == PLAYING) {
        if(g_nPlaybackPosFrames / g_nSamplerate > g_nPlaybackPosSeconds) {
            g_nPlaybackPosSeconds = g_nPlaybackPosFrames / g_nSamplerate;
            DPRINTF("%02d:%02d\n", g_nPlaybackPosSeconds / 60, g_nPlaybackPosSeconds % 60);
        }
    }
    */
	return 0;
}

// Handle JACK process callback
int onJackSamplerate(jack_nframes_t nFrames, void *pArgs) {
    DPRINTF("zynaudioplayer: Jack sample rate: %u\n", nFrames);
    g_nSamplerate = nFrames;
    return 0;
}

void* fileThread(void* param) {
    g_sf_info.format = 0; // This triggers open to populate info structure
    SNDFILE* pFile = sf_open(g_sFilename, SFM_READ, &g_sf_info);
    if(!pFile) {
        fprintf(stderr, "libaudioplayer failed to open file %s: %s\n", g_sFilename, sf_strerror(pFile));
        pthread_exit(NULL);
    }
    g_bFileOpen = 1;
    g_nChannelB = (g_sf_info.channels == 1)?0:1; // Mono or stereo based on first one or two channels

    g_bMore = 1;
    g_nSeek = SEEKING;
    g_nPlaybackPosFrames = 0;
    g_nPlaybackPosSeconds = 0;
    size_t nFramesSinceStart = 0;

    // Initialise samplerate conversion
    SRC_DATA srcData;
    float pBuffer[AUDIO_BUFFER_SIZE]; // Buffer used to read sample data from file
    srcData.data_in = pBuffer;
    srcData.src_ratio = (float)g_nSamplerate / g_sf_info.samplerate;
    srcData.output_frames = AUDIO_BUFFER_SIZE;
    size_t nMaxRead = AUDIO_BUFFER_SIZE;
    if(srcData.src_ratio > 1.0)
        nMaxRead = (float)AUDIO_BUFFER_SIZE / srcData.src_ratio;
    nMaxRead /= g_sf_info.channels;
    int nError;
    SRC_STATE* pSrcState = src_new(g_nSrcQuality, g_sf_info.channels, &nError);
    // Only read quantity of frames that will fit into buffer

    while(g_bFileOpen) {
        if(g_nSeek) {
            g_audioBuffer[0].isEmpty = 1;
            g_audioBuffer[1].isEmpty = 1;
            g_audioBuffer[0].end = 0;
            g_audioBuffer[1].end = 0;
            g_nActiveBuffer = 0;
            size_t nNewPos = g_nPlaybackPosFrames;
            if(srcData.src_ratio)
                nNewPos = g_nPlaybackPosFrames / srcData.src_ratio;
            sf_seek(pFile, nNewPos, SEEK_SET);
            g_nSeek = LOADING;
            src_reset(pSrcState);
            srcData.end_of_input = 0;
        }
        if(g_bMore || g_nSeek == LOADING)
        {
            uint8_t nDbuffer = g_nActiveBuffer;
            for(int i = 0; i < 2; ++i) {
                // Populate each empty double-buffer
                srcData.data_out = g_audioBuffer[nDbuffer].data;
                if(g_audioBuffer[nDbuffer].isEmpty) {
                    int nRead;
                    if(srcData.src_ratio == 1.0)
                        nRead = sf_readf_float(pFile, g_audioBuffer[nDbuffer].data, nMaxRead);
                    else
                        nRead = sf_readf_float(pFile, pBuffer, nMaxRead);
                    if(nRead) {
                        g_audioBuffer[nDbuffer].isEmpty = 0;
                        g_audioBuffer[nDbuffer].startPos = nFramesSinceStart;
                    }
                    else if(g_bLoop)
                        sf_seek(pFile, 0, SEEK_SET);
                    else {
                        g_bMore = 0;
                        DPRINTF("zynaudioplayer reached end of file\n");
                    }
                    if(srcData.src_ratio == 1.0) {
                        g_audioBuffer[nDbuffer].end = nRead * g_sf_info.channels;
                        DPRINTF("zynaudioplayer read %d samples at %d into double-buffer %d which is %0.1fs\n", nRead, g_nSamplerate, nDbuffer, (float)nRead / g_nSamplerate);
                    } else {
                        srcData.input_frames = nRead;
                        if(nRead < nMaxRead)
                            srcData.end_of_input = 1;
                        DPRINTF("About to start samplerate conversion on iteration %d with %d frames processing %ld frames\n", i, nRead, srcData.input_frames);
                        int rc = src_process(pSrcState, &srcData);
                        g_audioBuffer[nDbuffer].end = srcData.output_frames_gen * g_sf_info.channels;
                    }
                    nFramesSinceStart += g_audioBuffer[nDbuffer].end;
                }
                if(g_nSeek == LOADING) {
                    g_nSeek = IDLE;
                }
                nDbuffer = g_nActiveBuffer?0:1;
            }
        }
        usleep(10000);
    }
    if(pFile) {
        int nError = sf_close(pFile);
        if(nError != 0)
            fprintf(stderr, "libaudioplayer failed to close file with error code %d\n", nError);
    }
    g_audioBuffer[0].isEmpty = 1;
    g_audioBuffer[1].isEmpty = 1;
    g_nPlaybackPosFrames = 0;
    g_nPlaybackPosSeconds = 0;
    pSrcState = src_delete(pSrcState);
    pthread_exit(NULL);
}

void init() {
    printf("zynaudioplayer init\n");
    for(int i = 0; i < 2; ++i) {
        g_audioBuffer[i].size = AUDIO_BUFFER_SIZE;
        g_audioBuffer[i].end = 0;
        g_audioBuffer[i].startPos = 0; // Position within overal stream of buffer start in frames
        g_audioBuffer[i].isEmpty = 1;
    }

	// Register with Jack server
	char *sServerName = NULL;
	jack_status_t nStatus;
	jack_options_t nOptions = JackNoStartServer;

	if ((g_pJackClient = jack_client_open("zynaudioplayer", nOptions, &nStatus, sServerName)) == 0) {
		fprintf(stderr, "libaudioplayer failed to start jack client: %d\n", nStatus);
		exit(1);
	}

	// Create audio output ports
	if (!(g_pJackOutA = jack_port_register(g_pJackClient, "output_a", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(stderr, "libaudioplayer cannot register audio output port A\n");
		exit(1);
	}
	if (!(g_pJackOutB = jack_port_register(g_pJackClient, "output_b", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
		fprintf(stderr, "libaudioplayer cannot register audio output port B\n");
		exit(1);
	}

    // Create MIDI input port
    if(!(g_pJackMidiIn = jack_port_register(g_pJackClient, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0)))
    {
        fprintf(stderr, "libzynaudioplayer cannot register MIDI input port\n");
        exit(1);
    }

	// Register the cleanup function to be called when program exits
	//atexit(end);

	// Register the callback to process audio and MIDI
	jack_set_process_callback(g_pJackClient, onJackProcess, 0);

	if (jack_activate(g_pJackClient)) {
		fprintf(stderr, "libaudioplayer cannot activate client\n");
		exit(1);
	}
}

const char* getFileInfo(const char* filename, int type) {
    SF_INFO info;
    info.format = 0;
    info.samplerate = 0;
    SNDFILE* pFile = sf_open(filename, SFM_READ, &info);
    const char* pValue = sf_get_string(pFile, type);
    if(pValue) {
        sf_close(pFile);
        return pValue;
    }
    sf_close(pFile);
    return "";
}

uint8_t setSrcQuality(unsigned int quality) {
    if(quality > SRC_LINEAR)
        return 0;
    g_nSrcQuality = quality;
    return 1;
}

void setVolume(float level) {
    if(level < 0 || level > 2)
        return;
    g_fLevel = level;
}

float getVolume() {
    return g_fLevel;
}