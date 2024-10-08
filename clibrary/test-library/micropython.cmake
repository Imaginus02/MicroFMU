#Création d'une interface pour nos librairie C :
add_library(usermod_clibrary INTERFACE)

# Ajouter nos fichiers sources :
target_sources(usermod_clibrary INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/testlibrary.c
)

# AJouter le dossier en tant que "include" :
target_include_directories(usermod_clibrary INTERFACE
	${CMAKE_CURRENT_LIST_DIR}
)

# Liaison de l'INTERFACE à la cible usermod :
target_link_libraries(usermod INTERFACE usermod_clibrary)