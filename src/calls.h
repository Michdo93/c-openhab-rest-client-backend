/* Calls any function of c-openhab-rest-client by Python class and method name. */
#ifndef CALLS_H
#define CALLS_H
#include <openhab/openhab.h>
#ifdef __cplusplus
extern "C" {
#endif
/* REST functions: result as cJSON (caller frees), NULL for an unknown name. */
cJSON* openhab_call(openhab_client_t* c, const char* cls, const char* m, const cJSON* args);
/* Event streams: openhab_sse_t* (caller closes), NULL for an unknown name. */
openhab_sse_t* openhab_open_stream(openhab_client_t* c, const char* cls, const char* m,
                                   const cJSON* args);
#ifdef __cplusplus
}
#endif
#endif
