# Copyright (c) 2012 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# GNU Make based build file.  For details on GNU Make see:
# http://www.gnu.org/software/make/manual/make.html
#

#
# Project information
#
# These variables store project specific settings for the project name
# build flags, files to copy or install.  In the examples it is typically
# only the list of sources and project name that will actually change and
# the rest of the makefile is boilerplate for defining build rules.
#
PROJECT:=saltygme
LDFLAGS:=-lppapi -lm
CXX_SOURCES:=gme-source/Ay_Apu.cpp \
	gme-source/Ay_Cpu.cpp \
	gme-source/Ay_Emu.cpp \
	gme-source/Blip_Buffer.cpp \
	gme-source/Classic_Emu.cpp \
	gme-source/Data_Reader.cpp \
	gme-source/Dual_Resampler.cpp \
	gme-source/Effects_Buffer.cpp \
	gme-source/Fir_Resampler.cpp \
	gme-source/Gb_Apu.cpp \
	gme-source/Gb_Cpu.cpp \
	gme-source/Gb_Oscs.cpp \
	gme-source/Gbs_Emu.cpp \
	gme-source/gme.cpp \
	gme-source/Gme_File.cpp \
	gme-source/Gym_Emu.cpp \
	gme-source/Hes_Apu.cpp \
	gme-source/Hes_Cpu.cpp \
	gme-source/Hes_Emu.cpp \
	gme-source/Kss_Cpu.cpp \
	gme-source/Kss_Emu.cpp \
	gme-source/Kss_Scc_Apu.cpp \
	gme-source/M3u_Playlist.cpp \
	gme-source/Multi_Buffer.cpp \
	gme-source/Music_Emu.cpp \
	gme-source/Nes_Apu.cpp \
	gme-source/Nes_Cpu.cpp \
	gme-source/Nes_Fme7_Apu.cpp \
	gme-source/Nes_Namco_Apu.cpp \
	gme-source/Nes_Oscs.cpp \
	gme-source/Nes_Vrc6_Apu.cpp \
	gme-source/Nsfe_Emu.cpp \
	gme-source/Nsf_Emu.cpp \
	gme-source/Sap_Apu.cpp \
	gme-source/Sap_Cpu.cpp \
	gme-source/Sap_Emu.cpp \
	gme-source/Sms_Apu.cpp \
	gme-source/Snes_Spc.cpp \
	gme-source/Spc_Cpu.cpp \
	gme-source/Spc_Dsp.cpp \
	gme-source/Spc_Emu.cpp \
	gme-source/Vgm_Emu.cpp \
	gme-source/Vgm_Emu_Impl.cpp \
	gme-source/Ym2413_Emu.cpp \
	gme-source/Ym2612_Emu.cpp

C_SOURCES:= saltygme.c \
	plugin-libgme.c \
	plugin-vio2sf.c \
	plugin-aosdk.c \
	xzdec.c \
	xz-embedded/xz_crc32.c \
	xz-embedded/xz_dec_lzma2.c \
	xz-embedded/xz_dec_stream.c \
	vio2sf/corlett.c \
	vio2sf/vio2sf.c \
	vio2sf/desmume/MMU.c \
	vio2sf/desmume/mc.c \
	vio2sf/desmume/thumb_instructions.c \
	vio2sf/desmume/cp15.c \
	vio2sf/desmume/FIFO.c \
	vio2sf/desmume/arm_instructions.c \
	vio2sf/desmume/GPU.c \
	vio2sf/desmume/SPU.c \
	vio2sf/desmume/armcpu.c \
	vio2sf/desmume/bios.c \
	vio2sf/desmume/matrix.c \
	vio2sf/desmume/NDSSystem.c \
	aosdk/corlett.c \
	aosdk/eng_dsf/eng_dsf.c \
	aosdk/eng_dsf/dc_hw.c \
	aosdk/eng_dsf/arm7.c \
	aosdk/eng_dsf/arm7i.c \
	aosdk/eng_dsf/aica.c \
	aosdk/eng_dsf/aicadsp.c \
	aosdk/eng_ssf/scspdsp.c \
	aosdk/eng_ssf/m68kopnz.c \
	aosdk/eng_ssf/m68kcpu.c \
	aosdk/eng_ssf/scsp.c \
	aosdk/eng_ssf/sat_hw.c \
	aosdk/eng_ssf/m68kops.c \
	aosdk/eng_ssf/m68kopac.c \
	aosdk/eng_ssf/eng_ssf.c \
	aosdk/eng_ssf/m68kopdm.c \
	aosdk/eng_psf/psx.c \
	aosdk/eng_psf/eng_psf2.c \
	aosdk/eng_psf/eng_psf.c \
	aosdk/eng_psf/eng_spu.c \
	aosdk/eng_psf/psx_hw.c \
	aosdk/eng_psf/peops2/spu.c \
	aosdk/eng_psf/peops2/dma.c \
	aosdk/eng_psf/peops2/registers.c \
	aosdk/eng_psf/peops/spu.c \
	zlib/adler32.c \
	zlib/crc32.c \
	zlib/infback.c \
	zlib/inffast.c \
	zlib/inflate.c \
	zlib/zutil.c \
	zlib/inftrees.c \
	zlib/uncompr.c

