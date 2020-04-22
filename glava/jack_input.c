#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <jack/jack.h>

#include "fifo.h"


/* GLava backend and configuration */
struct audio_data *audio;
size_t buffer_size;

/* JACK backend */
jack_client_t *client;
jack_port_t *input_port1, *input_port2;



/* 
 * Callback function for JACK.
 * Appends new samples to GLava buffer and marks it as modified.
 */
int process (jack_nframes_t nframes, void *arg) {
    float* samples1 = (jack_default_audio_sample_t*)jack_port_get_buffer
                        (input_port1, nframes);
    float* samples2 = (jack_default_audio_sample_t*)jack_port_get_buffer
                        (input_port2, nframes);

    pthread_mutex_lock(&audio->mutex);
    float* bl = (float*) audio->audio_out_l;
    float* br = (float*) audio->audio_out_r;

    size_t buffer_steps = (size_t) nframes;
    size_t buffer_offset = buffer_size - buffer_steps;
    memmove(bl, &bl[buffer_steps], buffer_offset * sizeof(float));
    memmove(br, &br[buffer_steps], buffer_offset * sizeof(float));

    for (int frame = 0; frame < buffer_steps; frame++) {
        bl[buffer_offset + frame] = samples1[frame];
        br[buffer_offset + frame] = samples2[frame];
    }

    audio->modified = true;
    pthread_mutex_unlock(&audio->mutex);

    return 0;
}

void jack_shutdown (void *arg) {
    //Do more cleanup here if needed
}

static void init(struct audio_data* audio) {
    if (!audio->source)
        audio->source = strdup ("JACK");

    const char *client_name = "GLava";
    const char *server_name = NULL;
    jack_options_t options = JackNullOption;
    jack_status_t status;
    client = jack_client_open (client_name, options, &status, server_name);

    if (client == NULL) {
        fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n",
                    status);
        if (status & JackServerFailed) {
            printf ("Unable to connect to JACK server!\n");
        }
        exit (1);
    }
    if (status & JackServerStarted) {
        fprintf (stdout, "JACK server started\n");
    }
    if (status & JackNameNotUnique) {
        client_name = jack_get_client_name (client);
        fprintf (stderr, "Unique name '%s' assigned!\n", client_name);
    }

    jack_on_shutdown (client, jack_shutdown, 0);

    input_port1 = jack_port_register (client, "input1", JACK_DEFAULT_AUDIO_TYPE,
                                        JackPortIsInput, 0);
    input_port2 = jack_port_register (client, "input2", JACK_DEFAULT_AUDIO_TYPE,
                                        JackPortIsInput, 0);
    
    if ((input_port1 == NULL) || (input_port2 == NULL)) {
        fprintf (stderr, "No more JACK ports available!\n");
        exit (1);
    }
}

static void* entry(void* data) {
    audio = (struct audio_data *) data;
    buffer_size = audio->audio_buf_sz;

    jack_set_process_callback (client, process, NULL);
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
    return 0;
}

AUDIO_ATTACH (jack);
