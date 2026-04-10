###################################################
# Constants
###################################################
TARGET := scache

SRC_DIRS := ./src
BUILD_DIR := ./build
DEBUG_DIR := $(BUILD_DIR)/debug
OUTPUT_DIR := ./output

# find the source files, extract the filenames, 
# stick them in the build dir as .o
SRC := $(shell find $(SRC_DIRS) -name '*.cpp')
FILENAMES := $(basename $(notdir $(SRC)))
OBJS := $(FILENAMES:%=$(BUILD_DIR)/%.o)
DEBUG_OBJS := $(FILENAMES:%=$(DEBUG_DIR)/%.o)

# Flags for g++
CPPFLAGS := -O3 -Wall -Wextra -Werror
DEBUG_CPPFLAGS := $(CPPFLAGS) -g

# Phony targets (do not represent a file)
.PHONY: remake debug clean 

###################################################
# Targets
###################################################
# Use "order only" prereqs to make sure dirs are created
# Use the phony to make sure that we always copy the correct
# exe over
all: $(BUILD_DIR)/$(TARGET) | $(BUILD_DIR) $(OUTPUT_DIR)
	cp $(BUILD_DIR)/$(TARGET) ./$(TARGET)

# Redundant target for ease of use
$(TARGET): all

# Directory targets, silent
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
$(DEBUG_DIR):
	@mkdir -p $(DEBUG_DIR)
$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

# Main and debug targets
$(BUILD_DIR)/$(TARGET): $(OBJS) | $(BUILD_DIR)
	g++ $(CPPFLAGS) $(OBJS) -o $(BUILD_DIR)/$(TARGET)

# Construct a unique compilation step for each src->object
# to minimize re-building. Depends on the src file
# and order-only on the directory.
define OBJ_COMP_TEMPLATE =
$(1)/$(basename $(notdir $(2))).o: $(2) | $(1)
	g++ -c $(3) $(2) -o $$@
endef

# Instantiate & evaluate each of the object build steps for f in $(SRC)
$(foreach f,$(SRC),$(eval $(call OBJ_COMP_TEMPLATE,$(BUILD_DIR),$(f),$(CPPFLAGS))))

#-------------------------------------------------- 
# Debug targets
#-------------------------------------------------- 
debug: $(DEBUG_DIR)/$(TARGET) | $(DEBUG_DIR) $(OUTPUT_DIR)
	cp $(DEBUG_DIR)/$(TARGET) ./$(TARGET)

$(DEBUG_DIR)/$(TARGET): $(DEBUG_OBJS) | $(DEBUG_DIR)
	g++ $(DEBUG_CPPFLAGS) $(DEBUG_OBJS) -o $(DEBUG_DIR)/$(TARGET)

$(foreach f,$(SRC),$(eval $(call OBJ_COMP_TEMPLATE,$(DEBUG_DIR),$(f),$(DEBUG_CPPFLAGS))))

#-------------------------------------------------- 
# Helpful phonies
#-------------------------------------------------- 
remake: clean all

clean:
	-rm -r $(DEBUG_DIR) $(BUILD_DIR) $(OUTPUT_DIR) $(TARGET) 
