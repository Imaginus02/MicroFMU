#!/bin/bash

# Fichier XML à analyser
xml_file="fmu/modelDescription.xml"

# Fonction pour extraire les attributs et le contenu des balises
extract_attributes_and_content() {
    local tag=$1
    xmllint --xpath "//$tag" "$xml_file" | while read -r line; do
        # Extraire les attributs
        attributes=$(echo "$line" | xmllint --xpath 'name(/*) | //@*' - 2>/dev/null | tr '\n' ' ')
        
        # Extraire le contenu
        content=$(echo "$line" | xmllint --xpath 'string(//*)' - 2>/dev/null)
        
        # Afficher les attributs et le contenu
        echo "Tag: $tag"
        echo "Attributes: $attributes"
        echo "Content: $content"
        echo "----------------------"
    done
}

# Exemple d'utilisation pour une balise spécifique
extract_attributes_and_content "ScalarVariable"