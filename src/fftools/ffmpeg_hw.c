#include <string.h>
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavfilter/buffersink.h"
#include "ffmpeg.h"

static int nb_hw_devices;
static HWDevice **hw_devices;

static HWDevice *hw_device_get_by_type(enum AVHWDeviceType type) {
    HWDevice *found = NULL;
    int i;
    for (i = 0; i < nb_hw_devices; i++) {
        if (hw_devices[i]->type == type) {
            if (found)
                return NULL;
            found = hw_devices[i];
        }
    }
    return found;
}

HWDevice *hw_device_get_by_name(const char *name) {
    int i;
    for (i = 0; i < nb_hw_devices; i++) {
        if (!strcmp(hw_devices[i]->name, name))
            return hw_devices[i];
    }
    return NULL;
}

static HWDevice *hw_device_add(void) {
    int err;
    err = av_reallocp_array(&hw_devices, nb_hw_devices + 1, sizeof(*hw_devices));
    if (err) {
        nb_hw_devices = 0;
        return NULL;
    }
    hw_devices[nb_hw_devices] = av_mallocz(sizeof(HWDevice));
    if (!hw_devices[nb_hw_devices])
        return NULL;
    return hw_devices[nb_hw_devices++];
}

static char *hw_device_default_name(enum AVHWDeviceType type) {
    const char *type_name = av_hwdevice_get_type_name(type);
    char *name;
    size_t index_pos;
    int index, index_limit = 1000;
    index_pos = strlen(type_name);
    name = av_malloc(index_pos + 4);
    if (!name)
        return NULL;
    for (index = 0; index < index_limit; index++) {
        snprintf(name, index_pos + 4, "%s%d", type_name, index);
        if (!hw_device_get_by_name(name))
            break;
    }
    if (index >= index_limit) {
        av_freep(&name);
        return NULL;
    }
    return name;
}

int hw_device_init_from_string(const char *arg, HWDevice **dev_out) {
    AVDictionary *options = NULL;
    const char *type_name = NULL, *name = NULL, *device = NULL;
    enum AVHWDeviceType type;
    HWDevice *dev, *src;
    AVBufferRef *device_ref = NULL;
    int err;
    const char *errmsg, *p, *q;
    size_t k;

    k = strcspn(arg, ":=@");
    p = arg + k;

    type_name = av_strndup(arg, k);
    if (!type_name) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    type = av_hwdevice_find_type_by_name(type_name);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        errmsg = "unknown device type";
        goto invalid;
    }

    if (*p == '=') {
        k = strcspn(p + 1, ":@,");

        name = av_strndup(p + 1, k);
        if (!name) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        if (hw_device_get_by_name(name)) {
            errmsg = "named device already exists";
            goto invalid;
        }

        p += 1 + k;
    } else {
        name = hw_device_default_name(type);
        if (!name) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!*p) {
        err = av_hwdevice_ctx_create(&device_ref, type, NULL, NULL, 0);
        if (err < 0)
            goto fail;

    } else if (*p == ':') {
        ++p;
        q = strchr(p, ',');
        if (q) {
            if (q - p > 0) {
                device = av_strndup(p, q - p);
                if (!device) {
                    err = AVERROR(ENOMEM);
                    goto fail;
                }
            }
            err = av_dict_parse_string(&options, q + 1, "=", ",", 0);
            if (err < 0) {
                errmsg = "failed to parse options";
                goto invalid;
            }
        }

        err = av_hwdevice_ctx_create(&device_ref, type, q ? device : p[0] ? p : NULL, options, 0);
        if (err < 0)
            goto fail;

    } else if (*p == '@') {
        src = hw_device_get_by_name(p + 1);
        if (!src) {
            errmsg = "invalid source device name";
            goto invalid;
        }

        err = av_hwdevice_ctx_create_derived(&device_ref, type, src->device_ref, 0);
        if (err < 0)
            goto fail;
    } else if (*p == ',') {
        err = av_dict_parse_string(&options, p + 1, "=", ",", 0);
        if (err < 0) {
            errmsg = "failed to parse options";
            goto invalid;
        }

        err = av_hwdevice_ctx_create(&device_ref, type, NULL, options, 0);
        if (err < 0)
            goto fail;
    } else {
        errmsg = "parse error";
        goto invalid;
    }

    dev = hw_device_add();
    if (!dev) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    dev->name = name;
    dev->type = type;
    dev->device_ref = device_ref;

    if (dev_out)
        *dev_out = dev;

    name = NULL;
    err = 0;
done:
    av_freep(&type_name);
    av_freep(&name);
    av_freep(&device);
    av_dict_free(&options);
    return err;
invalid:
    av_log(NULL, AV_LOG_ERROR, "Invalid device specification \"%s\": %s\n", arg, errmsg);
    err = AVERROR(EINVAL);
    goto done;
fail:
    av_log(NULL, AV_LOG_ERROR, "Device creation failed: %d.\n", err);
    av_buffer_unref(&device_ref);
    goto done;
}

static int hw_device_init_from_type(enum AVHWDeviceType type, const char *device, HWDevice **dev_out) {
    AVBufferRef *device_ref = NULL;
    HWDevice *dev;
    char *name;
    int err;

    name = hw_device_default_name(type);
    if (!name) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_hwdevice_ctx_create(&device_ref, type, device, NULL, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Device creation failed: %d.\n", err);
        goto fail;
    }

    dev = hw_device_add();
    if (!dev) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    dev->name= name;
    dev->type = type;
    dev->device_ref = device_ref;

    if (dev_out)
        *dev_out = dev;

    return 0;

fail:
    av_freep(&name);
    av_buffer_unref(&device_ref);
    return err;
}

