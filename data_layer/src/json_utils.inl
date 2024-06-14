#pragma once

#include "rapidjson/document.h"

static bool value_get_opt_bool(const rapidjson::Value& v, const char* key, bool def_value = false) {
    bool res = def_value;
    if (v.HasMember(key)) {
        res = v[key].GetBool();
    }
    return res;
}

static float value_get_opt_float(const rapidjson::Value& v, const char* key, float def_value = 0.0f) {
    float res = def_value;
    if (v.HasMember(key)) {
        res = v[key].GetFloat();
    }
    return res;
}

static unsigned value_get_opt_uint(const rapidjson::Value& v, const char* key, unsigned def_value = 0) {
    unsigned res = def_value;
    if (v.HasMember(key)) {
        res = v[key].GetUint();
    }
    return res;
}
