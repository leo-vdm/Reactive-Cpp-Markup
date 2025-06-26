#include <map>
#include "dom_attatchment.h"
#include "file_system.h"
#include "compiler.h"
#include "platform.h"

std::map<int, LoadedFileHandle*> file_id_map = {};
std::map<std::string, LoadedFileHandle*> file_name_map = {};

void InitRuntime(Arena* master_arena, Runtime* target)
{
    target->master_arena = master_arena;
    target->loaded_files = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_tags = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_templates = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_attributes = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_styles = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_selectors = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->static_combined_values = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->doms = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->selectors = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->styles = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->bound_expressions = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->strings = (Arena*)Alloc(master_arena, sizeof(Arena));

    *(target->loaded_files) = CreateArena(100*sizeof(LoadedFileHandle), sizeof(LoadedFileHandle));
    *(target->loaded_tags) = CreateArena(10000*sizeof(Compiler::Tag), sizeof(Compiler::Tag));
    *(target->loaded_templates) = CreateArena(1000*sizeof(BodyTemplate), sizeof(BodyTemplate));
    *(target->loaded_attributes) = CreateArena(10000*sizeof(Compiler::Attribute), sizeof(Compiler::Attribute));
    *(target->loaded_styles) = CreateArena(10000*sizeof(Compiler::Style), sizeof(Compiler::Style));
    *(target->loaded_selectors) = CreateArena(10000*sizeof(Compiler::Selector), sizeof(Compiler::Selector));
    *(target->static_combined_values) = CreateArena(100000*sizeof(char), sizeof(char));
    *(target->doms) = CreateArena(100*sizeof(DOM), sizeof(DOM));
    *(target->selectors) = CreateArena(100*sizeof(Selector), sizeof(Selector));
    *(target->styles) = CreateArena(100*sizeof(Style), sizeof(Style));
    *(target->bound_expressions) = CreateArena(1000*sizeof(BoundExpression), sizeof(BoundExpression));
    *(target->strings) = CreateArena(1000*sizeof(StringBlock), sizeof(StringBlock));
}

Runtime runtime;

std::map<std::string, Selector*> selector_map = {};

// Note(Leo): Selectors are indexed as an array by their global ID
void ConvertSelectors(Compiler::Selector* selector)
{
    assert(selector);
    Selector* arena_base = (Selector*)runtime.selectors->mapped_address;
    Compiler::Selector* curr_selector = selector;

    while(curr_selector->global_id)
    {
        // Test if we need to allocate more space for this selector to have its slot
        if((arena_base + curr_selector->global_id) >= (Selector*)runtime.selectors->next_address) 
        {
            // + 1 since if we are allocated up to the required address we are still 1 Selector short
            int allocated_count = ((arena_base + curr_selector->global_id) + 1) - (Selector*)runtime.selectors->next_address;
            Alloc(runtime.selectors, sizeof(Selector) * allocated_count, zero());
        }    
        
        Selector* added_selector = arena_base + curr_selector->global_id;
        added_selector->id = curr_selector->global_id;
        
        added_selector->style_count = curr_selector->num_styles;
        memcpy(added_selector->style_ids, curr_selector->style_ids, curr_selector->num_styles*sizeof(int));
        added_selector->name_length = curr_selector->name_length;
        added_selector->name = curr_selector->name;
        
        // TODO(Leo): The compiler seems to add null terminators after selector names but it is not clear where that is happening
        // so this code doesnt rely on names coming in null terminated. Figure out why its happening. 
        
        char* terminated_name = (char*)AllocScratch((curr_selector->name_length + 1)*sizeof(char)); // +1 to fit \0
        memcpy(terminated_name, curr_selector->name, curr_selector->name_length*sizeof(char));
        terminated_name[curr_selector->name_length] = '\0';
        
        selector_map.insert({(const char*)terminated_name, added_selector});
        DeAllocScratch(terminated_name);
        curr_selector++;
    }
}

// Note(Leo): Styles are indexed as an array by their global ID 
void ConvertStyles(Compiler::Style* style)
{
    assert(style);
    Style* arena_base = (Style*)runtime.styles->mapped_address;
    Compiler::Style* curr_style = style;
    while(curr_style->global_id)
    {
        // Test if we need to allocate more space for this style to have its slot
        if((arena_base + curr_style->global_id) >= (Style*)runtime.styles->next_address) 
        {
            // + 1 since if we are allocated up to the required address we are still 1 Style short
            int allocated_count = ((arena_base + curr_style->global_id) + 1) - (Style*)runtime.styles->next_address;
            Alloc(runtime.styles, sizeof(Style) * allocated_count, zero());
        }    

        Style* added_style = arena_base + curr_style->global_id;
        memcpy(added_style, curr_style, sizeof(Style));

        // Indicates the style has a font to use
        if(curr_style->font_name.len > 0)
        {
            char* terminated_name = (char*)AllocScratch((curr_style->font_name.len + 1)*sizeof(char));
            memcpy(terminated_name, curr_style->font_name.value, curr_style->font_name.len*sizeof(char));
            terminated_name[curr_style->font_name.len] = '\0';
            
            added_style->font_id = FontPlatformGetFont(terminated_name);
            
            if(!added_style->font_id)
            {
                // Try loading the font
                PlatformFile opened = PlatformOpenFile(terminated_name);
                if(!opened.data)
                {
                    printf("Error while loading font '%s'\n", terminated_name);
                    added_style->font_id = 1; // Use the default font instead             
                }
                else
                {
                    FontPlatformLoadFace(terminated_name, &opened);
                    added_style->font_id = FontPlatformGetFont(terminated_name);
                    PlatformCloseFile(&opened);
                }
            
            }
            
            if(!added_style->font_id)
            {
                printf("Error while loading font '%s'\n", terminated_name);
                added_style->font_id = 1; // Use the default font instead             
            }
            
            DeAllocScratch(terminated_name);
        }
        else // Use the default font
        {
            added_style->font_id = 1;
        }
        
        curr_style++;
    }
}