#
# Get pepper directory for toolchain and includes.
#
# If NACL_SDK_ROOT is not set, then assume it can be found a two directories up,
# from the default example directory location.
#
THIS_MAKEFILE:=$(abspath $(lastword $(MAKEFILE_LIST)))
NACL_SDK_ROOT?=$(abspath $(dir $(THIS_MAKEFILE))../..)

# Project Build flags
WARNINGS:=-Wno-long-long -Wall -Wswitch-enum -pedantic
INC_PATHS:=-Iaosdk -Igme-source -Ivio2sf -Ixz-embedded -Izlib
CXXFLAGS:=-pthread -std=gnu++98 $(WARNINGS) $(INC_PATHS)
CFLAGS:=-pthread -std=c99 $(WARNINGS) $(INC_PATHS)

#
# Compute tool paths
#
#
OSNAME:=$(shell python $(NACL_SDK_ROOT)/tools/getos.py)
TC_PATH:=$(abspath $(NACL_SDK_ROOT)/toolchain/$(OSNAME)_x86_newlib)
CXX:=$(TC_PATH)/bin/i686-nacl-g++
CC:=$(TC_PATH)/bin/i686-nacl-gcc

#
# Disable DOS PATH warning when using Cygwin based tools Windows
#
CYGWIN ?= nodosfilewarning
export CYGWIN


# Declare the ALL target first, to make the 'all' target the default build
all: $(PROJECT)_x86_32.nexe $(PROJECT)_x86_64.nexe

# Define 32 bit compile and link rules for main application
x86_32_C_OBJS:=$(patsubst %.c,%_32.o,$(C_SOURCES))
$(x86_32_C_OBJS) : %_32.o : %.c $(THIS_MAKE)
	$(CC) -o $@ -c $< -m32 -O0 -g $(CFLAGS)

x86_32_CXX_OBJS:=$(patsubst %.cpp,%_32.o,$(CXX_SOURCES))
$(x86_32_CXX_OBJS) : %_32.o : %.cpp $(THIS_MAKE)
	$(CXX) -o $@ -c $< -m32 -O0 -g $(CXXFLAGS)

$(PROJECT)_x86_32.nexe : $(x86_32_CXX_OBJS) $(x86_32_C_OBJS)
	$(CXX) -o $@ $^ -m32 -O0 -g $(CXXFLAGS) $(CFLAGS) $(LDFLAGS)

# Define 64 bit compile and link rules for C++ sources
x86_64_C_OBJS:=$(patsubst %.c,%_64.o,$(C_SOURCES))
$(x86_64_C_OBJS) : %_64.o : %.c $(THIS_MAKE)
	$(CC) -o $@ -c $< -m64 -O0 -g $(CFLAGS)

x86_64_CXX_OBJS:=$(patsubst %.cpp,%_64.o,$(CXX_SOURCES))
$(x86_64_CXX_OBJS) : %_64.o : %.cpp $(THIS_MAKE)
	$(CXX) -o $@ -c $< -m64 -O0 -g $(CXXFLAGS)

$(PROJECT)_x86_64.nexe : $(x86_64_CXX_OBJS) $(x86_64_C_OBJS)
	$(CXX) -o $@ $^ -m64 -O0 -g $(CXXFLAGS) $(CFLAGS) $(LDFLAGS)

# Define a phony rule so it always runs, to build nexe and start up server.
.PHONY: RUN 
RUN: all
	python ../httpd.py
