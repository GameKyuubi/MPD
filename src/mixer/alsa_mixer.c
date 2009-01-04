
#include "../output_api.h"
#include "../mixer_api.h"

#include <glib.h>
#include <alsa/asoundlib.h>

#define VOLUME_MIXER_ALSA_DEFAULT		"default"
#define VOLUME_MIXER_ALSA_CONTROL_DEFAULT	"PCM"

struct alsa_mixer {
	char *device;
	char *control;
	snd_mixer_t *handle;
	snd_mixer_elem_t *elem;
	long volume_min;
	long volume_max;
	int volume_set;
};

struct alsa_mixer *
alsa_mixer_init(void)
{
	struct alsa_mixer *am = g_malloc(sizeof(struct alsa_mixer));
	am->device = NULL;
	am->control = NULL;
	am->handle = NULL;
	am->elem = NULL;
	am->volume_min = 0;
	am->volume_max = 0;
	am->volume_set = -1;
	return am;
}

void
alsa_mixer_finish(struct alsa_mixer *am)
{
	g_free(am);
}

void
alsa_mixer_configure(struct alsa_mixer *am, ConfigParam *param)
{
	BlockParam *bp;

	if ((bp = getBlockParam(param, "mixer_device")))
		am->device = bp->value;
	if ((bp = getBlockParam(param, "mixer_control")))
		am->control = bp->value;
}

void
alsa_mixer_close(struct alsa_mixer *am)
{
	if (am->handle) snd_mixer_close(am->handle);
	am->handle = NULL;
}

bool
alsa_mixer_open(struct alsa_mixer *am)
{
	int err;
	snd_mixer_elem_t *elem;
	const char *control_name = VOLUME_MIXER_ALSA_CONTROL_DEFAULT;
	const char *device = VOLUME_MIXER_ALSA_DEFAULT;

	if (am->device) {
		device = am->device;
	}
	err = snd_mixer_open(&am->handle, 0);
	snd_config_update_free_global();
	if (err < 0) {
		g_warning("problems opening alsa mixer: %s\n", snd_strerror(err));
		return false;
	}

	if ((err = snd_mixer_attach(am->handle, device)) < 0) {
		g_warning("problems attaching alsa mixer: %s\n",
			snd_strerror(err));
		alsa_mixer_close(am);
		return false;
	}

	if ((err = snd_mixer_selem_register(am->handle, NULL,
		    NULL)) < 0) {
		g_warning("problems snd_mixer_selem_register'ing: %s\n",
			snd_strerror(err));
		alsa_mixer_close(am);
		return false;
	}

	if ((err = snd_mixer_load(am->handle)) < 0) {
		g_warning("problems snd_mixer_selem_register'ing: %s\n",
			snd_strerror(err));
		alsa_mixer_close(am);
		return false;
	}

	elem = snd_mixer_first_elem(am->handle);

	if (am->control) {
		control_name = am->control;
	}

	while (elem) {
		if (snd_mixer_elem_get_type(elem) == SND_MIXER_ELEM_SIMPLE) {
			if (strcasecmp(control_name,
				       snd_mixer_selem_get_name(elem)) == 0) {
				break;
			}
		}
		elem = snd_mixer_elem_next(elem);
	}

	if (elem) {
		am->elem = elem;
		snd_mixer_selem_get_playback_volume_range(am->elem,
							  &am->volume_min,
							  &am->volume_max);
		return true;
	}

	g_warning("can't find alsa mixer control \"%s\"\n", control_name);

	alsa_mixer_close(am);
	return false;
}

bool
alsa_mixer_control(struct alsa_mixer *am, int cmd, void *arg)
{
	switch (cmd) {
	case AC_MIXER_CONFIGURE:
		alsa_mixer_configure(am, (ConfigParam *)arg);
		if (am->handle)
			alsa_mixer_close(am);
		return true;
	case AC_MIXER_GETVOL:
	{
		int err;
		int ret, *volume = arg;
		long level;

		if (!am->handle && !alsa_mixer_open(am)) {
			return false;
		}
		if ((err = snd_mixer_handle_events(am->handle)) < 0) {
			g_warning("problems getting alsa volume: %s (snd_mixer_%s)\n",
				snd_strerror(err), "handle_events");
			alsa_mixer_close(am);
			return false;
		}
		if ((err = snd_mixer_selem_get_playback_volume(am->elem,
			       SND_MIXER_SCHN_FRONT_LEFT, &level)) < 0) {
			g_warning("problems getting alsa volume: %s (snd_mixer_%s)\n",
				snd_strerror(err), "selem_get_playback_volume");
			alsa_mixer_close(am);
			return false;
		}
		ret = ((am->volume_set / 100.0) * (am->volume_max - am->volume_min)
			+ am->volume_min) + 0.5;
		if (am->volume_set > 0 && ret == level) {
			ret = am->volume_set;
		} else {
			ret = (int)(100 * (((float)(level - am->volume_min)) /
				(am->volume_max - am->volume_min)) + 0.5);
		}
		*volume = ret;
		return true;
	}
	case AC_MIXER_SETVOL:
	{
		float vol;
		long level;
		int *volume = arg;
		int err;

		if (!am->handle && !alsa_mixer_open(am)) {
			return false;
		}
		vol = *volume;

		am->volume_set = vol + 0.5;
		am->volume_set = am->volume_set > 100 ? 100 :
			    (am->volume_set < 0 ? 0 : am->volume_set);

		level = (long)(((vol / 100.0) * (am->volume_max - am->volume_min) +
			 am->volume_min) + 0.5);
		level = level > am->volume_max ? am->volume_max : level;
		level = level < am->volume_min ? am->volume_min : level;

		if ((err = snd_mixer_selem_set_playback_volume_all(am->elem,
								level)) < 0) {
			g_warning("problems setting alsa volume: %s\n",
				snd_strerror(err));
			alsa_mixer_close(am);
			return false;
		}
		return true;
	}
	default:
		g_warning("Unsuported alsa control\n");
		break;
	}
	return false;
}