int InitializeRuntime(Arena* master_arena, FileSearchResult* first_binary)
{
    // Initialize Runtime
    
    InitRuntime(master_arena, &runtime);

    FileSearchResult* curr = first_binary;
        
    // Load all page/comp binaries
    while(curr->file_path)
    {
        printf("Found binary \"%s\"\n", curr->file_name);
        //FILE* bin_file = fopen(curr->file_path, "rb");
        // Note(Leo): Files should be opened with "rb" options
        FILE* bin_file = curr->file;
        
        LoadedFileHandle* loaded_bin = (LoadedFileHandle*)Alloc(runtime.loaded_files, sizeof(LoadedFileHandle));
        *loaded_bin = LoadPage(bin_file, runtime.loaded_tags, runtime.loaded_templates, runtime.loaded_attributes, runtime.loaded_styles, runtime.loaded_selectors, runtime.static_combined_values);
        assert(loaded_bin);
        
        ConvertSelectors(loaded_bin->first_selector);
        ConvertStyles(loaded_bin->first_style);
        
        file_id_map[loaded_bin->file_id] = loaded_bin;
    
        int name_len = strlen(curr->file_name);
        name_len -= 3; // -4 to exclude .bin, + 1 to fit \0
        char* name = (char*)AllocScratch(sizeof(name_len), no_zero());
        memcpy(name, curr->file_name, name_len);
        name[name_len - 1] = '\0';
        
        std::string name_string = name;
        
        file_name_map[name_string] = loaded_bin;
        printf("Registering under name \"%s\"\n", name);
        
        DeAllocScratch(name);
        
        fclose(bin_file);
        
        curr++;
    }
    
    register_binding_subscriptions(&runtime);
    
    return 0;
}

bool RuntimeInstanceMainPage()
{
    LoadedFileHandle* main_page = GetFileFromName("MainPage");
    if(!main_page)
    {
        return false;
    }

    DOM* main_dom = (DOM*)Alloc(runtime.doms, sizeof(DOM), zero());    
    InitDOM(runtime.master_arena, main_dom);
    
    SwitchPage(main_dom, main_page->file_id);
    
    PlatformRegisterDom((void*)main_dom);
    return true;
}

bool PointInsideBounds(const bounding_box bounds, const vec2 point)
{
    return point.x >= bounds.x && point.x <= (bounds.x + bounds.width) && point.y >= bounds.y && point.y <= (bounds.y + bounds.height);
}

// Creates a null terminated global name from a selector's name
// Note(Leo): De-allocating name from scratch space is the caller's responsibility
char* mangle_selector_name(StringView name, int file_id)
{
    int global_name_length = snprintf(NULL, 0, "%d-%.*s", file_id, name.len, name.value);
    global_name_length++; // +1 to fit \0
    char* global_name = (char*)AllocScratch(sizeof(char)*global_name_length);
    sprintf(global_name, "%d-%.*s", file_id, name.len, name.value);
    return global_name;
}

LoadedFileHandle* GetFileFromId(int id)
{
    auto search = file_id_map.find(id);
    if(search != file_id_map.end())
    {
        return search->second;
    }
    
    return NULL;
}

LoadedFileHandle* GetFileFromName(const char* name)
{
    std::string name_string;
    name_string = name;
    auto search = file_name_map.find(name_string);
    if(search != file_name_map.end())
    {
        return search->second;
    }
    
    return NULL;
}

void SwitchPage(DOM* dom, int id, int flags)
{
    if(flags & save_dom())
    {
        printf("DOM state saving not implemented yet!\n");    
    }
    
    // Clear elements/data from dom
    ResetArena(dom->cached_cstrings);
    ResetArena(dom->dynamic_cstrings);
    ResetArena(dom->strings);
    ResetArena(dom->pointer_arrays);
    ResetArena(dom->elements);
    ResetArena(dom->attributes);
    ResetArena(dom->events);
    dom->focused_element = NULL;
    
    InstancePage(dom, id);    
}

void SwitchPage(DOM* dom, const char* name, int flags)
{
    LoadedFileHandle* page = GetFileFromName(name);
    if(!page)
    {
        return;
    }
    
    // Queue the page switch
    dom->switch_request.file_id = page->file_id;
    dom->switch_request.flags = flags;
}

Selector* GetSelectorFromName(const char* name)
{
    auto search = selector_map.find(name);
    if(search != selector_map.end())
    {
        return search->second;
    }
    
    return NULL;
}

inline Style* GetStyleFromID(int style_id)
{
    assert(style_id >= 0);
    return ((Style*)runtime.styles->mapped_address) + style_id;
}

InFlightStyle* merge_selector_styles(Selector* selector)
{   
    // Note(Leo): We save the merged style into the selector as a cache, if we see that the selector has a valid
    //            cached value we early return.
    if(selector->flags & cache_valid())
    {
        return &selector->cached_style;
    }
    
    selector->flags = selector->flags | cache_valid();
    
    InFlightStyle* merged_style = &selector->cached_style;
    
    // Note(Leo): Since styles can currently have priority 0, we must ensure that default style has -1 priority otherswise
    // priority 0 styles will be ignored
    DefaultStyle(merged_style);
    
    for(int i = 0; i < selector->style_count; i++)
    {
        MergeStyles(merged_style, GetStyleFromID(selector->style_ids[i]));
    }
    
    return merged_style;
}

// Gets a selector from the global pool by mangling its name into a global name
Selector* GetGlobalSelector(StringView local_name, int file_id)
{
    int global_name_length = snprintf(NULL, 0, "%d-%.*s", file_id, local_name.len, local_name.value);
    global_name_length++; // +1 to fit \0
    char* global_name = (char*)AllocScratch(sizeof(char)*global_name_length);
    sprintf(global_name, "%d-%.*s", file_id, local_name.len, local_name.value);
    
    Selector* found = GetSelectorFromName(global_name);
    
    DeAllocScratch(global_name);
    return found;
}

