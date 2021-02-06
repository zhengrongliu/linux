/*
 * virtio-snd.c
 *
 *  Created on: Nov 20, 2020
 *      Author: Zhengrong.liu
 */
#include <linux/module.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/pcm-indirect.h>
#include <sound/asoundef.h>
#include <sound/initval.h>

#define DRIVE_NAME "vsound"

#define VIRTIO_ID_SND 25

#define VSND_CMD_SET_PARAMS 1

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */


module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Virtio Sound Card.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Virtio Sound Card.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Virtio Sound Card.");

struct virtio_snd_device{
	struct snd_card *card;
	struct virtio_device *vdev;
	struct device *dev;
	struct snd_pcm *pcm;
	struct virtqueue *vq_ctrl;
	spinlock_t lock;
};

enum vsnd_cmd_type {
	VSND_CMD_PARAM_SET,
};

struct vsnd_cmd_hdr{
	__le32 type;
	__le32 unused;
};
struct vsnd_cmd_set_param{
	struct vsnd_cmd_hdr hdr;
	__le32 format;
	__le32 rate;
	__le32 channels;
	__le64 buffer;
};

static int virtio_snd_cmd_set_param(struct virtio_snd_device *vsnd,
		uint32_t format,uint32_t rate,uint32_t channels,uint64_t buffer)
{
	struct scatterlist sg[1];
	struct vsnd_cmd_set_param *cmd;
	unsigned long flags;
	int ret;

	cmd = kzalloc(sizeof(*cmd),GFP_KERNEL);
	if(!cmd){
		dev_err(vsnd->dev,"no enough memory for vgpu set mode command\n");
		return -ENOMEM;
	}

	cmd->hdr.type = cpu_to_le32(VSND_CMD_PARAM_SET);
	cmd->format = cpu_to_le32(format);
	cmd->rate = cpu_to_le32(rate);
	cmd->channels = cpu_to_le32(channels);
	cmd->buffer = cpu_to_le64(buffer);

	sg_init_one(sg, cmd, sizeof(*cmd));

	spin_lock_irqsave(&vsnd->lock, flags);
	ret = virtqueue_add_outbuf(vsnd->vq_ctrl, sg, 1, cmd, GFP_ATOMIC);
	if(ret < 0){
		kfree(cmd);
		dev_err(vsnd->dev,"add virtio buf failed(%d) for vgpu set mode command\n",ret);
		spin_unlock_irqrestore(&vsnd->lock, flags);
		return ret;
	}
	virtqueue_kick(vsnd->vq_ctrl);
	spin_unlock_irqrestore(&vsnd->lock, flags);
}




/* hardware definition */
static struct snd_pcm_hardware virtio_snd_playback_hw = {
        .info = (SNDRV_PCM_INFO_MMAP |
                 SNDRV_PCM_INFO_INTERLEAVED |
                 SNDRV_PCM_INFO_BLOCK_TRANSFER |
                 SNDRV_PCM_INFO_MMAP_VALID),
        .formats =          SNDRV_PCM_FMTBIT_S16_LE,
        .rates =            SNDRV_PCM_RATE_8000_48000,
        .rate_min =         8000,
        .rate_max =         48000,
        .channels_min =     2,
        .channels_max =     2,
        .buffer_bytes_max = 32768,
        .period_bytes_min = 4096,
        .period_bytes_max = 32768,
        .periods_min =      1,
        .periods_max =      1024,
};

/* hardware definition */
static struct snd_pcm_hardware virtio_snd_capture_hw = {
        .info = (SNDRV_PCM_INFO_MMAP |
                 SNDRV_PCM_INFO_INTERLEAVED |
                 SNDRV_PCM_INFO_BLOCK_TRANSFER |
                 SNDRV_PCM_INFO_MMAP_VALID),
        .formats =          SNDRV_PCM_FMTBIT_S16_LE,
        .rates =            SNDRV_PCM_RATE_8000_48000,
        .rate_min =         8000,
        .rate_max =         48000,
        .channels_min =     2,
        .channels_max =     2,
        .buffer_bytes_max = 32768,
        .period_bytes_min = 4096,
        .period_bytes_max = 32768,
        .periods_min =      1,
        .periods_max =      1024,
};

/* open callback */
static int virtio_snd_playback_open(struct snd_pcm_substream *substream)
{
        struct mychip *chip = snd_pcm_substream_chip(substream);
        struct snd_pcm_runtime *runtime = substream->runtime;

        runtime->hw = virtio_snd_playback_hw;
        /* more hardware-initialization will be done here */
        return 0;
}

