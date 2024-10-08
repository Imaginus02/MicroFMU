CLIBRARY_MOD_DIR := $(USERMOD_DIR)

# Ajouter tout les fichiers C à SRC_USERMOD :
SRC_USERMOD += $(CLIBRARY_MOD_DIR)/testlibrary.c

# We can add our module folder to include paths if needed
# This is not actually needed in this example.
CFLAGS_USERMOD += -I$(CLIBRARY_MOD_DIR)