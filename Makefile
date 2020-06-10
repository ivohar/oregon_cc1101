TARGET_APP=oregon_read
INSTALL_DIR=/opt/vc/bin
INIT_DIR=/etc/init.d/
INIT_SCRIPT=oregon_cc1101.sh
# SYSGCC_INSTALL_BIN := C:\SysGCC\Raspberry\bin
# SYSGCC_PREFIX := arm-linux-gnueabihf-
CXX := $(SYSGCC_INSTALL_BIN)$(SYSGCC_PREFIX)g++
OUTPUT_DIRECTORY := build
MK := mkdir
RM := rm -rf

LIBS = -lwiringPi cc1101_oregon.cpp
DEPS = $(wildcard cc1101_oregon.*)
# OPT = -O3 -g3
OPT = -O3 

default: app

all: app 

app: $(OUTPUT_DIRECTORY)/$(TARGET_APP)

$(OUTPUT_DIRECTORY)/$(TARGET_APP): $(TARGET_APP).cpp $(DEPS) | $(OUTPUT_DIRECTORY)
	$(CXX) $(OPT) $(LIBS) $< -o $@

$(OUTPUT_DIRECTORY):
	$(MK) $@
	
install: $(OUTPUT_DIRECTORY)/${TARGET_APP} 
	if [ ! -d $(INSTALL_DIR) ]; then mkdir -p $(INSTALL_DIR);fi
	cp $(OUTPUT_DIRECTORY)/$(TARGET_APP) $(INSTALL_DIR)/$(TARGET_APP)
	cp $(INIT_SCRIPT) $(INIT_DIR)/$(INIT_SCRIPT)
	chmod a+x $(INIT_DIR)/$(INIT_SCRIPT)
	update-rc.d $(INIT_SCRIPT) defaults

clean:
	$(RM) $(OUTPUT_DIRECTORY)
	