/* close callback */
static int virtio_snd_playback_close(struct snd_pcm_substream *substream)
{
        struct mychip *chip = snd_pcm_substream_chip(substream);
        /* the hardware-specific codes will be here */
        return 0;

}

/* open callback */
static int virtio_snd_capture_open(struct snd_pcm_substream *substream)
{
        struct mychip *chip = snd_pcm_substream_chip(substream);
        struct snd_pcm_runtime *runtime = substream->runtime;

        runtime->hw = virtio_snd_capture_hw;
        /* more hardware-initialization will be done here */
        return 0;
}

/* close callback */
static int virtio_snd_capture_close(struct snd_pcm_substream *substream)
{
        struct mychip *chip = snd_pcm_substream_chip(substream);
        /* the hardware-specific codes will be here */
        return 0;
}

/* hw_params callback */
static int virtio_snd_pcm_hw_params(struct snd_pcm_substream *substream,
                             struct snd_pcm_hw_params *hw_params)
{
        /* the hardware-specific codes will be here */
        return 0;
}

/* hw_free callback */
static int virtio_snd_pcm_hw_free(struct snd_pcm_substream *substream)
{
        /* the hardware-specific codes will be here */
        return 0;
}

/* prepare callback */
static int virtio_snd_pcm_prepare(struct snd_pcm_substream *substream)
{
        struct virtio_snd_device *vsnd = snd_pcm_substream_chip(substream);
        struct snd_pcm_runtime *runtime = substream->runtime;

        /* set up the hardware with the current configuration
         * for example...
         */

        return virtio_snd_cmd_set_param(vsnd,runtime->format,
        		runtime->rate,runtime->channels,runtime->dma_addr);
}

/* trigger callback */
static int virtio_snd_pcm_trigger(struct snd_pcm_substream *substream,
                                  int cmd)
{
        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
                /* do something to start the PCM engine */
                break;
        case SNDRV_PCM_TRIGGER_STOP:
                /* do something to stop the PCM engine */
                break;
        default:
                return -EINVAL;
        }

        return 0;
}

/* pointer callback */
static snd_pcm_uframes_t
virtio_snd_pcm_pointer(struct snd_pcm_substream *substream)
{
        struct mychip *chip = snd_pcm_substream_chip(substream);
        unsigned int current_ptr;

        /* get the current hardware pointer */
        current_ptr = mychip_get_hw_pointer(chip);
        return current_ptr;
}

/* operators */
static struct snd_pcm_ops virtio_snd_playback_ops = {
        .open =        virtio_snd_playback_open,
        .close =       virtio_snd_playback_close,
        .hw_params =   virtio_snd_pcm_hw_params,
        .hw_free =     virtio_snd_pcm_hw_free,
        .prepare =     virtio_snd_pcm_prepare,
        .trigger =     virtio_snd_pcm_trigger,
        .pointer =     virtio_snd_pcm_pointer,
};

/* operators */
static struct snd_pcm_ops virtio_snd_capture_ops = {
        .open =        virtio_snd_capture_open,
        .close =       virtio_snd_capture_close,
        .hw_params =   virtio_snd_pcm_hw_params,
        .hw_free =     virtio_snd_pcm_hw_free,
        .prepare =     virtio_snd_pcm_prepare,
        .trigger =     virtio_snd_pcm_trigger,
        .pointer =     virtio_snd_pcm_pointer,
};


static int virtio_snd_control_pcm_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int virtio_snd_control_pcm_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int virtio_snd_control_pcm_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static struct snd_kcontrol_new virtio_snd_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info =	virtio_snd_control_pcm_info,
		.get =	virtio_snd_control_pcm_get,
		.put =	virtio_snd_control_pcm_put
	},
#if 0
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, PCM_STREAM),
		.info =	virtio_snd_control_pcm_stream_info,
		.get =	virtio_snd_control_pcm_stream_get,
		.put =	virtio_snd_control_pcm_stream_put
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.info =	snd_rme32_control_pcm_mask_info,
		.get =	snd_rme32_control_pcm_mask_get,
		.private_value = IEC958_AES0_PROFESSIONAL | IEC958_AES0_CON_EMPHASIS
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, PRO_MASK),
		.info =	virtio_snd_control_pcm_mask_info,
		.get =	virtio_snd_control_pcm_mask_get,
		.private_value = IEC958_AES0_PROFESSIONAL | IEC958_AES0_PRO_EMPHASIS
	},
