
#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_INFO */

#include <linux/init.h> /* Needed for the macros */

#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <linux/input.h>
#include <linux/interrupt.h>



#define DRIVER_AUTHOR "zhangfanghui"
#define DRIVER_DESC   "fake sound driver"

#define BUFFER_SIZE (128*1024)
#define TONE_MIN_HZ 100
#define TONE_MAX_HZ 10000
#define MAX_VOLUME  256

static unsigned int max_sample_rate = 22050;
static bool debug = false;

module_param(max_sample_rate, uint, S_IRUGO);
MODULE_PARM_DESC(max_sample_rate, "Maximum sample rate, range [8000..48000].");
module_param(debug,  bool, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable ALSA callback logging.");


struct snd_fake {
        struct snd_card      *card;
        struct input_dev     *input_dev;
        struct snd_pcm       *pcm;
        struct tasklet_struct pcm_period_tasklet;
        struct delayed_work   pcm_stop;
        bool                  pcm_stop_cancelled;
        unsigned              amp_gpio;

        unsigned long  tone_frequency;
        unsigned long  tone_duration;
        struct hrtimer tone_timer;

        int volume;
};


struct snd_fake_platform_data {
        unsigned amp_gpio;
};

static struct snd_fake_platform_data fake_snd_data = {
        .amp_gpio       = 101,
};

static void platform_fake_release(struct device * dev)
{
    printk("this is platform device release function\n");
    return ;
}
static struct platform_device snd_fake =
{
        .name   = "snd-fake",
        .id     = -1,
        .dev    = {
                .platform_data = &fake_snd_data,
		.release = platform_fake_release,
        },
};



static int snd_fake_do_tone(struct snd_fake *chip, int hz)
{

        if (chip->pcm->streams[0].substream_opened ||
            chip->pcm->streams[1].substream_opened)
                return -EBUSY;

        if (hz <= 0) {
		printk("hz=0, disable speake by %d\n",chip->amp_gpio);
                chip->tone_frequency = 0;
                chip->tone_duration  = 0;
                return 0;
        }
        if (hz < TONE_MIN_HZ)
                hz = TONE_MIN_HZ;
        if (hz > TONE_MAX_HZ)
                hz = TONE_MAX_HZ;

	printk("call real pwm set function\n");

	printk("enable pwm\n");
        chip->tone_frequency = hz;
        chip->tone_duration  = 0;
	printk("set gpio %d to enable speaker\n",chip->amp_gpio);

        return 0;
}


/*--- input device (beep) ---*/

static int snd_fake_beep_event(struct input_dev *input, unsigned int type,
                                  unsigned int code, int hz)
{
        struct snd_fake *chip = input_get_drvdata(input);

        switch(code) {
        case SND_BELL:
                if (hz)
                        hz = 1000;
        case SND_TONE:
                break;
        default:
                return -1;
        }

        snd_fake_do_tone(chip, hz);

        return 0;
}

static void snd_fake_stop_tone(struct snd_fake * chip)
{
        snd_fake_do_tone(chip, 0);
}

static enum hrtimer_restart snd_fake_cb_stop_tone(struct hrtimer *pTimer)
{
        struct snd_fake *chip = container_of(pTimer, struct snd_fake, tone_timer);

        snd_fake_do_tone(chip, 0);

        return HRTIMER_NORESTART;
}


static int snd_fake_input_device_create(struct snd_card *card)
{
        struct snd_fake *chip = card->private_data;
        struct input_dev *input;
        int err;

        input = input_allocate_device();
        if (IS_ERR(input))
                return PTR_ERR(input);

        input->name = "fake speaker beep";
        input->dev.parent = chip->card->dev;
        input->event = snd_fake_beep_event;
        input_set_capability(input, EV_SND, SND_BELL);
        input_set_capability(input, EV_SND, SND_TONE);

        err = input_register_device(input);
        if (err < 0) {
                input_free_device(input);
                return err;
        }

        input_set_drvdata(input, chip);
        chip->input_dev = input;

        hrtimer_init(&chip->tone_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        chip->tone_timer.function = snd_fake_cb_stop_tone;

        return 0;
}
/*--- ALSA PCM device ---*/
static struct snd_kcontrol_new volume_control;

static int snd_fake_volume_control_info(struct snd_kcontrol *kcontrol,
                                           struct snd_ctl_elem_info *uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 1;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = MAX_VOLUME;
        return 0;
}

static int snd_fake_volume_control_get(struct snd_kcontrol *kcontrol,
                                          struct snd_ctl_elem_value *ucontrol)
{
        struct snd_fake *chip = snd_kcontrol_chip(kcontrol);
        ucontrol->value.integer.value[0] = chip->volume;
        return 0;
}

static int snd_fake_volume_control_put(struct snd_kcontrol *kcontrol,
                                          struct snd_ctl_elem_value *ucontrol)
{
        struct snd_fake *chip = snd_kcontrol_chip(kcontrol);
        int changed = 0, newValue = ucontrol->value.integer.value[0];

	printk("snd_fake_volume_control_put\n");
        if (newValue < 0) newValue = 0;
        if (newValue > MAX_VOLUME) newValue = MAX_VOLUME;

        if (chip->volume != newValue) {
		printk("old volue %d, new value %d\n",chip->volume, newValue);
                chip->volume = newValue;

                /* if tone or PCM playback is running, apply volume */
                if (chip->tone_frequency)
			printk("set real tone_frequency %ld\n",chip->tone_frequency);
                if (chip->pcm->streams[0].substream_opened ||
                    chip->pcm->streams[1].substream_opened)
			printk("set real volume %d\n",chip->volume);
                changed = 1;
        }

        return changed;
}

static struct snd_kcontrol_new volume_control = {
        .iface  = SNDRV_CTL_ELEM_IFACE_MIXER,
        .name   = "fake Playback Volume",
        .index  = 0,
        .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
        .info   = snd_fake_volume_control_info,
        .get    = snd_fake_volume_control_get,
        .put    = snd_fake_volume_control_put
};
static struct snd_pcm_hardware snd_fake_playback_hw = {
        .info = (SNDRV_PCM_INFO_MMAP |
                 SNDRV_PCM_INFO_MMAP_VALID |
                 SNDRV_PCM_INFO_INTERLEAVED),
        .formats =          SNDRV_PCM_FMTBIT_S16_LE,
        .rates =            SNDRV_PCM_RATE_CONTINUOUS,
        .rate_min =         8000,
        .rate_max =         48000,
        .channels_min =     1,
        .channels_max =     1,
        .buffer_bytes_max = BUFFER_SIZE,
        .period_bytes_min = 4096,
        .period_bytes_max = BUFFER_SIZE,
        .periods_min =      1,
        .periods_max =      1024,
};


static void snd_fake_call_pcm_elapsed(unsigned long data)
{
        if (data)
        {
                struct snd_pcm_substream *substream = (void *)data;
                snd_pcm_period_elapsed(substream);
        }
}


static int snd_fake_pcm_playback_open(struct snd_pcm_substream *substream)
{
        struct snd_fake *chip = snd_pcm_substream_chip(substream);
        struct snd_pcm_runtime *runtime = substream->runtime;

        if (chip->tone_frequency)
                return -EBUSY;


        runtime->hw = snd_fake_playback_hw;


        tasklet_init(&chip->pcm_period_tasklet, snd_fake_call_pcm_elapsed,
                     (unsigned long)substream);

        if (debug) printk(KERN_INFO "fake_pcm_playback_open\n");

        return 0;

}

static int snd_fake_pcm_playback_close(struct snd_pcm_substream *substream)
{
        struct snd_fake *chip = snd_pcm_substream_chip(substream);

        if (chip)
        {
                tasklet_kill(&chip->pcm_period_tasklet);
                if (debug) printk(KERN_INFO "fake_pcm_playback_close\n");
        }

	printk("snd_fake_pcm_playback_close\n");
        return 0;
}

static int snd_fake_pcm_hw_params(struct snd_pcm_substream *substream,
                                     struct snd_pcm_hw_params *hw_params)
{
	printk("snd_fake_pcm_hw_params\n");
        return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_fake_pcm_hw_free(struct snd_pcm_substream *substream)
{
	printk("snd_fake_pcm_hw_free\n");
        return snd_pcm_lib_free_pages(substream);
}

static int snd_fake_pcm_prepare(struct snd_pcm_substream *substream)
{
        struct snd_fake *chip = snd_pcm_substream_chip(substream);
        unsigned char int_period;

        /* interrupt can occur on every 1st, 2nd or 3rd pwm pulse */
        if (substream->runtime->rate > 32000)
                int_period = 1;
        else if (substream->runtime->rate > 16000)
                int_period = 2;
        else
                int_period = 3;
	printk("runtime->rat %d, int_period %d\n",substream->runtime->rate,int_period);

        if (debug)
                dev_info(chip->card->dev,
                         "legoev3_pcm_prepare with sample rate=%d, factor=%d\n",
                         substream->runtime->rate, int_period);


	printk("enable gpio to enable speaker\n");
        return 0;
}


static int snd_fake_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
        struct snd_fake *chip = snd_pcm_substream_chip(substream);
	printk("snd_fake_pcm_trigger\n");

        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
		printk("trigger start\n");
                if (debug) printk(KERN_INFO "fake_pcm_trigger(start)\n");
                chip->pcm_stop_cancelled = true;
                cancel_delayed_work(&chip->pcm_stop);
                break;
        case SNDRV_PCM_TRIGGER_STOP:
		printk("trigger stop\n");
                if (debug) printk(KERN_INFO "legoev3_pcm_trigger(stop)\n");
                chip->pcm_stop_cancelled = false;
                schedule_delayed_work(&chip->pcm_stop, msecs_to_jiffies(1000));
                break;
        default:
                return -EINVAL;
        }
        return 0;
}

unsigned fake_get_playback_ptr(void)
{
	return 100;
}

static snd_pcm_uframes_t
snd_fake_pcm_pointer(struct snd_pcm_substream *substream)
{
	printk("snd_fake_pcm_pointer\n");
        return bytes_to_frames(substream->runtime,
                               fake_get_playback_ptr());
}

static struct snd_pcm_ops snd_fake_playback_ops = {
        .open =        snd_fake_pcm_playback_open,
        .close =       snd_fake_pcm_playback_close,
        .ioctl =       snd_pcm_lib_ioctl,
        .hw_params =   snd_fake_pcm_hw_params,
        .hw_free =     snd_fake_pcm_hw_free,
        .prepare =     snd_fake_pcm_prepare,
        .trigger =     snd_fake_pcm_trigger,
        .pointer =     snd_fake_pcm_pointer,
};
static int snd_fake_new_pcm(struct snd_fake *chip)
{
        struct snd_pcm *pcm;
        int err;

        err = snd_pcm_new(chip->card, "Fake Sound PCM", 0, 1, 0, &pcm);
        if (err < 0)
                return err;
        pcm->private_data = chip;
        strcpy(pcm->name, "Fake Sound PCM");
        chip->pcm = pcm;

        snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
                &snd_fake_playback_ops);

        err = snd_pcm_lib_preallocate_pages_for_all(pcm,
                SNDRV_DMA_TYPE_CONTINUOUS, snd_dma_continuous_data (GFP_KERNEL),
                BUFFER_SIZE, BUFFER_SIZE);
        if (err < 0)
                return err;

        return 0;
}


static void snd_fake_pcm_stop(struct work_struct *work)
{
        struct snd_fake *chip = container_of((struct delayed_work*)work,
                                                struct snd_fake, pcm_stop);

        if (chip && !chip->pcm_stop_cancelled)
        {
                if (debug) printk(KERN_INFO "fake_pcm_stop\n");
		printk("disable pcm stop\n");
        }
}
static int snd_fake_create(struct snd_card *card,
                              unsigned gpio)
{
        struct snd_fake *chip  = card->private_data;
        static struct snd_device_ops ops = { };
        int err;

        chip->card = card;
        INIT_DELAYED_WORK(&chip->pcm_stop, snd_fake_pcm_stop);
        chip->pcm_stop_cancelled = false;
        chip->amp_gpio = gpio;

        err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
        if (err < 0)
                return err;

        err = snd_fake_new_pcm(chip);
        if (err < 0)
                return err;

        err = snd_ctl_add(card, snd_ctl_new1(&volume_control, chip));
        if (err < 0)
                return err;

        return 0;
}

static ssize_t snd_fake_show_volume(struct device *dev,
                                       struct device_attribute *attr,
                                       char *buf)
{
        struct snd_card *card = dev_get_drvdata(&to_platform_device(dev)->dev);
        struct snd_fake *chip = card->private_data;

        return snprintf(buf, PAGE_SIZE, "%u\n", (chip->volume * 100)/MAX_VOLUME);
}

