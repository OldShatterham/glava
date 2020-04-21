#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <poll.h>

#include <jack/jack.h>

#include "fifo.h"


/* glava backend */
struct audio_data* audio;
int buffer_steps;
int buffer_offset;


/* Resampling buffer */
//When enough JACK samples to fill another glava buffer frame are summed,
//the current sample_sum_* is appended to frames_*[frame_index]
double sample_sum_1, sample_sum_2;
float sample_counter;

float sample_sz_ratio = 93.75f;

int frame_index;
int LIMIT;


/* JACK backend */
const int glava_sample_rate = 22050;
const int JACK_sample_rate = 96000;
jack_port_t *input_port1, *input_port2;
jack_client_t* client;

void updateBuffer(float frames_l[], float frames_r[]) {
    float* bl = (float*) audio->audio_out_l;
    float* br = (float*) audio->audio_out_r;

    pthread_mutex_lock(&audio->mutex);

    //Shift buffers:
    memmove(bl, &bl[buffer_steps], buffer_offset * sizeof(float));
    memmove(br, &br[buffer_steps], buffer_offset * sizeof(float));

    //Append samples from frames_l and frames_r:
    float blockSum = 0.0f;
    for (int i = 0; i < buffer_steps; i++) {
        blockSum += frames_l[i];
    }

    for (int frame = 0; frame < buffer_steps; frame++) {
        bl[buffer_offset + frame] = frames_l[frame];
        br[buffer_offset + frame] = frames_r[frame];
    }

    float checkSum = 0.0f;
    for (int i = 0; i < buffer_steps; i++) {
        checkSum += bl[buffer_offset + i];
    }
    //fprintf(stdout, "[DEBUG] Copied data from frames_l to glava buffers; block sum: %f, check sum: %f.\n", blockSum, checkSum);


    frame_index = 0;

    audio->modified = true;
    pthread_mutex_unlock(&audio->mutex);
}

int process (jack_nframes_t nframes, void *arg) {
    double *frames_l = (double*)arg;
    //TODO: double *frames_r = frames_l;

    float* samples1 = (jack_default_audio_sample_t*)jack_port_get_buffer (input_port1, nframes);
	float* samples2 = (jack_default_audio_sample_t*)jack_port_get_buffer (input_port2, nframes);

    //Try to resample from JACK sample rate to glava sample rate
    //TODO: find a proper resampling algorithm

    /*for (int frame = 0; frame < nframes; frame++) {
        sample_sum_1 += *samples1;
        sample_sum_2 += *samples2;
        sample_counter++;

        if (sample_counter >= sample_sz_ratio) {
            if (frame_index >= LIMIT) {
                frame_index = LIMIT - 1;
                fprintf(stderr, "Dear god, frame_index was %d while limit is %d!\n", frame_index, LIMIT);
            }
            frames_l[frame_index] = sample_sum_1 / sample_counter;
            //TODO: frames_r[frame_index] = sample_sum_2 / sample_counter;

            sample_sum_1 = 0.0;
            sample_sum_2 = 0.0;

            if(++frame_index >= buffer_steps) {
                //TODO: Swap frames_l for frames_r
                updateBuffer(frames_l, frames_l);
            }

            sample_counter -= sample_sz_ratio;
        }
    }*/

    updateBuffer(samples1, samples2);

    return 0;
}

void jack_shutdown (void *arg) {
    //DO SOMETHING HERE?
    printf ("jack_shutdown() called.\n");
}

static void init(struct audio_data* audio) {
    printf ("[DEBUG] init() called.\n");
    if (!audio->source) {
        audio->source = strdup("/tmp/mpd.fifo");
    }

    const char *client_name = "glava";
    const char *server_name = NULL;
    jack_options_t options = JackNullOption;
    jack_status_t status;
    client = jack_client_open (client_name, options, &status, server_name);

    if (client == NULL) {
		fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			printf ("Unable to connect to JACK server!\n");
		}
		exit (1);
	}
	if (status & JackServerStarted) {
		fprintf (stdout, "JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(client);
		fprintf (stderr, "Unique name '%s' assigned!\n", client_name);
	}

    jack_on_shutdown (client, jack_shutdown, 0);

    input_port1 = jack_port_register (client, "input1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    input_port2 = jack_port_register (client, "input2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    
    if ((input_port1 == NULL) || (input_port2 == NULL)) {
        fprintf (stderr, "No more JACK ports available!\n");
        exit (1);
    }
    printf ("[DEBUG] init() finished.\n");
}

static void* entry(void* data) {
    printf ("[DEBUG] entry() called.\n");
    audio = (struct audio_data *) data;

    size_t buffer_sz = audio->audio_buf_sz;
    size_t glava_sample_sz = audio->sample_sz;
    sample_sz_ratio = ((float)JACK_sample_rate) / glava_sample_rate;

    //How many samples the buffer will shift each update:
    //TODO: buffer_steps = glava_sample_sz / 4;
    buffer_steps = 64;
    buffer_offset = buffer_sz - buffer_steps;

    fprintf(stdout, "Buffer size: '%d', Sample size: '%d'\n", buffer_sz, glava_sample_sz);
 
    double frames_l[buffer_steps];
    //TODO: Clean all of this up...
    double *frames_r = frames_l;
    LIMIT = buffer_steps;

    jack_set_process_callback (client, process, frames_l);
    printf ("[DEBUG] trying to activate client...\n");
    if (jack_activate (client)) {
        fprintf (stderr, "Cannot activate client!\n");
        exit (1);
    }
    
    while (true) {
        sleep(1);

        if (audio->terminate == 1) {
            break;
        }
    }

    jack_client_close (client);
    printf ("[DEBUG] entry() finished.\n");
    return 0;
}

AUDIO_ATTACH(jack);
