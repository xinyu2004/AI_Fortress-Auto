CFG_TEE_TA_LOG_LEVEL ?= 2

BINARY=8f7a1c2e-6b4d-4e91-9c3a-2d5e7f10a002

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
clean:
	@echo 'TA_DEV_KIT_DIR is unset'
endif
