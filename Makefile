# Copyright (C) 2026 erixabo
# SPDX-License-Identifier: GPL-3.0-only

CXX ?= g++
CPPFLAGS += -I/usr/local/include
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++11
LDFLAGS += -L/usr/local/lib
LDLIBS += -lrpitx -lm -lrt -lpthread -lbcm_host

TARGET := build/pisstv
SOURCE := src/pisstv_pd240_qsstv_timing.cpp

.PHONY: all clean install
all: $(TARGET)

$(TARGET): $(SOURCE)
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

install: $(TARGET)
	install -D -m 0755 $(TARGET) /usr/local/bin/pisstv
	install -D -m 0755 tools/prepare_sstv_image.py /usr/local/bin/pisstv-prepare-image
	install -D -m 0644 LICENSE /usr/local/share/doc/pisstv/LICENSE
	install -D -m 0644 THIRD-PARTY-NOTICES.md /usr/local/share/doc/pisstv/THIRD-PARTY-NOTICES.md
	install -D -m 0644 README.md /usr/local/share/doc/pisstv/README.md

clean:
	rm -rf build
