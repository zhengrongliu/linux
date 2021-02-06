/*
 * vgpu.c
 *
 *  Created on: Feb 23, 2020
 *      Author: jason
 */
#include <linux/module.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_gpu.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <video/videomode.h>

#define DRIVE_NAME "vgpu"
#define VIRTIO_ID_VGPU 24

#define VGPU_XRES_MIN    32
#define VGPU_YRES_MIN    32

#define VGPU_XRES_MAX  8192
#define VGPU_YRES_MAX  8192

#define VGPU_MAX_SCANOUTS 16

#define vgpu_dbg(vgdev, fmt, args...) \
	dev_dbg(vgdev->dev, fmt, ## args)
#define vgpu_err(vgdev, fmt, args...) \
	dev_err(vgdev->dev, fmt, ## args)
#define vgpu_info(vgdev, fmt, args...) \
	dev_info(vgdev->dev, fmt, ## args)
#define vgpu_warn(vgdev, fmt, args...) \
	dev_warn(vgdev->dev, fmt, ## args)


enum vgpu_cmd_type {
	VGPU_CMD_MODE_SET,
	VGPU_CMD_FB_SETUP,
};

enum vgpu_color_formats {
	VGPU_FORMAT_B8G8R8A8_UNORM  = 1,
	VGPU_FORMAT_B8G8R8X8_UNORM  = 2,
	VGPU_FORMAT_A8R8G8B8_UNORM  = 3,
	VGPU_FORMAT_X8R8G8B8_UNORM  = 4,
	VGPU_FORMAT_R8G8B8A8_UNORM  = 5,
	VGPU_FORMAT_X8B8G8R8_UNORM  = 6,
	VGPU_FORMAT_A8B8G8R8_UNORM  = 7,
	VGPU_FORMAT_R8G8B8X8_UNORM  = 8,
};



struct vgpu_scanout{
	uint32_t index;
	struct drm_crtc crtc;
	struct drm_connector conn;
	struct drm_encoder enc;
};

struct vgpu_drm_device{
	struct drm_device ddev;
	struct virtio_device *vdev;
	struct device *dev;
	uint32_t num_scanouts;
	struct vgpu_scanout scanouts[VGPU_MAX_SCANOUTS];
	struct virtqueue *vq_ctrl;
	spinlock_t lock;
};

struct vgpu_virtio_config{
	uint32_t num_scanouts;
};

struct vgpu_cmd_hdr{
	__le32 type;
	__le32 unused;
};

struct vgpu_cmd_set_mode{
	struct vgpu_cmd_hdr hdr;
	__le32 scanout;
	__le32 width;
	__le32 height;
};

struct vgpu_cmd_setup_fb{
	struct vgpu_cmd_hdr hdr;
	__le32 scanout;
	__le32 format;
	__le32 pitch;
	__le64 paddr;
};




static int vgpu_plane_atomic_check(struct drm_plane *plane,
					 struct drm_plane_state *state);

static void vgpu_plane_atomic_update(struct drm_plane *plane,
					    struct drm_plane_state *old_state);


DEFINE_DRM_GEM_CMA_FOPS(drv_driver_fops);

static uint32_t to_vgpu_format(uint32_t format)
{
	switch(format){
	case DRM_FORMAT_XRGB8888:
		return VGPU_FORMAT_X8R8G8B8_UNORM;
	case DRM_FORMAT_ARGB8888:
		return VGPU_FORMAT_A8R8G8B8_UNORM;
	case DRM_FORMAT_BGRX8888:
		return VGPU_FORMAT_B8G8R8X8_UNORM;
	case DRM_FORMAT_BGRA8888:
		return VGPU_FORMAT_B8G8R8A8_UNORM;
	case DRM_FORMAT_RGBX8888:
		return VGPU_FORMAT_R8G8B8X8_UNORM;
	case DRM_FORMAT_RGBA8888:
		return VGPU_FORMAT_R8G8B8A8_UNORM;
	case DRM_FORMAT_XBGR8888:
		return VGPU_FORMAT_X8B8G8R8_UNORM;
	case DRM_FORMAT_ABGR8888:
		return VGPU_FORMAT_A8B8G8R8_UNORM;
	}

	WARN_ON(1);
	return 0;
}

static void vgpu_virtio_ctrl_ack(struct virtqueue *vq)
{
	struct vgpu_drm_device *vgdev = vq->vdev->priv;
	struct vgpu_cmd_hdr *hdr;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vgdev->lock, flags);
	while ((hdr = virtqueue_get_buf(vgdev->vq_ctrl, &len)) != NULL)
		kfree(hdr);
	spin_unlock_irqrestore(&vgdev->lock, flags);
}

static int vgpu_virtio_cmd_setup_fb(struct vgpu_drm_device *vgdev,
		uint32_t scanout,uint64_t paddr,uint32_t format,uint32_t pitch)
{
	struct scatterlist sg[1];
	struct vgpu_cmd_setup_fb *cmd;
	unsigned long flags;
	int ret;

	cmd = kzalloc(sizeof(*cmd),GFP_KERNEL);
	if(!cmd){
		DRM_ERROR("no enough memory for vgpu set mode command\n");
		return -ENOMEM;
	}

	cmd->hdr.type = cpu_to_le32(VGPU_CMD_FB_SETUP);
	cmd->format = cpu_to_le32(format);
	cmd->paddr = cpu_to_le64(paddr);
	cmd->scanout = cpu_to_le32(scanout);
	cmd->pitch = pitch;

	sg_init_one(sg, cmd, sizeof(*cmd));

	spin_lock_irqsave(&vgdev->lock, flags);
	ret = virtqueue_add_outbuf(vgdev->vq_ctrl, sg, 1, cmd, GFP_ATOMIC);
	if(ret < 0){
		DRM_ERROR("add virtio buf failed(%d) for vgpu set mode command\n",ret);
		kfree(cmd);
		spin_unlock_irqrestore(&vgdev->lock, flags);
		return ret;
	}
	virtqueue_kick(vgdev->vq_ctrl);
	spin_unlock_irqrestore(&vgdev->lock, flags);
	return 0;

}
static int vgpu_virtio_cmd_set_mode(struct vgpu_drm_device *vgdev,
		uint32_t scanout,uint32_t width,uint32_t height)
{
	struct scatterlist sg[1];
	struct vgpu_cmd_set_mode *cmd;
	unsigned long flags;
	int ret;

	cmd = kzalloc(sizeof(*cmd),GFP_KERNEL);
	if(!cmd){
		DRM_ERROR("no enough memory for vgpu set mode command\n");
		return -ENOMEM;
	}

	cmd->hdr.type = cpu_to_le32(VGPU_CMD_MODE_SET);
	cmd->width = cpu_to_le32(width);
	cmd->height = cpu_to_le32(height);
	cmd->scanout = cpu_to_le32(scanout);

	sg_init_one(sg, cmd, sizeof(*cmd));

	spin_lock_irqsave(&vgdev->lock, flags);
	ret = virtqueue_add_outbuf(vgdev->vq_ctrl, sg, 1, cmd, GFP_ATOMIC);
	if(ret < 0){
		DRM_ERROR("add virtio buf failed(%d) for vgpu set mode command\n",ret);
		kfree(cmd);
		spin_unlock_irqrestore(&vgdev->lock, flags);
		return ret;
	}
	virtqueue_kick(vgdev->vq_ctrl);
	spin_unlock_irqrestore(&vgdev->lock, flags);
	return 0;
}

static int vgpu_virtio_init_vqs(struct vgpu_drm_device *vgdev)
{
	struct virtqueue *vqs[1];
	vq_callback_t *cbs[] = { vgpu_virtio_ctrl_ack};
	static const char * const names[] = { "control" };
	int err;

	err = virtio_find_vqs(vgdev->vdev, ARRAY_SIZE(vqs),
			vqs, cbs, names, NULL);
	if (err)
		return err;

	vgdev->vq_ctrl = vqs[0];

	return 0;
}




static struct drm_driver drv_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME |
			   DRIVER_ATOMIC,
	.name = "vgpu",
	.desc = "vgpu drm",
	.date = "20200226",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	.fops = &drv_driver_fops,
	.dumb_create = drm_gem_cma_dumb_create,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,
};


static const uint32_t vgpu_drm_color_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
};

static int vgpu_plane_atomic_check(struct drm_plane *plane,
					 struct drm_plane_state *state)
{


	return 0;
}

static void vgpu_plane_atomic_update(struct drm_plane *plane,
					    struct drm_plane_state *old_state)
{
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_crtc *crtc = plane->state->crtc;
	struct vgpu_scanout *scanout;
	struct vgpu_drm_device *vgdev ;
	uint64_t paddr;
	uint32_t format;
	uint32_t pitch;

	if(!crtc){
		crtc = old_state->crtc;
		fb = old_state->fb;
	}

	vgdev = crtc->dev->dev_private;

	scanout = container_of(crtc,struct vgpu_scanout,crtc);

	if (plane->state->fb) {
		paddr = drm_fb_cma_get_gem_addr(fb, plane->state, 0);
		pitch = fb->pitches[0];
		format = to_vgpu_format(fb->format->format);
	} else {
		paddr = 0;
		pitch = 0;
		format = 0;
	}

	vgpu_virtio_cmd_setup_fb(vgdev,scanout->index,paddr,format,pitch);
}

static void vgpu_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct videomode vm;
	struct drm_device *dev = crtc->dev;
	struct vgpu_drm_device *vgdev = dev->dev_private;
	struct vgpu_scanout *scanout;

	scanout = container_of(crtc,struct vgpu_scanout,crtc);

	drm_display_mode_to_videomode(mode, &vm);

	vgpu_virtio_cmd_set_mode(vgdev,scanout->index,vm.hactive,vm.vactive);
}

