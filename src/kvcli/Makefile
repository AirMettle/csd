#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2023 AirMettle, Inc.
#  All rights reserved.
#

SPDK_ROOT_DIR ?= $(abspath $(CURDIR)/../../..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP = kvcli

C_SRCS := kvcli.c parse_args.c

SPDK_LIB_LIST = $(ALL_MODULES_LIST) event event_bdev

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk
