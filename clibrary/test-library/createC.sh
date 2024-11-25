#!/bin/bash

output_file="output.c"

# Start the C file with includes and struct definition
cat <<EOT > "$output_file"
#include <stdio.h>
#include <stdlib.h>

typedef enum { INTEGER, REAL } VarType;

typedef struct {
    char *name;
    unsigned int valueReference;
    char *description;
    VarType type;
    union {
        int intValue;
        double realValue;
    } start;
    union {
        int intMin;
        double realMin;
    } min;
    union {
        int intMax;
        double realMax;
    } max;
} ScalarVariable;


void initialize(ScalarVariable *var, char *name, int valueReference, char *description,
                VarType type, void *start, void *min, void *max) {
    var->name = name;
    var->valueReference = valueReference;
    var->description = description;
    var->type = type;
    if (type == INTEGER) {
        var->start.intValue = start ? *(int*)start : 0;
        var->min.intMin = min ? *(int*)min : 0;
        var->max.intMax = max ? *(int*)max : 0;
    } else {
        var->start.realValue = start ? *(double*)start : 0.0;
        var->min.realMin = min ? *(double*)min : 0.0;
        var->max.realMax = max ? *(double*)max : 0.0;
    }
}

int get_variable_list(ScalarVariable **variables) {
EOT

# Read input and populate variables
counter=1
temp_output=""

while IFS= read -r line; do
    # Parse each attribute from the line
    name=$(echo "$line" | grep -oP 'name="\K[^"]*')
    valueReference=$(echo "$line" | grep -oP 'valueReference="\K[^"]*')
    valueReference=$((valueReference + 1))
    description=$(echo "$line" | grep -oP 'description="\K[^"]*')
    type=$(echo "$line" | grep -oP 'type="\K[^"]*')
    start=$(echo "$line" | grep -oP 'start="\K[^"]*')
    min=$(echo "$line" | grep -oP 'min="\K[^"]*')
    max=$(echo "$line" | grep -oP 'max="\K[^"]*')

    # Set default values if fields are missing
    valueReference=${valueReference:-0}
    description=${description:-NULL}
    type_enum="REAL"
    type_var="double"
    start_val=0.0
    min_val=0.0
    max_val=0.0

    # Possibility : REAL CHAR BOOLEAN

    # Adjust types and defaults based on type
    if [ "$type" = "Integer" ]; then
        type_enum="INTEGER"
        type_var="int"
        start_val=${start:-0}
        min_val=${min:-0}
        max_val=${max:-0}
    else
        start_val=${start:-0.0}
        min_val=${min:-0.0}
        max_val=${max:-0.0}
    fi

    # Generate C code to initialize each variable
    temp_output+=$(cat <<EOT
    {
        ScalarVariable var;
        char *name = "$name";
        char *description = "$description";
        const unsigned int valueReference = $valueReference;
        VarType type = $type_enum;
        ${type_var} start = ${start_val};
        ${type_var} min = ${min_val};
        ${type_var} max = ${max_val};
        initialize(&var, name, valueReference, description, type, &start, &min, &max);
        (*variables)[$counter] = var;
    }
EOT
)
    temp_output+="\n"
    counter=$((counter + 1))
done

# alloc memory for variables

echo "    (*variables) = (ScalarVariable*)calloc($counter, sizeof(ScalarVariable));" >> "$output_file"
echo "    {" >> "$output_file"
echo "        ScalarVariable var;" >> "$output_file"
echo "        char *name = \"Unknown\";" >> "$output_file"
echo "        char *description = \"Unknown\";" >> "$output_file"
echo "        const unsigned int valueReference = 0;" >> "$output_file"
echo "        VarType type = REAL;" >> "$output_file"
echo "        double start = 0.0;" >> "$output_file"
echo "        double min = 0.0;" >> "$output_file"
echo "        double max = 0.0;" >> "$output_file"
echo "        initialize(&var, name, valueReference, description, type, &start, &min, &max);" >> "$output_file"
echo "        (*variables)[0] = var;" >> "$output_file"
echo "    }" >> "$output_file"


# Append the generated code to the output file
echo -e "$temp_output" >> "$output_file"

# End of the function
echo "    return 0;" >> "$output_file"
echo "}" >> "$output_file"

# Function to get the number of variables
echo "int get_variable_count() {" >> "$output_file"
echo "    return $counter;" >> "$output_file"
echo "}" >> "$output_file"

#Constant number of variables
echo "#define NVARIABLES $counter" >> "$output_file"

#End of the file

# Notify user
echo "Generated $output_file"
