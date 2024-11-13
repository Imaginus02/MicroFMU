.PHONY: all clean

# Variable directory for user C modules
CLIBRARY_MOD_DIR := $(USERMOD_DIR)
# Ajouter tous les fichiers C à SRC_USERMOD :
SRC_USERMOD += $(CLIBRARY_MOD_DIR)/main.c
SRC_USERMOD += $(CLIBRARY_MOD_DIR)/fmu/sources/all.c
# Ajouter le chemin d'inclusion des headers C, si nécessaire
CFLAGS_USERMOD += -I$(CLIBRARY_MOD_DIR) -I$(CLIBRARY_MOD_DIR)/headers -I$(CLIBRARY_MOD_DIR)/fmu/sources -Wall -g -DFMI_VERSION=2 -DModelFMI_COSIMULATION=0 -DMODEL_IDENTIFIER=BouncingBall -DFMI2_OVERRIDE_FUNCTION_PREFIX="" -fno-common


all :
	@echo "Building test-library MicroPython module"

	# Décompression de l'archive .fmu
	find $(CLIBRARY_MOD_DIR) -name "*.fmu" -exec unzip -o {} -d $(CLIBRARY_MOD_DIR)/fmu \;
	@echo "FMU extracted"

# clean:
# 	@echo "Cleaning up..."
# 	# Supprimer le dossier fmu et son contenu
# 	rm -rf fmu
# Ajout à la règle clean de MicroPython
clean += rm -rf $(CLIBRARY_MOD_DIR)/fmu
clean += @echo "FMU directory removed"
