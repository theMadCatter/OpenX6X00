#include "parameter_api.h"

#include <string>

#include "parameter.h"

int32_t param_i_get(const ParamInt *p) {
    return p->get();
}

void param_i_set(ParamInt *p, int32_t v) {
    p->set(v);
}

int64_t param_hz_get(const ParamHz *p) {
    return p->get();
}

void param_hz_set(ParamHz *p, int64_t v) {
    p->set(v);
}

float param_f_get(const ParamFloat *p) {
    return p->get();
}
