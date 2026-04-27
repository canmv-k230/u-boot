# /* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
#  *
#  * Redistribution and use in source and binary forms, with or without
#  * modification, are permitted provided that the following conditions are met:
#  * 1. Redistributions of source code must retain the above copyright
#  * notice, this list of conditions and the following disclaimer.
#  * 2. Redistributions in binary form must reproduce the above copyright
#  * notice, this list of conditions and the following disclaimer in the
#  * documentation and/or other materials provided with the distribution.
#  *
#  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
#  * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
#  * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
#  * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
#  * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
#  * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
#  * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#  */
obj-y += k230d/

obj-y += board_common.o

ifndef CONFIG_KBURN
obj-y += k230_boot.o
obj-y += k230_atag.o
obj-y += k230_gpio.o
endif

ifdef CONFIG_SPL_BUILD
obj-y += k230_spl.o
endif

# Add pufs driver
obj-y += puf_fw/pufs_internal.o
obj-y += puf_fw/pufs_crypto.o
obj-y += puf_fw/pufs_dma.o
obj-y += puf_fw/pufs_ecc.o
obj-y += puf_fw/pufs_ecp.o
obj-y += puf_fw/pufs_ecp_mprog.o
obj-y += puf_fw/pufs_hmac.o
obj-y += puf_fw/pufs_ka.o
obj-y += puf_fw/pufs_reg_ctrl.o
obj-y += puf_fw/pufs_rt.o
obj-y += puf_fw/pufs_rt_cde.o
obj-y += puf_fw/pufs_sm2.o
obj-y += puf_fw/pufs_sp38a.o
obj-y += puf_fw/pufs_sp38d.o