static ssize_t snd_fake_store_volume(struct device *dev,
                                        struct device_attribute *attr,
                                        const char *buf, size_t count)
{
        struct snd_card *card = dev_get_drvdata(&to_platform_device(dev)->dev);
        struct snd_fake *chip = card->private_data;

        const char *start = buf;
              char *end   = (char*)buf;
        const char *last  = buf + count;

        int value;

        start = skip_spaces(end);
        if (last <= start)
                return -EINVAL;

        value = simple_strtol(start, &end, 0);
        if (end == start)
                return -EINVAL;

        if (value < 0)
                return -EINVAL;
        if (value > 100)
                return -EINVAL;

        chip->volume = value * MAX_VOLUME / 100;
        /* if tone or pcm playback is running, apply volume */
        if (chip->tone_frequency)
		printk("set tone_frequency\n");
        if (chip->pcm->streams[0].substream_opened ||
            chip->pcm->streams[1].substream_opened)
	{
		printk("is substream open, use other function to set\n");
	}

        return count;
}


static ssize_t snd_fake_show_mode(struct device *dev,
                                     struct device_attribute *attr,
                                     char *buf)
{
        struct snd_card *card = dev_get_drvdata(&to_platform_device(dev)->dev);
        struct snd_fake *chip = card->private_data;

        if (chip->tone_frequency)
                return snprintf(buf, PAGE_SIZE, "tone\n");
        else if (chip->pcm->streams[0].substream_opened ||
                 chip->pcm->streams[1].substream_opened)
                return snprintf(buf, PAGE_SIZE, "pcm\n");

        return snprintf(buf, PAGE_SIZE, "idle\n");
}

