#include <iostream>
#include <fstream>
#include <stdio.h>
#include <cstring>

#include "compiler.h"
using namespace Compiler;

// Expect that source_file_name is null terminated
void copy_between_tag_hit(FILE* destination, FILE* source, char* open_tag, char* close_tag);

SplitFileNames SeperateSource(FILE* source_file, Arena* file_name_arena, char* source_file_name, char* output_dir)
{
    SplitFileNames result;
    
    // TODO(Leo): Continue here. Swap this to use the new output_dir aswell so files can be put into the build dir
    
    // Find where the . in the filename is located
    int real_name_length = 0;
    while(*(source_file_name + real_name_length) != '\0')
    {
        if(*(source_file_name + real_name_length) == '.')
        {
            break;
        }
        real_name_length++;
    }
    
    // Remove previously generated files
    char* output_name_buffer;
    char* real_name;
    
    int output_dir_name_len = strlen(output_dir);
    
    output_name_buffer = (char*)Alloc(file_name_arena, (output_dir_name_len + real_name_length + LARGEST_FILE_TYPE_LENGTH + 2)*sizeof(char)); // +2 to fit / and \0
    real_name = (char*)Alloc(file_name_arena, (real_name_length + 1)*sizeof(char)); // +1 to fitt \0
    
    
    // Name of the source file without filetype
    memcpy(real_name, source_file_name, real_name_length);
    *(real_name + real_name_length) = '\0'; // add null terminator back
    
    int final_length = 0;
    // Style file name
    final_length = sprintf(output_name_buffer, "%s/%s.style", output_dir, real_name) + 1; // Sprintf doesnt include \0 in length, add 1!

    result.style_file_name = (char*)Alloc(file_name_arena, final_length*sizeof(char));
    memcpy(result.style_file_name, output_name_buffer, final_length);
    printf("Removing: %s\n", output_name_buffer);
    
    remove(output_name_buffer); // Remove old file
    
    FILE* style_file = fopen(output_name_buffer, "w"); // Recreate file
    
    // Markup file name
    final_length = sprintf(output_name_buffer, "%s/%s.markup", output_dir, real_name) + 1;
    result.markup_file_name = (char*)Alloc(file_name_arena, final_length*sizeof(char));
    memcpy(result.markup_file_name, output_name_buffer, final_length);
    printf("Removing: %s\n", output_name_buffer);
    
    remove(output_name_buffer);
    
    FILE* markup_file = fopen(output_name_buffer, "w");
    
    // Code file name
    final_length = sprintf(output_name_buffer, "%s/%s.cpp", output_dir, real_name) + 1;
    result.code_file_name = (char*)Alloc(file_name_arena, final_length*sizeof(char));
    memcpy(result.code_file_name, output_name_buffer, final_length);
    printf("Removing: %s\n", output_name_buffer);
    
    remove(output_name_buffer);
    
    FILE* code_file = fopen(output_name_buffer, "w");
    
    // Header file name
    final_length = sprintf(output_name_buffer, "%s/%s.h", output_dir, real_name) + 1;
    result.header_file_name = (char*)Alloc(file_name_arena, final_length*sizeof(char));
    memcpy(result.header_file_name, output_name_buffer, final_length);
    printf("Removing: %s\n", output_name_buffer);
    
    remove(output_name_buffer);
    
    // Break out cpp code
    copy_between_tag_hit(code_file, source_file, "<code>", "</code>");
    
    // Break out markup code
    copy_between_tag_hit(markup_file, source_file, "<root>", "</root>");    
    
    // Break out the style code
    copy_between_tag_hit(style_file, source_file, "<style>", "</style>");
    
    fclose(style_file);
    fclose(markup_file);
    fclose(code_file);
    
    return result;
}


// NOTE: FOR this to work the source MUST have the tag exactly as in the tag string
void copy_between_tag_hit(FILE* destination, FILE* source, char* open_tag, char* close_tag)
{
    int next_char;
    char* checked_tag = open_tag; 
    while((next_char = fgetc(source)) != EOF)
    {
        // potential match, need to save the chars we pop incase it isnt a real match
        if(next_char == *checked_tag)
        {
            char* saved_string = (char*)AllocScratch(sizeof(char)*LONGEST_META_TAG_LENGTH);
            int compared_index = 0;
            while(*(checked_tag + compared_index) != '\0' && next_char != EOF)
            {   
                *(saved_string + compared_index) = next_char;
                
                // Unsuccesfull match
                if(*(checked_tag + compared_index) != *(saved_string + compared_index))
                {
                    // Put our chars into the destination
                    for(int i = 0; i < compared_index; i++)
                    {
                        putc(*(saved_string + i), destination);
                    }
                    DeAllocScratch(saved_string);
                    compared_index = 0;
                    break;
                }
                compared_index++;
                next_char = fgetc(source);
            }
            // Indicates a succesfull match
            if(compared_index != 0)
            {
                //found the end tag
                if(checked_tag == close_tag)
                {
                return;
                }
                else{ // found the start tag
                checked_tag = close_tag;                
                }

            }
            
        }
        // No match
        if(checked_tag == open_tag)
        {
            continue;
        }
        putc(next_char, destination);
        
    }
    printf("Expected a %s tag!", open_tag);

}