static void vgpu_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_state)
{

}

static void vgpu_crtc_atomic_disable(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_state)
{

}

static void vgpu_crtc_atomic_flush(struct drm_crtc *crtc,
			     struct drm_crtc_state *old_crtc_state)
{
	unsigned long flags;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (crtc->state->event)
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
	crtc->state->event = NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}
static enum drm_mode_status vgpu_crtc_mode_valid(struct drm_crtc *crtc,
					   const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static int vgpu_conn_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector, VGPU_XRES_MAX, VGPU_YRES_MAX);

	drm_set_preferred_mode(connector, 1024, 768);

	return count;
}


static int vgpu_conn_detect_ctx(struct drm_connector *connector,
		struct drm_modeset_acquire_ctx *ctx,
		bool force)
{
	return connector_status_connected;
}

static const struct drm_mode_config_funcs vgpu_mode_config_funcs = {
	.fb_create = drm_fb_cma_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_plane_funcs vgpu_plane_funcs = {
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static const struct drm_plane_helper_funcs vgpu_plane_helper_funcs = {
	.atomic_check = vgpu_plane_atomic_check,
	.atomic_update = vgpu_plane_atomic_update,
};

static const struct drm_crtc_funcs vgpu_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs vgpu_crtc_helper_funcs = {
	.mode_set_nofb = vgpu_crtc_mode_set_nofb,
	.mode_valid =  vgpu_crtc_mode_valid,
	.atomic_flush = vgpu_crtc_atomic_flush,
	.atomic_enable = vgpu_crtc_atomic_enable,
	.atomic_disable = vgpu_crtc_atomic_disable,
};

static const struct drm_connector_funcs vgpu_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.fill_modes = drm_helper_probe_single_connector_modes,
};

static const struct drm_connector_helper_funcs vgpu_conn_helper_funcs = {
	.get_modes = vgpu_conn_get_modes,
	.detect_ctx = vgpu_conn_detect_ctx,
};

static const struct drm_encoder_funcs vgpu_enc_funcs = {
};

static const struct drm_encoder_helper_funcs vgpu_enc_helper_funcs = {
};




static struct drm_plane * vgpu_plane_create(struct vgpu_drm_device *vgdev,enum drm_plane_type type,int index)
{
	struct drm_plane *plane = NULL;
	int ret  = 0;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane){
		DRM_ERROR("no enough memory for drm plane\n");
		return ERR_PTR(-ENOMEM);
	}

	ret = drm_universal_plane_init(&vgdev->ddev, plane, 1<<index,
				       &vgpu_plane_funcs, vgpu_drm_color_formats, ARRAY_SIZE(vgpu_drm_color_formats),
				       NULL, type, NULL);
	if (ret < 0){
		DRM_ERROR("drm init plane failed\n");
		goto failed_init_plane;
	}

	drm_plane_helper_add(plane, &vgpu_plane_helper_funcs);

	return plane;

failed_init_plane:
	kfree(plane);
	return ERR_PTR(ret);

}

