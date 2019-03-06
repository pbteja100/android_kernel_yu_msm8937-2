# For NXP Smart Audio.

# tfa drivers code path:
TFA_PATH := sound/soc/codecs/tfa

# include path defination.
EXTRA_CFLAGS += -I$(TFA_PATH)/inc
EXTRA_CFLAGS += -I$(TFA_PATH)/linux-kernel-inc
EXTRA_CFLAGS += -I$(TFA_PATH)/src

# debugging support (also enables trace_printk)
EXTRA_CFLAGS += -DDEBUG

# tfa source code:
snd-soc-tfa98xx-objs := src/tfa98xx.o
snd-soc-tfa98xx-objs += src/tfa_container.o
snd-soc-tfa98xx-objs += src/tfa_dsp.o
snd-soc-tfa98xx-objs += src/tfa9890_init.o
snd-soc-tfa98xx-objs += src/tfa9891_init.o
snd-soc-tfa98xx-objs += src/tfa9888_init.o
snd-soc-tfa98xx-objs += src/tfa9897_init.o
snd-soc-tfa98xx-objs += src/tfa9887B_init.o
snd-soc-tfa98xx-objs += src/tfa9887_init.o

#obj-$(CONFIG_SND_SOC_TFA98XX) += snd-soc-tfa98xx.o
obj-$(CONFIG_SND_SOC_TFA9890) += snd-soc-tfa98xx.o
#obj-y	+= snd-soc-tfa98xx.o