// Merge the style of an element based on its type selector into the given style
void merge_element_type_style(ElementType type, bool is_hovered, int file_id, InFlightStyle* target)
{
    ArenaString* selector = CreateString(runtime.strings);
    switch(type)
    {
        case(ElementType::ROOT):
        {
            Append(selector, "root");
            break;
        }
        case(ElementType::HDIV):
        {
            Append(selector, "hdiv");
            break;
        }
        case(ElementType::VDIV):
        {
            Append(selector, "hdiv");
            break;
        }
        case(ElementType::IMG):
        {
            Append(selector, "img");
            break;
        }
        case(ElementType::GRID):
        {
            Append(selector, "grid");
            break;
        }
        default:
        {
            break;
        }
    }
    
    char* base_name = Flatten(selector);
    Selector* base_selector = GetGlobalSelector({base_name, (uint32_t)selector->length}, file_id);
    
    if(base_selector)
    {
        InFlightStyle* base_style = merge_selector_styles(base_selector);
        MergeStyles(target, base_style);
    }
    if(is_hovered)
    {
        Append(selector, "!hover");
        char* hovered_name = Flatten(selector);
        Selector* hovered_selector = GetGlobalSelector({hovered_name, (uint32_t)selector->length}, file_id);
        if(hovered_selector)
        {
            InFlightStyle* hovered_style = merge_selector_styles(hovered_selector);
            MergeStyles(target, hovered_style);
        }
        DeAllocScratch(hovered_name);
    }
    
    DeAllocScratch(base_name); 
    FreeString(selector);
}

void update_click_state(Element* target, PlatformControlState* controls)
{
    target->flags &= ~is_clicked();
    
    if((target->flags & is_hovered()) == 0 || controls->mouse_left_state == MouseState::UP)
    {
        target->click_state = ClickState::NONE;
        return;
    }
    else if(controls->mouse_left_state == MouseState::DOWN_THIS_FRAME)
    {
        target->click_state = ClickState::MOUSE_DOWN;
        return;
    }
    else if(controls->mouse_left_state == MouseState::DOWN && target->click_state == ClickState::MOUSE_DOWN)
    {
        target->click_state = ClickState::MOUSE_DOWN;
        return;
    }
    else if(controls->mouse_left_state == MouseState::DOWN) // Mouse is down but didnt start down on this element
    {
        target->click_state = ClickState::NONE;
        return;
    }
    else if(target->click_state == ClickState::NONE)
    {
        target->click_state = ClickState::NONE;
        return;
    }
    
    target->flags = target->flags | is_clicked();
    // This element must have had mouse down and now gotten mouse up all while being hovered
    assert(controls->mouse_left_state == MouseState::UP_THIS_FRAME && target->click_state == ClickState::MOUSE_DOWN && target->flags & is_hovered()); 
    
}

void merge_element_class_style(Element* element, Attribute* class_attribute)
{
    // Todo(Leo): Once classes get reworked to not allow bindings/multiple selectors this should all be removed

    assert(class_attribute->type == AttributeType::CLASS);
    
    ArenaString* class_string = CreateString(runtime.strings);
    
    if(class_attribute->Text.binding_position) // Indicates theres text to copy before the binding
    {
        Append(class_string, class_attribute->Text.static_value, class_attribute->Text.binding_position*sizeof(char));
    }
    
    if(class_attribute->Text.binding_id)
    {
        BoundExpression* binding = GetBoundExpression(class_attribute->Text.binding_id);
        assert(binding->type == BoundExpressionType::ARENA_STRING);
        assert(element->master);
        ArenaString* binding_text = NULL;
        
        if(binding->context == BindingContext::GLOBAL)
        {
            binding_text = binding->stub_string((void*)element->master, runtime.strings);
        }
        else
        {
            binding_text = binding->arr_stub_string((void*)element->context_master, runtime.strings, element->context_index);
        }
        
        // Note(Leo): binding_text gets freed as a part of class_string (dont need to call freestring)
        Append(class_string, binding_text, no_copy());
    }
    
    
    if(class_attribute->Text.value_length > class_attribute->Text.binding_position) // Indicates theres text after the binding
    {
        Append(class_string, class_attribute->Text.static_value + class_attribute->Text.binding_position, (class_attribute->Text.value_length - class_attribute->Text.binding_position)*sizeof(char));
    }
    
    if(!class_string->length)
    {
        return;
    }
    
    // Split selectors and mangle names to create global names and combine the styles from all the selectors
    char* flat_class_string = Flatten(class_string); 
    char* start_address = flat_class_string;
    char* end_address = start_address; 
    for(int i = 0; i < class_string->length; i++)
    {
        // Go until hitting a space or getting to the end of the string
        if(flat_class_string[i] != ' ' && i != class_string->length - 1)
        {
            end_address++;
            continue;
        }
        
        // Weve hit another space after just hitting one
        if(start_address == end_address)
        {
            start_address++;
            end_address++;
            continue;
        }
        
        // We are on the last iteration so include the remaining charachter
        if(i == class_string->length - 1)
        {
            end_address++;
        }
        
        // Weve succesfully found a selector
        int name_length = end_address - start_address;
        //printf("Found selector: %.*s\n", unmangled_name_length, start_address);
        
        Selector* found_selector = GetGlobalSelector({start_address, (uint32_t)name_length}, ((ElementMaster*)element->master)->file_id);
    
        if(found_selector)
        {
            InFlightStyle* selector_style = merge_selector_styles(found_selector);
            MergeStyles(&element->working_style, selector_style);
        }
        
        if(element->flags & is_hovered())
        {
            // Check for a hovered version of the selector
            ArenaString* hovered_selector = CreateString(runtime.strings);
            Append(hovered_selector, start_address, name_length);
            Append(hovered_selector, "!hover");
            char* flat_selector_string = Flatten(hovered_selector);
            found_selector = GetGlobalSelector({flat_selector_string, (uint32_t)hovered_selector->length}, ((ElementMaster*)element->master)->file_id);
    
            if(found_selector)
            {
                InFlightStyle* selector_style = merge_selector_styles(found_selector);
                MergeStyles(&element->working_style, selector_style);
            }
            DeAllocScratch(flat_selector_string);
            FreeString(hovered_selector);
        }
        
        
        // Move over the ' '
        end_address++;
        start_address = end_address;
        
    }
    
    DeAllocScratch(flat_class_string);
    FreeString(class_string);
}

// Note(Leo): Called for every element every frame!!!!!!
void runtime_evaluate_attributes(DOM* dom, PlatformControlState* controls, Element* element)
{
    BEGIN_TIMED_BLOCK(EVALUATE_ATTRIBUTES);
    
    DefaultStyle(&element->working_style);
    if(element->last_sizing)
    {
        // Element is hovered
        if(controls->cursor_pos.x != 0.0f && controls->cursor_pos.y != 0.0f &&
            PointInsideBounds(element->last_sizing->bounds, controls->cursor_pos))
        {
            element->flags = element->flags | is_hovered(); 
        }
        else // Not hovered
        {
            element->flags &= ~is_hovered(); 
        }
    }
    
    update_click_state(element, controls);
    
    merge_element_type_style(element->type, element->flags & is_hovered(), ((ElementMaster*)element->master)->file_id, &element->working_style);

    if(element->do_override_style)
    {
        MergeStyles(&element->working_style, &element->override_style);
    }

    Attribute* curr_attribute = element->first_attribute;
    while(curr_attribute)
    {
        switch(curr_attribute->type)
        {
            case(AttributeType::TEXT):
            {
                assert(element->type == ElementType::TEXT);
                
                // Text font and size comes from parent
                element->working_style.font_id = element->parent->working_style.font_id;
                element->working_style.font_size = element->parent->working_style.font_size;
                element->working_style.text_color = element->parent->working_style.text_color;
                
                if(!curr_attribute->Text.binding_id) // Text is known
                {
                    element->Text.temporal_text = (char*)Alloc(dom->frame_arena, sizeof(char)*curr_attribute->Text.value_length);
                    memcpy(element->Text.temporal_text, curr_attribute->Text.static_value, sizeof(char)*curr_attribute->Text.value_length);
                    element->Text.temporal_text_length = curr_attribute->Text.value_length;
                    break;
                }
                // Text has to come from a binding
                BoundExpression* binding = GetBoundExpression(curr_attribute->Text.binding_id);
                assert(binding->type == BoundExpressionType::ARENA_STRING);
                assert(element->master);
                
                ArenaString* binding_text = NULL;
                if(binding->context == BindingContext::GLOBAL)
                {
                    binding_text = binding->stub_string((void*)element->master, runtime.strings);
                }
                else
                {
                    binding_text = binding->arr_stub_string((void*)element->context_master, runtime.strings, element->context_index);
                }
                
                element->Text.temporal_text_length = binding_text->length;
                
                // Note(Leo): A \0 is automatically added by flatten so account for it
                element->Text.temporal_text = (char*)Alloc(dom->frame_arena, sizeof(char)*(binding_text->length + 1));
                Flatten(binding_text, element->Text.temporal_text, binding_text->length + 1);
            
                FreeString(binding_text);
                
                break;
            }
            case(AttributeType::STYLE):
            {
                
                break;
            }
            case(AttributeType::SRC):
            {
                // SRC is only for images and video elements
                assert(element->type == ElementType::IMG || element->type == ElementType::VIDEO);
                
                if(element->type == ElementType::IMG)
                {
                    // Handle has not been cached yet, get it.
                    if(!element->Image.handle)
                    {
                        char* terminated_name = (char*)AllocScratch((curr_attribute->Text.value_length + 1)*sizeof(char)); // +1 for \0
                        memcpy(terminated_name, curr_attribute->Text.static_value, curr_attribute->Text.value_length*sizeof(char));
                        terminated_name[curr_attribute->Text.value_length] = '\0';

                        element->Image.handle = RenderplatformGetImage(terminated_name);
                        DeAllocScratch(terminated_name);
                    }
                }
                break;
            }
            case(AttributeType::LOOP):
            {
                assert(element->type == ElementType::EACH);
                
                BoundExpression* binding = GetBoundExpression(curr_attribute->Loop.length_binding);
                assert(binding->type == BoundExpressionType::INT_RET);
                
                int count = 0;
                if(binding->context == BindingContext::GLOBAL)
                {
                    count = binding->stub_int((void*)element->master);
                }
                else
                {
                    count = binding->arr_stub_int((void*)element->context_master, (void*)element->master, element->context_index);
                }
                
                if(element->Each.last_count == count)
                {
                    break;
                }
                
                element->Each.last_count = count;
                binding = GetBoundExpression(curr_attribute->Loop.array_binding);
                assert(binding->type == BoundExpressionType::PTR_RET);
                
                element->Each.array_ptr = NULL;
                
                if(binding->context == BindingContext::GLOBAL) 
                {
                    element->Each.array_ptr = binding->stub_get_ptr((void*)element->master);
                }
                else
                {
                    element->Each.array_ptr = binding->arr_stub_get_ptr((void*)element->context_master, element->context_index);
                }
                
                if(!element->Each.array_ptr || count == 0)
                {
                    break;
                }
                
                // Note(Leo): adding template elements backwards since they are appended as the first child of 
                //            the EACH, meaning the last one to get added is the first child
                for(int i = (count - 1); i >= 0; i--)
                {
                    InstanceTemplate(dom, element, element->Each.array_ptr, curr_attribute->Loop.template_id, i);
                }
                
                break;
            }
            case(AttributeType::ON_CLICK):
            {
                if((element->flags & is_clicked()) == 0)
                {
                    break;
                }
                assert(curr_attribute->OnClick.binding_id);
                
                BoundExpression* binding = GetBoundExpression(curr_attribute->OnClick.binding_id);
                assert(binding->type == BoundExpressionType::VOID_RET);
                assert(element->master);

                if(binding->context == BindingContext::GLOBAL)
                {
                    binding->stub_void((void*)element->master);
                }
                else
                {
                    binding->arr_stub_void((void*)element->context_master, (void*)element->master, element->context_index);
                }
                
                element->click_state = ClickState::NONE;
                break;
            }
            case(AttributeType::THIS_ELEMENT):
            {
                if(curr_attribute->This.is_initialized)
                {
                    break;
                }
                BoundExpression* binding = GetBoundExpression(curr_attribute->This.binding_id);
                
                if(binding->context == BindingContext::GLOBAL)
                {
                    binding->stub_ptr((void*)element->master, (void*)element);
                }
                else
                {
                    binding->arr_stub_ptr((void*)element->context_master, element->context_index, (void*)element);
                }
                curr_attribute->This.is_initialized = true;
            
                break;
            }
            case(AttributeType::CONDITION):
            {
                BoundExpression* binding = GetBoundExpression(curr_attribute->Condition.binding_id);
                assert(binding->type == BoundExpressionType::BOOL_RET);
                assert(element->master);
                
                bool is_hidden = false;
                if(binding->context == BindingContext::GLOBAL)
                {
                    is_hidden = binding->stub_bool((void*)element->master);
                }
                else
                {
                    is_hidden = binding->arr_stub_bool((void*)element->context_master, (void*)element->master, element->context_index);
                }
                
                // Hidden
                if(!is_hidden)
                {
                    element->flags = element->flags | is_hidden();
                }
                else // Not hidden
                {
                    element->flags &= ~is_hidden();
                }
                
                break;
            }
            case(AttributeType::ON_FOCUS):
            case(AttributeType::FOCUSABLE):
            {
                if((element->flags & is_clicked()) == 0)
                {
                    break;
                }
                
                dom->focused_element = element;
                if(curr_attribute->type == AttributeType::FOCUSABLE)
                {
                    element->flags |= is_focusable();
                }
                
                break;
            }
            case(AttributeType::TICKING):
            {
                Event* tick_event = PushEvent(dom);
                tick_event->type = EventType::TICK;
                tick_event->Tick.target = element;
                
                break;
            }
            case(AttributeType::CLASS):
            {
                merge_element_class_style(element, curr_attribute);
                
                break;
            }
            default:
            {
                break;
            }
        
        }
        
        curr_attribute = curr_attribute->next_attribute; 
    }
    
    END_TIMED_BLOCK(EVALUATE_ATTRIBUTES);
}

void PlatformEvaluateAttributes(DOM* dom, Element* target)
{
    if(!dom || !target || !dom->controls)
    {
        return;
    }
    
    runtime_evaluate_attributes(dom, dom->controls, target);
}

void RuntimeClearTemporal(DOM* target)
{
    ResetArena(target->frame_arena);
}

//  Returns whether the element would like to capture any scrolling from the curent frame.
//  The deepest element in the tree that is hovered by the mouse and has clipped content will be the
//  one to capture the scroll
bool should_capture_scroll(PlatformControlState* controls, Element* target)
{
    if(!target->last_sizing)
    {
        return false;
    }
    assert(target && controls);
    
    if(PointInsideBounds(target->last_sizing->bounds, controls->cursor_pos) && 
        target->working_style.vertical_clipping == ClipStyle::SCROLL &&
        target->last_sizing->bounds.height < target->last_sizing->sizing.height.current
    )
    {
        return true;
    }
    
    return false;   
} 

// Scrolls in the inputted direction 
void scroll_element(vec2 scroll_dir, Element* target)
{
    if(!target->last_sizing)
    {
        return;
    }
    
    float scrollable_top = (target->last_sizing->bounds.y - target->last_sizing->position.y);
    scrollable_top += target->scroll.y;
    float scrollable_bottom = (target->last_sizing->position.y + target->last_sizing->sizing.height.current) - (target->last_sizing->bounds.y + target->last_sizing->bounds.height);
    scrollable_bottom -= target->scroll.y;
     
    float scrollable_left = target->last_sizing->bounds.x - target->last_sizing->position.x;
    scrollable_left += target->scroll.x;
    float scrollable_right = (target->last_sizing->position.x + target->last_sizing->sizing.width.current) - (target->last_sizing->bounds.x + target->last_sizing->bounds.width);
    scrollable_right -= target->scroll.x;
    
    
    if(scroll_dir.y > 0.0f && scrollable_top > 0.0f)
    {
        target->scroll.y -= MIN(scroll_dir.y, scrollable_top);
    }
    else if(scroll_dir.y < 0.0f && scrollable_bottom > 0.0f)
    {
        target->scroll.y += MIN((scroll_dir.y * -1.0f), scrollable_bottom);
    }
    
    if(scroll_dir.x > 0.0f && scrollable_right > 0.0f)
    {
        target->scroll.x += MIN(scroll_dir.x, scrollable_right);
    }
    else if(scroll_dir.x < 0.0f && scrollable_left > 0.0f)
    {
        target->scroll.x -= MIN((scroll_dir.x * -1.0f), scrollable_left);
    }
}

// Note(Leo): When a scrolled div shrinks down past a point it was scrolled too the scrollables will be negative, 
//            we autoscroll back to the legal region
void sanitize_scrollable(Element* target)
{
    if(!target->last_sizing)
    {
        return;
    }
    
    if(!(target->working_style.vertical_clipping == ClipStyle::SCROLL || target->working_style.horizontal_clipping == ClipStyle::SCROLL))
    {
        return;
    }
    
    
    float scrollable_top = (target->last_sizing->bounds.y - target->last_sizing->position.y);
    scrollable_top += target->scroll.y;
    float scrollable_bottom = (target->last_sizing->position.y + MAX(target->last_sizing->sizing.height.current, target->last_sizing->sizing.height.desired.size)) - (target->last_sizing->bounds.y + target->last_sizing->bounds.height);
    scrollable_bottom -= target->scroll.y;
    
    float scrollable_left = target->last_sizing->bounds.x - target->last_sizing->position.x;
    scrollable_left += target->scroll.x;
    float scrollable_right = (target->last_sizing->position.x + MAX(target->last_sizing->sizing.width.current, target->last_sizing->sizing.width.desired.size)) - (target->last_sizing->bounds.x + target->last_sizing->bounds.width);
    scrollable_right -= target->scroll.x;
    
    if(scrollable_top < 0.0f)
    {
        target->scroll.y += -1.0f * scrollable_top;
    }
    else if(scrollable_bottom < 0.0f)
    {
        target->scroll.y -= -1.0f * scrollable_bottom;
    }
    
    if(scrollable_left < 0.0f)
    {
        target->scroll.x += -1.0f * scrollable_left;
    }
    else if(scrollable_right < 0.0f)
    {
        target->scroll.x -= -1.0f * scrollable_right;
    }
}

void FocusElement(DOM* dom, Element* old_focused, Element* new_focused)
{
    
    if(old_focused)
    {
        Attribute* focussed_binding = GetAttribute(old_focused, AttributeType::ON_FOCUS);
        if(focussed_binding)
        {
            assert(focussed_binding->OnFocus.binding_id);
            
            BoundExpression* binding = GetBoundExpression(focussed_binding->OnClick.binding_id);
            assert(binding->type == BoundExpressionType::VOID_BOOL_RET);
            assert(old_focused->master);
            
            if(binding->context == BindingContext::GLOBAL)
            {
                binding->stub_void_bool((void*)old_focused->master, false);
            }
            else
            {
                binding->arr_stub_void_bool((void*)old_focused->context_master, (void*)old_focused->master, old_focused->context_index, false);
            }
        }
        if(old_focused->flags & is_focusable()) // Element has the focusable property
        {
            Event* de_focus_event = PushEvent(dom);
            de_focus_event->type = EventType::DE_FOCUSED;
            de_focus_event->DeFocused.target = old_focused;
        }
    }
    
    if(new_focused)
    {
        Attribute* focussed_binding = GetAttribute(new_focused, AttributeType::ON_FOCUS);
        if(focussed_binding) // Element has an OnFocus binding
        {
            assert(focussed_binding->OnFocus.binding_id);
            
            BoundExpression* binding = GetBoundExpression(focussed_binding->OnClick.binding_id);
            assert(binding->type == BoundExpressionType::VOID_BOOL_RET);
            assert(new_focused->master);
            
            if(binding->context == BindingContext::GLOBAL)
            {
                binding->stub_void_bool((void*)new_focused->master, true);
            }
            else
            {
                binding->arr_stub_void_bool((void*)new_focused->context_master, (void*)new_focused->master, new_focused->context_index, true);
            }
        }
        if(new_focused->flags & is_focusable()) // Element has the focusable property
        {
            Event* focus_event = PushEvent(dom);
            focus_event->type = EventType::FOCUSED;
            focus_event->Focused.target = new_focused;    
        }
    }
    
    dom->focused_element = new_focused;
}

Arena* RuntimeTickAndBuildRenderque(Arena* renderque, DOM* dom, PlatformControlState* controls, int window_width, int window_height)
{
    dom->controls = controls;
    
    // Note(Leo): Page root element is always at the first address of the dom
    Element* old_focused = dom->focused_element;
    
    Element* root_element = (Element*)dom->elements->mapped_address;
    assert(!root_element->parent && !root_element->next_sibling); // Root cant have a parent or siblings
    
    // If a page switch was requested last frame do it now
    if(dom->switch_request.file_id)
    {
        FreeSubtreeObjects(root_element);
        SwitchPage(dom, dom->switch_request.file_id, dom->switch_request.flags);
        dom->switch_request = {};
    }
      
    Element* scroll_capturer = NULL; // The current deepest element asking to capture scroll
    
    Element* curr_element = root_element;
    
    runtime_evaluate_attributes(dom, controls, curr_element);
    sanitize_scrollable(curr_element);
    if(should_capture_scroll(controls, curr_element)) // Root is allowed to capture scroll
    {
        scroll_capturer = curr_element;
    }
    
    // Depth first walk
    while(curr_element)
    {
    
        if(curr_element->first_child && curr_element->flags ^ is_hidden())
        {
            curr_element = curr_element->first_child;
            runtime_evaluate_attributes(dom, controls, curr_element);
            
            sanitize_scrollable(curr_element);
            if(should_capture_scroll(controls, curr_element))
            {
                scroll_capturer = curr_element;
            }
            
            continue;
        }
        if(curr_element->next_sibling)
        {
            curr_element = curr_element->next_sibling;
            runtime_evaluate_attributes(dom, controls, curr_element);
            
            sanitize_scrollable(curr_element);
            if(should_capture_scroll(controls, curr_element))
            {
                scroll_capturer = curr_element;
            }
            
            continue;
        }
        
        // We need to traverse back up until we find a next sibling or get to the top
        curr_element = curr_element->parent;
        while(curr_element)
        {
            if(curr_element->next_sibling)
            {
                curr_element = curr_element->next_sibling;
                runtime_evaluate_attributes(dom, controls, curr_element);
                
                sanitize_scrollable(curr_element);
                if(should_capture_scroll(controls, curr_element))
                {
                    scroll_capturer = curr_element;
                }
                
                break;
            }
            curr_element = curr_element->parent;
        }
    }
    
    if(scroll_capturer && controls->cursor_source == CursorSource::MOUSE)
    {
        scroll_element(controls->scroll_dir, scroll_capturer);
    }
    // Note(Leo): Touch scrolls when 1 pointer is down and is moving.
    // Todo(Leo): This should change when we add dragging since drag and scroll are mutally exclusive.
    else if(scroll_capturer && controls->cursor_source == CursorSource::TOUCH && controls->cursor_count == 1
            && (controls->mouse_left_state == MouseState::DOWN || controls->mouse_left_state == MouseState::DOWN_THIS_FRAME))
    {
        // Todo(Leo): Should this have some type of deadzone?
        
        vec2 scroll_dir = controls->cursor_delta;
    
        scroll_element(scroll_dir, scroll_capturer);
    }
    
    if(old_focused != dom->focused_element)
    {
        FocusElement(dom, old_focused, dom->focused_element);
    }


    call_page_frame(dom, ((ElementMaster*)root_element->master)->file_id, root_element->master);    
    
    // Note(Leo): This is an approximation of the element count since there could be empty space inside the arena but we
    //            will never underestimate so its ok.
    int element_count = (dom->elements->next_address - dom->elements->mapped_address)/sizeof(Element);

    BEGIN_TIMED_BLOCK(PLATFORM_SHAPE);    
    Arena* result = ShapingPlatformShape(root_element, renderque, element_count, window_width, window_height);
    END_TIMED_BLOCK(PLATFORM_SHAPE);
    
    return result;
}

