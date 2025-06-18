#include "DOM.h"
#define EID(name, file) name ##_ ##file ##_GLOBAL_ID

void register_binding_subscriptions(Runtime* runtime);

void call_page_main(DOM* dom, int file_id, void** d_void_target);
void call_comp_main(DOM* dom, int file_id, void** d_void_target, CustomArgs* ARGS);
void call_page_frame(DOM* dom, int file_id, void* d_void);
void call_comp_event(DOM* dom, Event* event, int file_id, void* d_void);