static int vgpu_scanout_init(struct vgpu_drm_device *vgdev,int index)
{
	struct drm_plane *primary = NULL;
	struct vgpu_scanout *scanout = &vgdev->scanouts[index];
	struct drm_connector *conn = &scanout->conn;
	struct drm_encoder *enc = &scanout->enc;
	struct drm_crtc *crtc = &scanout->crtc;
	struct drm_device *ddev = &vgdev->ddev;
	int ret = 0;

	scanout->index = index;

	primary = vgpu_plane_create(vgdev,DRM_PLANE_TYPE_PRIMARY,index);
	if(IS_ERR(primary)){
		return PTR_ERR(primary);
	}

	ret = drm_crtc_init_with_planes(ddev, crtc, primary, NULL,
				      &vgpu_crtc_funcs, NULL);
	if(ret < 0){
		DRM_ERROR("drm crtc init failed\n");
		goto failed;
	}

	drm_crtc_helper_add(crtc, &vgpu_crtc_helper_funcs);

	ret = drm_connector_init(ddev, conn, &vgpu_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	if(ret < 0){
		DRM_ERROR("drm conntector init failed\n");
		goto failed;
	}
	drm_connector_helper_add(conn, &vgpu_conn_helper_funcs);

	ret = drm_encoder_init(ddev, enc, &vgpu_enc_funcs,
			 DRM_MODE_ENCODER_VIRTUAL, NULL);
	if(ret < 0){
		DRM_ERROR("drm encoder init failed\n");
		goto failed;
	}
	drm_encoder_helper_add(enc, &vgpu_enc_helper_funcs);

	enc->possible_crtcs = 1<<index;

	ret = drm_mode_connector_attach_encoder(conn, enc);
	if(ret < 0){
		DRM_ERROR("drm connector attach failed\n");
		goto failed;
	}

	return 0;
failed:
	drm_plane_cleanup(primary);
	return ret;
}

static int vgpu_virtio_probe(struct virtio_device *vdev)
{
	struct device *dev = &vdev->dev;
	struct vgpu_drm_device *vgdev;
	struct drm_device *ddev;
	uint32_t num_scanouts;
	int ret = 0;
	int i;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	dma_set_coherent_mask(dev, DMA_BIT_MASK(32));

	vgdev = kzalloc(sizeof(*vgdev),GFP_KERNEL);
	if(vgdev == NULL)
		return -ENOMEM;

	vgdev->dev = dev;
	vgdev->vdev = vdev;
	vdev->priv = vgdev;

	ddev = &vgdev->ddev;

	ret = drm_dev_init(ddev,&drv_driver,dev);
	if(ret){
		DRM_ERROR("drm device init failed\n");
		goto failed_init_drm_device;
	}

	ddev->dev_private = vgdev;

	/* get display info */
	virtio_cread(vdev, struct vgpu_virtio_config,
		     num_scanouts, &num_scanouts);
	vgdev->num_scanouts = min_t(uint32_t, num_scanouts,
				    VIRTIO_GPU_MAX_SCANOUTS);
	if(vgdev->num_scanouts == 0){
		ret = -EINVAL;
		DRM_ERROR("vgpu scanout is zero");
		goto device_error;
	}

	drm_mode_config_init(ddev);

	ddev->mode_config.min_width = VGPU_XRES_MIN;
	ddev->mode_config.min_height = VGPU_YRES_MIN;
	ddev->mode_config.max_width = VGPU_XRES_MAX;
	ddev->mode_config.max_height = VGPU_YRES_MAX;
	ddev->mode_config.funcs = &vgpu_mode_config_funcs;

	for(i = 0;i < vgdev->num_scanouts;i++){
		vgpu_scanout_init(vgdev,i);
	}

	drm_mode_config_reset(&vgdev->ddev);


	spin_lock_init(&vgdev->lock);

	ret = vgpu_virtio_init_vqs(vgdev);
	if(ret){
		DRM_ERROR("vgpu init virtio vqs failed");
		goto failed_init_vqs;
	}

	ret = drm_dev_register(ddev, 0);
	if(ret){
		DRM_ERROR("drm device register failed");
		goto failed_reg_drm_device;
	}


	return 0;
failed_reg_drm_device:
failed_init_vqs:
device_error:
	drm_dev_unref(ddev);
failed_init_drm_device:
	kfree(vgdev);
	return ret;
}

static void vgpu_virtio_remove(struct virtio_device *vdev)
{
}



static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_VGPU, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_gpu_driver = {
	.driver.name = DRIVE_NAME,
	.id_table = id_table,
	.probe = vgpu_virtio_probe,
	.remove = vgpu_virtio_remove,
};
module_virtio_driver(virtio_gpu_driver);


MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtual GPU driver");
MODULE_AUTHOR("Zhengrong.liu <towering@126.com>");