// Returns the number of bytes that were consumed
uint32_t PlatformConsumeUTF8ToUTF32(const char* utf8_buffer, uint32_t* codepoint, uint32_t buffer_length)
{
    if(buffer_length < 1)
    {
        return 0;
    }
    
    if((utf8_buffer[0] & 0b10000000) == 0) 
    {
        *codepoint = (utf8_buffer[0] & 0b01111111);
        return 1;
    }
    else if((utf8_buffer[0] & 0b11100000) == 0b11000000)
    {
        if(buffer_length < 2)
        {
            return 0;
        }
        
        *codepoint = (utf8_buffer[0] & 0b00011111) << 6 | (utf8_buffer[1] & 0b00111111);
        return 2;
    }
    else if((utf8_buffer[0] & 0b11110000) == 0b11100000)
    {
        if(buffer_length < 3)
        {
            return 0;
        }
        
        *codepoint = (utf8_buffer[0] & 0b00001111) << 12 | (utf8_buffer[1] & 0b00111111) << 6 | (utf8_buffer[2] & 0b00111111);
        return 3;
    }
    else
    {
        if(buffer_length < 4)
        {
            return 0;
        }
        
        *codepoint = (utf8_buffer[0] & 0b00000111) << 18 | (utf8_buffer[1] & 0b00111111) << 12 | (utf8_buffer[2] & 0b00111111) << 6 | (utf8_buffer[3] & 0b00111111);
        return 4;
    }
    
    return 0;
}

// Returns the number of bytes in the utf8 glob
uint32_t PlatformUTF32ToUTF8(uint32_t codepoint, char* utf8_buffer)
{
    if(codepoint < 0x80) 
    {
        utf8_buffer[0] = (char)codepoint;
        return 1;
    }
    else if(codepoint < 0x800)
    {
        utf8_buffer[0] = (0b11000000 | (codepoint >> 6));
        utf8_buffer[1] = (0b10000000 | (codepoint & 0x3F));
        return 2;
    }
    else if(codepoint < 0x10000)
    {
        utf8_buffer[0] = (0b11100000 | (codepoint >> 12));         
		utf8_buffer[1] = (0b10000000 | ((codepoint >> 6) & 0x3f)); 
		utf8_buffer[2] = (0b10000000 | (codepoint & 0x3f));        
        
        return 3;
    }
    else if(codepoint < 0x200000)
    {
        utf8_buffer[0] = (0b11110000 | (codepoint >> 18));          
		utf8_buffer[1] = (0b10000000 | ((codepoint >> 12) & 0x3f)); 
		utf8_buffer[2] = (0b10000000 | ((codepoint >> 6)  & 0x3f)); 
		utf8_buffer[3] = (0b10000000 | (codepoint & 0x3f));         
		
        return 4;
    }
    
    // Invalid codepoint
    return 0;
    
}

// Returns the number of bytes that were consumed.
uint32_t PlatformConsumeUTF16ToUTF32(const uint16_t* utf16_buffer, uint32_t* codepoint, uint32_t buffer_length)
{
    // Need 2 bytes to have a utf16 char
    if(buffer_length < 2)
    {
        return 0;
    }
    
    uint16_t high_surrogate = utf16_buffer[0];
    uint16_t low_surrogate = utf16_buffer[1];
    
    // Direct encoded codepoints
    if(high_surrogate < 0xD7FF || (high_surrogate > 0xE000 && high_surrogate < 0xFFFF))
    {
        *codepoint = high_surrogate;
    
        return 2;
    }
    // Double word encoded codepoints
    if(high_surrogate > 0xD800 && high_surrogate < 0xDBFF)
    {
        if(buffer_length < 4)
        {
            return 0;
        }
        *codepoint = ((0b0000001111111111 & high_surrogate) << 10) | (0b0000001111111111 & low_surrogate);
        
        return 4;
    }
    
    return 0;
}

PlatformControlState* PlatformGetControlState(DOM* dom)
{
    return dom->controls;
}

