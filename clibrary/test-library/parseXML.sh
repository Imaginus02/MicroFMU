#!/bin/bash

# Check if tag name is provided
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <tag_name>"
    exit 1
fi

# Get the tag name from the argument
tag_name="$1"

# Define XML file path
xml_file="fmu/modelDescription.xml"  # Update with your actual file path if needed

# Check if the XML file exists
if [ ! -f "$xml_file" ]; then
    echo "Error: XML file not found at $xml_file"
    exit 1
fi

# Use awk to parse and output content between specified tags in a single line with attributes and nested tags
awk -v tag="$tag_name" '
    BEGIN { RS="</" tag ">"; ORS="\n" }  # Set record separator and output separator
    {
        # Find the opening tag and extract attributes and content
        if (match($0, "<" tag "[^>]*>")) {
            header = substr($0, RSTART, RLENGTH)   # Capture the opening tag with attributes
            content = substr($0, RSTART + RLENGTH) # Capture everything after the opening tag

            # Extract attributes within the opening tag
            attributes = ""
            while (match(header, /[a-zA-Z0-9_]+="[^"]*"/)) {
                attributes = attributes (attributes ? " " : "") substr(header, RSTART, RLENGTH)
                header = substr(header, RSTART + RLENGTH)
            }
            
            # Replace nested tags like <Real ... /> with type="Real" ... (remove angle brackets)
            nested_content = ""
            while (match(content, /<([a-zA-Z]+)[^>]*\/>/)) {
                tag_content = substr(content, RSTART, RLENGTH)   # Extract full tag content
				content = substr(content, RSTART + RLENGTH)  # Remove the processed tag from content
				tag_name = substr(tag_content, 2, match(tag_content, /\s/)-2)  # Extract tag name (e.g., Real, Integer)
                # Remove the angle brackets and replace with type="tag_name"
                gsub(/<[a-zA-Z]+/, "type=\"" tag_name "\"", tag_content)  # Replace <Real -> type="Real"
				gsub(/\/>/, "", tag_content)  # Remove the trailing />
                nested_content = nested_content (nested_content ? " " : "") tag_content
            }

            # Replace newlines with spaces in the remaining content and trim whitespace
            gsub(/\n/, " ", content)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", content)
            
            # Print attributes, content, and nested content on one line without extra spaces
            output = attributes
            if (content != "") output = output (output ? " " : "") content
            if (nested_content != "") output = output (output ? " " : "") nested_content
            
            print output
        }
    }
' "$xml_file"