static ssize_t snd_fake_show_tone(struct device *dev,
                                     struct device_attribute *attr,
                                     char *buf)
{
        struct snd_card *card = dev_get_drvdata(&to_platform_device(dev)->dev);
        struct snd_fake *chip = card->private_data;

        if (chip->tone_frequency)
        {
                if (chip->tone_duration)
                        return snprintf(buf, PAGE_SIZE, "%lu %lu\n",
                                        chip->tone_frequency, chip->tone_duration);
                else
                        return snprintf(buf, PAGE_SIZE, "%lu\n", chip->tone_frequency);
        }

        return snprintf(buf, PAGE_SIZE, "0\n");
}

static ssize_t snd_fake_store_tone(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
        struct snd_card *card = dev_get_drvdata(&to_platform_device(dev)->dev);
        struct snd_fake *chip = card->private_data;

        const char *start = buf;
              char *end   = (char*)buf;
        const char *last  = buf + count;

        int freq, duration = 0;
        ktime_t time;

        start = skip_spaces(end);
        if (last <= start)
                return -EINVAL;

        freq = simple_strtol(start, &end, 0);
        if (end == start)
                return -EINVAL;

        if (freq != 0)
        {
                if (freq < TONE_MIN_HZ)
                        return -EINVAL;
                if (freq > TONE_MAX_HZ)
                        return -EINVAL;
        }

        start = skip_spaces(end);
        if (last <= start)
        {
                if (snd_fake_do_tone(chip, freq) == 0)
                        return count;

                return -EINVAL;
        }

        duration = simple_strtol(start, &end, 0);
        if (end == start)
                return -EINVAL;

        if (duration < 0)
                return -EINVAL;

        if (snd_fake_do_tone(chip, freq) == 0)
        {
                time = ktime_set(duration/1000, (duration%1000)*NSEC_PER_MSEC);
                hrtimer_start(&chip->tone_timer, time, HRTIMER_MODE_REL);

                return count;
        }

        return -EINVAL;
}

static DEVICE_ATTR(mode,   0444, snd_fake_show_mode,   NULL);
static DEVICE_ATTR(tone,   0644, snd_fake_show_tone,   snd_fake_store_tone);
static DEVICE_ATTR(volume, 0644, snd_fake_show_volume, snd_fake_store_volume);