#endif
};

static int virtio_snd_control_init(struct virtio_snd_device *vsnd)
{
	int idx, err;
	struct snd_kcontrol *kctl;
	struct snd_card *card = vsnd->card;

	for (idx = 0; idx < (int)ARRAY_SIZE(virtio_snd_controls); idx++) {
		kctl = snd_ctl_new1(&virtio_snd_controls[idx], vsnd);
		err = snd_ctl_add(card, kctl);
		if(err)
			return err;
	}

	return 0;
}

static int virtio_snd_device_init(struct virtio_snd_device *vsnd)
{
	int err;

	err = snd_pcm_new(vsnd->card, "Virtio PCM", 0, 1, 1, &vsnd->pcm);
	if (err < 0) {
		return err;
	}

	vsnd->pcm->private_data = vsnd;

	strcpy(vsnd->pcm->name, "Virtio PCM");

	snd_pcm_set_ops(vsnd->pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&virtio_snd_playback_ops);
	snd_pcm_set_ops(vsnd->pcm, SNDRV_PCM_STREAM_CAPTURE,
				&virtio_snd_capture_ops);
	err = snd_pcm_lib_preallocate_pages_for_all(vsnd->pcm,
			SNDRV_DMA_TYPE_DEV,
			vsnd->dev,64*1024, 64*1024);
	if(err < 0){
		snd_pcm_free(vsnd->pcm);
		return err;
	}

	err = virtio_snd_control_init(vsnd);
	if(err < 0){
		snd_pcm_free(vsnd->pcm);
		return err;
	}


	return 0;
}

static void virtio_snd_ctrl_ack(struct virtqueue *vq)
{
	struct virtio_snd_device *vsnd = vq->vdev->priv;
	struct vgpu_cmd_hdr *hdr;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vsnd->lock, flags);
	while ((hdr = virtqueue_get_buf(vsnd->vq_ctrl, &len)) != NULL)
		kfree(hdr);
	spin_unlock_irqrestore(&vsnd->lock, flags);
}


static int virtio_snd_vqs_init(struct virtio_snd_device *vsnd)
{
	struct virtqueue *vqs[1];
	vq_callback_t *cbs[] = { virtio_snd_ctrl_ack};
	static const char * const names[] = { "control" };
	int err;

	err = virtio_find_vqs(vsnd->vdev, ARRAY_SIZE(vqs),
			vqs, cbs, names, NULL);
	if (err)
		return err;

	vsnd->vq_ctrl = vqs[0];

	return 0;
}

static int virtio_snd_card_probe(struct virtio_device *vdev)
{
	static int dev_index;
	struct snd_card *card;
	struct virtio_snd_device *vsnd;
	struct device *dev = &vdev->dev;
	int err;

	if (dev_index >= SNDRV_CARDS) {
		return -ENODEV;
	}
	if (!enable[dev_index]) {
		dev_index++;
		return -ENOENT;
	}

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	err = snd_card_new(dev, index[dev_index], id[dev_index],
			THIS_MODULE, sizeof(struct virtio_snd_device), &card);
	if (err < 0){
		return err;
	}

	vsnd = card->private_data;

	vsnd->card = card;
	vsnd->dev = vdev->dev;

	spin_lock_init(&vsnd->lock);

	err = virtio_snd_vqs_init(vsnd);
	if(err < 0){
		snd_card_free(card);
		return err;
	}

	err = virtio_snd_device_init(vsnd);
	if(err < 0){
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "VirtioSound");
	strcpy(card->shortname, "VirtioSound");
	strcpy(card->longname, "VirtioSound");
	err = snd_card_register(card);
	if(err < 0){
		snd_card_free(card);
		return err;
	}

	vdev->priv = vsnd;
	dev_index++;

	return 0;
}

static void virtio_snd_card_remove(struct virtio_device *vdev)
{
	struct virtio_snd_device *vsnd = vdev->priv;
	struct snd_card *card = vsnd->card;

	snd_card_free(card);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SND, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_snd_card_driver = {
	.driver.name = DRIVE_NAME,
	.id_table = id_table,
	.probe = virtio_snd_card_probe,
	.remove = virtio_snd_card_remove,
};
module_virtio_driver(virtio_snd_card_driver);


MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtual Sound Card driver");
MODULE_AUTHOR("Zhengrong.liu <towering@126.com>");