void hw_device_free_all(void) {
    int i;
    for (i = 0; i < nb_hw_devices; i++) {
        av_freep(&hw_devices[i]->name);
        av_buffer_unref(&hw_devices[i]->device_ref);
        av_freep(&hw_devices[i]);
    }
    av_freep(&hw_devices);
    nb_hw_devices = 0;
}

static HWDevice *hw_device_match_by_codec(const AVCodec *codec) {
    const AVCodecHWConfig *config;
    HWDevice *dev;
    int i;
    for (i = 0;; i++) {
        config = avcodec_get_hw_config(codec, i);
        if (!config)
            return NULL;
        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            continue;
        dev = hw_device_get_by_type(config->device_type);
        if (dev)
            return dev;
    }
}

int hw_device_setup_for_decode(InputStream *ist) {
    const AVCodecHWConfig *config;
    enum AVHWDeviceType type;
    HWDevice *dev = NULL;
    int err, auto_device = 0;

    if (ist->hwaccel_device) {
        dev = hw_device_get_by_name(ist->hwaccel_device);
        if (!dev) {
            if (ist->hwaccel_id == HWACCEL_AUTO) {
                auto_device = 1;
            } else if (ist->hwaccel_id == HWACCEL_GENERIC) {
                type = ist->hwaccel_device_type;
                err = hw_device_init_from_type(type, ist->hwaccel_device, &dev);
            } else {
                return 0;
            }
        } else {
            if (ist->hwaccel_id == HWACCEL_AUTO) {
                ist->hwaccel_device_type = dev->type;
            } else if (ist->hwaccel_device_type != dev->type) {
                av_log(ist->dec_ctx, AV_LOG_ERROR, "Invalid hwaccel device specified for decoder: device %s of type %s is not usable with hwaccel %s.\n", dev->name, av_hwdevice_get_type_name(dev->type), av_hwdevice_get_type_name(ist->hwaccel_device_type));
                return AVERROR(EINVAL);
            }
        }
    } else {
        if (ist->hwaccel_id == HWACCEL_AUTO) {
            auto_device = 1;
        } else if (ist->hwaccel_id == HWACCEL_GENERIC) {
            type = ist->hwaccel_device_type;
            dev = hw_device_get_by_type(type);

            if (!dev && type == AV_HWDEVICE_TYPE_QSV)
                dev = hw_device_get_by_name("__qsv_device");

            if (!dev)
                err = hw_device_init_from_type(type, NULL, &dev);
        } else {
            dev = hw_device_match_by_codec(ist->dec);
            if (!dev)
                return 0;
        }
    }

    if (auto_device) {
        int i;
        if (!avcodec_get_hw_config(ist->dec, 0)) {
            return 0;
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(ist->dec, i);
            if (!config)
                break;
            type = config->device_type;
            dev = hw_device_get_by_type(type);
            if (dev) {
                av_log(ist->dec_ctx, AV_LOG_INFO, "Using auto hwaccel type %s with existing device %s.\n", av_hwdevice_get_type_name(type), dev->name);
            }
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(ist->dec, i);
            if (!config)
                break;
            type = config->device_type;
            err = hw_device_init_from_type(type, ist->hwaccel_device, &dev);
            if (err < 0) {
                continue;
            }
            if (ist->hwaccel_device) {
                av_log(ist->dec_ctx, AV_LOG_INFO, "Using auto hwaccel type %s with new device created from %s.\n", av_hwdevice_get_type_name(type), ist->hwaccel_device);
            } else {
                av_log(ist->dec_ctx, AV_LOG_INFO, "Using auto hwaccel type %s with new default device.\n", av_hwdevice_get_type_name(type));
            }
        }
        if (dev) {
            ist->hwaccel_device_type = type;
        } else {
            av_log(ist->dec_ctx, AV_LOG_INFO, "Auto hwaccel disabled: no device found.\n");
            ist->hwaccel_id = HWACCEL_NONE;
            return 0;
        }
    }

    if (!dev) {
        av_log(ist->dec_ctx, AV_LOG_ERROR, "No device available for decoder: device type %s needed for codec %s.\n", av_hwdevice_get_type_name(type), ist->dec->name);
        return err;
    }

    ist->dec_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
    if (!ist->dec_ctx->hw_device_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

int hw_device_setup_for_encode(OutputStream *ost) {
    const AVCodecHWConfig *config;
    HWDevice *dev = NULL;
    AVBufferRef *frames_ref = NULL;
    int i;

    if (ost->filter) {
        frames_ref = av_buffersink_get_hw_frames_ctx(ost->filter->filter);
        if (frames_ref && ((AVHWFramesContext*)frames_ref->data)->format == ost->enc_ctx->pix_fmt) {
        } else {
            frames_ref = NULL;
        }
    }

    for (i = 0;; i++) {
        config = avcodec_get_hw_config(ost->enc, i);
        if (!config)
            break;

        if (frames_ref && config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX && (config->pix_fmt == AV_PIX_FMT_NONE || config->pix_fmt == ost->enc_ctx->pix_fmt)) {
            av_log(ost->enc_ctx, AV_LOG_VERBOSE, "Using input frames context (format %s) with %s encoder.\n", av_get_pix_fmt_name(ost->enc_ctx->pix_fmt), ost->enc->name);
            ost->enc_ctx->hw_frames_ctx = av_buffer_ref(frames_ref);
            if (!ost->enc_ctx->hw_frames_ctx)
                return AVERROR(ENOMEM);
            return 0;
        }

        if (!dev && config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            dev = hw_device_get_by_type(config->device_type);
    }

    if (dev) {
        av_log(ost->enc_ctx, AV_LOG_VERBOSE, "Using device %s (type %s) with %s encoder.\n", dev->name, av_hwdevice_get_type_name(dev->type), ost->enc->name);
        ost->enc