FontPlatformShapedGlyph* PlatformGetGlyphAt(Element* text, vec2 pos)
{
    assert(text);
    if(!text || text->type != ElementType::TEXT || !text->last_sizing)
    {
        return NULL;
    }
    
    // Note(Leo): If an element is hidden then reapears its sizing pointer probably points at someone elese layout element
    //            so ensure we dont accidentaly use that.
    if(text->id != text->last_sizing->element_id)
    {
        return NULL;
    }
    
    // Todo(Leo): Make this work with text that ISNT just the first text element of a parent
    assert(text->last_sizing->type == LayoutElementType::TEXT_COMBINED);
    
    FontPlatformShapedGlyph* curr = text->last_sizing->TEXT_COMBINED.first_glyph;
    uint32_t count = text->last_sizing->TEXT_COMBINED.glyph_count;
    
    float base_x = text->last_sizing->position.x;
    float base_y = text->last_sizing->position.y;
    
    for(uint32_t i = 0; i < count; i++)
    {
        bounding_box curr_bounds = { base_x + curr->placement_offsets.x, base_y + curr->placement_offsets.y,
                                     curr->placement_size.x, curr->placement_size.y };
        
        if(PointInsideBounds(curr_bounds, pos))
        {
            return curr;
        }
        
        curr++;
    }
    
    return NULL;
    
}

FontPlatformShapedGlyph* PlatformGetGlyphForBufferIndex(Element* text, uint32_t index)
{
    assert(text);
    if(!text || text->type != ElementType::TEXT || !text->last_sizing)
    {
        return NULL;
    }
    
    // Note(Leo): If an element is hidden then reapears its sizing pointer probably points at someone elese layout element
    //            so ensure we dont accidentaly use that.
    if(text->id != text->last_sizing->element_id)
    {
        return NULL;
    }
    
    // Todo(Leo): Make this work with text that ISNT just the first text element of a parent
    assert(text->last_sizing->type == LayoutElementType::TEXT_COMBINED);
    
    FontPlatformShapedGlyph* curr = text->last_sizing->TEXT_COMBINED.first_glyph;
    uint32_t count = text->last_sizing->TEXT_COMBINED.glyph_count;
    
    for(uint32_t i = 0; i < count; i++)
    {
        if(index >= curr->buffer_index && index < (curr->buffer_index + curr->run_length))
        {
            return curr;
        }
    
        curr++;
    }
    
    return NULL;
}

FontPlatformShapedGlyph* PlatformGetGlyphAt(Element* text, uint32_t index)
{
    assert(text);
    if(!text || text->type != ElementType::TEXT || !text->last_sizing)
    {
        return NULL;
    }
    
    // Note(Leo): If an element is hidden then reapears its sizing pointer probably points at someone elese layout element
    //            so ensure we dont accidentaly use that.
    if(text->id != text->last_sizing->element_id)
    {
        return NULL;
    }
    
    // Todo(Leo): Make this work with text that ISNT just the first text element of a parent
    assert(text->last_sizing->type == LayoutElementType::TEXT_COMBINED);
    
    FontPlatformShapedGlyph* first = text->last_sizing->TEXT_COMBINED.first_glyph;
    uint32_t count = text->last_sizing->TEXT_COMBINED.glyph_count;
    
    if(index >= count)
    {
        return NULL;
    }
    
    return first + index;
    
}

uint32_t PlatformGetGlyphIndex(Element* text, FontPlatformShapedGlyph* glyph)
{
    assert(text);
    if(!text || text->type != ElementType::TEXT || !text->last_sizing || !glyph)
    {
        return 0;
    }
    
    // Note(Leo): If an element is hidden then reapears its sizing pointer probably points at someone elese layout element
    //            so ensure we dont accidentaly use that.
    if(text->id != text->last_sizing->element_id)
    {
        return 0;
    }
    
    // Todo(Leo): Make this work with text that ISNT just the first text element of a parent
    assert(text->last_sizing->type == LayoutElementType::TEXT_COMBINED);
    
    FontPlatformShapedGlyph* first = text->last_sizing->TEXT_COMBINED.first_glyph;
    uint32_t count = text->last_sizing->TEXT_COMBINED.glyph_count;
    
    // Todo(Leo): How safe are these pointer comparisons?
    if((first + count) < glyph || glyph < first)
    {
        return 0;
    }
    
    return glyph - first;
}

uint32_t PlatformGetGlyphCount(Element* text)
{
    assert(text);
    if(!text || text->type != ElementType::TEXT || !text->last_sizing)
    {
        return 0;
    }
    
    // Note(Leo): If an element is hidden then reapears its sizing pointer probably points at someone elese layout element
    //            so ensure we dont accidentaly use that.
    if(text->id != text->last_sizing->element_id)
    {
        return 0;
    }
    
    // Todo(Leo): Make this work with text that ISNT just the first text element of a parent
    assert(text->last_sizing->type == LayoutElementType::TEXT_COMBINED);
    
    uint32_t count = text->last_sizing->TEXT_COMBINED.glyph_count;
    
    return count;
}

void PlatformUpdateStyle(Element* target)
{
    if(!target)
    {
        return;
    }

    // Note(Leo): Most of this is copy pasta from runtime_evaluate_attributes
    DefaultStyle(&target->working_style);
    merge_element_type_style(target->type, target->flags & is_hovered(), ((ElementMaster*)target->master)->file_id, &target->working_style);
    
    if(target->do_override_style)
    {
        MergeStyles(&target->working_style, &target->override_style);
    }
    
    if(target->type == ElementType::TEXT)
    {
        // Text font and size comes from parent
        target->working_style.font_id = target->parent->working_style.font_id;
        target->working_style.font_size = target->parent->working_style.font_size;
        target->working_style.text_color = target->parent->working_style.text_color;
        return;
    }
    
    Attribute* class_attribute = GetAttribute(target, AttributeType::CLASS);
    
    if(class_attribute)
    {
        merge_element_class_style(target, class_attribute);
    }
}