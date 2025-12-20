#pragma once

#include <string>
#include <switch.h>
#include <language.hpp>

typedef struct {
    Locale lang = Locale::English;
    bool dev_options = false;
    bool image_filename = false;
    bool multi_lang = true;
    bool full_charset = false;
} config_t;

extern config_t cfg;
extern std::string cwd;
extern std::string device;

namespace Config {
    int Save(config_t &config);
    int Load(void);
}