static struct attribute *snd_fake_attrs[] = {
    &dev_attr_mode.attr
  , &dev_attr_tone.attr
  , &dev_attr_volume.attr
  , NULL
};

static struct attribute_group snd_fake_attr_group = {
        .attrs = snd_fake_attrs,
};



static int snd_fake_probe(struct platform_device *pdev)
{
        struct snd_fake_platform_data *pdata;
        struct snd_card *card;
        int err;

        pdata = pdev->dev.platform_data;
        if (!pdata)
                return -ENXIO;
	printk("this is snd_fake_probe\n");
	printk("simulate gpio is %d\n",pdata->amp_gpio);

        // configure maximal sample rate
        if (max_sample_rate < 8000)
                max_sample_rate = 8000;
        else if (max_sample_rate > 48000)
                max_sample_rate = 48000;

        snd_fake_playback_hw.rate_max = max_sample_rate;

        err = snd_card_new(&pdev->dev, -1, "fakesnd", THIS_MODULE,
                           sizeof(struct snd_fake), &card);
        if (err < 0) {
                dev_err(&pdev->dev, "failed to create sound card!\n");
                goto err_snd_card_new;
        }

        err = snd_fake_create(card, pdata->amp_gpio);
        if (err < 0) {
                dev_err(&pdev->dev, "failed to create sound device!\n");
                goto err_snd_fake_create;
        }

        strcpy(card->driver, "fakesnd");
        strcpy(card->shortname, "fake speaker");
        sprintf(card->longname, "%s connected to pwm", card->shortname);

        err = snd_card_register(card);
        if (err < 0) {
                dev_err(&pdev->dev, "failed to register sound card!\n");
                goto err_snd_card_register;
        }

        err = snd_fake_input_device_create(card);
        if (err < 0) {
                dev_err(&pdev->dev, "failed to create beep device!\n");
                goto err_snd_fake_input_device_create;
        }

        dev_set_drvdata(&pdev->dev, card);

        err = sysfs_create_group(&pdev->dev.kobj, &snd_fake_attr_group);

        return 0;

err_snd_fake_input_device_create:
err_snd_card_register:
err_snd_fake_create:
        snd_card_free(card);
err_snd_card_new:
	return 0;
}

static int snd_fake_remove(struct platform_device *pdev)
{
        struct snd_fake_platform_data *pdata = pdev->dev.platform_data;
        struct snd_card *card = dev_get_drvdata(&pdev->dev);
        struct snd_fake *chip =  card->private_data;

	printk("platform_driver remove function\n");
	printk("use gpio %d\n",pdata->amp_gpio);
        /* make sure sound is off */
        hrtimer_cancel(&chip->tone_timer);
        snd_fake_stop_tone(chip);

        chip->pcm_stop_cancelled = false;
        cancel_delayed_work_sync(&chip->pcm_stop);

        sysfs_remove_group(&pdev->dev.kobj, &snd_fake_attr_group);
        input_unregister_device(chip->input_dev);
        input_free_device(chip->input_dev);
        snd_card_free(card);
        dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}
static struct platform_driver snd_fake_platform_driver = {
        .driver = {
                .name = "snd-fake",
        },
        .probe = snd_fake_probe,
        .remove = snd_fake_remove,
};


static int __init fakesound_init(void)
{
        int ret;
        printk(KERN_INFO "This is fake sound driver\n=============\n");
        ret = platform_device_register(&snd_fake);
        if (ret)
                pr_warn("fakesound_init: "
                        "sound device registration failed: %d\n", ret);
	ret = platform_driver_register(&snd_fake_platform_driver);
	if (ret)
                pr_warn("fakesound_init: "
                        "sound driver registration failed: %d\n", ret);
        return 0;
}
static void __exit fakesound_exit(void)
{
        printk(KERN_INFO "Goodbye, fakesound\n");
	platform_driver_unregister(&snd_fake_platform_driver);
	platform_device_unregister(&snd_fake);
}

module_init(fakesound_init);
module_exit(fakesound_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR); //Who wrote this module?
MODULE_DESCRIPTION(DRIVER_DESC); // What does this module do

