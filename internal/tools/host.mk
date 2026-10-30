CC = xcrun clang
GAME ?= silksong
CONFIG ?= build/games/$(GAME)/host/config.mk
.DEFAULT_GOAL := all
ifeq ($(wildcard $(CONFIG)),)
$(error Run bash tools/build_host.sh --game $(GAME) first)
endif
include $(CONFIG)
FRAMEWORKS = Cocoa QuartzCore Metal AudioToolbox ImageIO UniformTypeIdentifiers GameController $(GAME_FRAMEWORKS)

all: $(HOST_OUTPUT)
$(HOST_OUTPUT): $(OBJECTS) tools/host.mk $(CONFIG)
	$(CC) $(FLAGS) $(OBJECTS) $(foreach f,$(FRAMEWORKS),-framework $(f)) -o "$@"
-include $(OBJECTS:.o=.d)
