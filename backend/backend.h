#include "arena.h"

enum class ElementType
{
    NONE,
    ROOT,
    DIV
};

struct CompiledStyle {
    int id;
    int selector_ids[20];    
    int priority;
};

struct Element
{
    Element* parent;
    Element* next_sibling;
    Element* first_child;

    ElementType type;
    
    // NOTE: Fixed size of 20 selectors and 20 applied styles atm. can move into a different more dynamic format later
    int selector_ids[20];
    int style_ids[20];
};

struct DOM
{
    Arena* document_tree;
    Element* root_element;
    
    Arena* compiled_styles; // Styles ordered in a binary tree based on their ids.
    
};


static DOM* dom;

// Initialize the DOM
void InitializeDOM();

void CalculateStyles();

void BuildRenderque();

void Draw();