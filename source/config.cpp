#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cctype>
#include <jansson.h>

#include "config.hpp"
#include "fs.hpp"
#include "log.hpp"

#define CONFIG_VERSION 6

config_t cfg;

namespace Config {
    static const char *config_path = "/switch/NX-Shell/config.json";
    static const char *config_file = "{\n\t\"config_version\": %d,\n\t\"language\": %d,\n\t\"dev_options\": %d,\n\t\"image_filename\": %d,\n\t\"multi_lang\": %d\n}";
    static int config_version_holder = 0;
    static const int buf_size = 128;
    static const std::pair<Locale, const char*> LocaleMap[] = {
        {Locale::Japanese, "Japanese"},
        {Locale::English, "English"},
        {Locale::French, "French"},
        {Locale::German, "German"},
        {Locale::Italian, "Italian"},
        {Locale::Spanish, "Spanish"},
        {Locale::SimplifiedChinese, "SimplifiedChinese"},
        {Locale::Korean, "Korean"},
        {Locale::Dutch, "Dutch"},
        {Locale::Portuguese, "Portuguese"},
        {Locale::Russian, "Russian"},
        {Locale::TraditionalChinese, "TraditionalChinese"}
    };
    static const size_t LocaleCount = sizeof(LocaleMap)/sizeof(LocaleMap[0]);

    std::string locale2str(Locale loc) {
        for (size_t i=0; i<LocaleCount; i++) {
            if (LocaleMap[i].first == loc)
                return LocaleMap[i].second;
        }
        return "English";
    }

    Locale str2locale(const std::string& str, bool ignore_case = true) {
        std::string s = str;
        if (ignore_case) {
            for (char& c : s)
                c = tolower(static_cast<unsigned char>(c));
        }
        for (size_t i=0; i<LocaleCount; i++) {
            std::string key = LocaleMap[i].second;
            if (ignore_case)
                for (char& c : key)
                    c = tolower(static_cast<unsigned char>(c));
            if (s == key)
                return LocaleMap[i].first;
        }
        return Locale::English;
    }

    int Save(config_t &config) {
        Result ret = 0;
        char *buf = new char[buf_size];
        u64 len = std::snprintf(buf, buf_size, config_file, CONFIG_VERSION, locale2str(config.lang), config.dev_options, config.image_filename, config.multi_lang);

        // Delete and re-create the file, we don't care about the return value here.
        fsFsDeleteFile(std::addressof(devices[FileSystemSDMC]), config_path);
        fsFsCreateFile(std::addressof(devices[FileSystemSDMC]), config_path, len, 0);

        FsFile file;
        if (R_FAILED(ret = fsFsOpenFile(std::addressof(devices[FileSystemSDMC]), config_path, FsOpenMode_Write, std::addressof(file)))) {
            Log::Error("Config::Save fsFsOpenFile(%s) failed: 0x%x\n", config_path, ret);
            delete[] buf;
            return ret;
        }

        if (R_FAILED(ret = fsFileWrite(std::addressof(file), 0, buf, len, FsWriteOption_Flush))) {
            Log::Error("Config::Save fsFileWrite(%s) failed: 0x%x\n", config_path, ret);
            delete[] buf;
            fsFileClose(std::addressof(file));
            return ret;
        }

        fsFileClose(std::addressof(file));
        delete[] buf;
        return 0;
    }

    int Load(void) {
        Result ret = 0;

        if (!FS::DirExists("/switch/"))
            fsFsCreateDirectory(std::addressof(devices[FileSystemSDMC]), "/switch");
        if (!FS::DirExists("/switch/NX-Shell/"))
            fsFsCreateDirectory(std::addressof(devices[FileSystemSDMC]), "/switch/NX-Shell");

        if (!FS::FileExists(config_path)) {
            cfg = {};
            return Config::Save(cfg);
        }

        FsFile file;
        if (R_FAILED(ret = fsFsOpenFile(std::addressof(devices[FileSystemSDMC]), config_path, FsOpenMode_Read, std::addressof(file))))
            return ret;

        s64 size = 0;
        if (R_FAILED(ret = fsFileGetSize(std::addressof(file), std::addressof(size)))) {
            fsFileClose(std::addressof(file));
            return ret;
        }

        char *buf =  new char[size + 1];
        if (R_FAILED(ret = fsFileRead(std::addressof(file), 0, buf, static_cast<u64>(size) + 1, FsReadOption_None, nullptr))) {
            delete[] buf;
            fsFileClose(std::addressof(file));
            return ret;
        }

        fsFileClose(std::addressof(file));

        json_t *root;
        json_error_t error;
        root = json_loads(buf, 0, std::addressof(error));
        delete[] buf;

        if (!root) {
            std::printf("error: on line %d: %s\n", error.line, error.text);
            return -1;
        }

        json_t *config_ver = json_object_get(root, "config_version");
        config_version_holder = json_integer_value(config_ver);

        // Delete config file if config file is updated. This will rarely happen.
        if (config_version_holder < CONFIG_VERSION) {
            fsFsDeleteFile(std::addressof(devices[FileSystemSDMC]), config_path);
            cfg = {};
            return Config::Save(cfg);
        }

        json_t *dev_options = json_object_get(root, "dev_options");
        cfg.dev_options = json_integer_value(dev_options);

        json_t *image_filename = json_object_get(root, "image_filename");
        cfg.image_filename = json_integer_value(image_filename);

        json_t *multi_lang = json_object_get(root, "multi_lang");
        cfg.multi_lang = json_integer_value(multi_lang);
        if (cfg.lang != Locale::English)
            cfg.multi_lang = 1;

        const auto type = appletGetAppletType();
        if (type != AppletType_Application && type != AppletType_SystemApplication)
            cfg.full_charset = 0;

        json_t *language = json_object_get(root, "language");
        if (language && json_is_string(language)) {
            cfg.lang = str2locale(json_string_value(language));
        } else {
            if(R_SUCCEEDED(setInitialize())) {
                u64 languageCode;
                if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
                    SetLanguage setLanguage{SetLanguage_ENUS};
                    if (R_SUCCEEDED(setMakeLanguage(languageCode, &setLanguage))) {
                        switch (setLanguage) {
                        case SetLanguage_JA:
                            cfg.lang = Locale::Japanese;
                            break;
                        case SetLanguage_FR:
                        case SetLanguage_FRCA:
                            cfg.lang = Locale::French;
                            break;
                        case SetLanguage_DE:
                            cfg.lang = Locale::German;
                            break;
                        case SetLanguage_IT:
                            cfg.lang = Locale::Italian;
                            break;
                        case SetLanguage_ES:
                        case SetLanguage_ES419:
                            cfg.lang = Locale::Spanish;
                            break;
                        case SetLanguage_ZHCN:
                        case SetLanguage_ZHHANS:
                            cfg.lang = Locale::SimplifiedChinese;
                            break;
                        case SetLanguage_KO:
                            cfg.lang = Locale::Korean;
                            break;
                        case SetLanguage_NL:
                            cfg.lang = Locale::Dutch;
                            break;
                        case SetLanguage_PT:
                        case SetLanguage_PTBR:
                            cfg.lang = Locale::Portuguese;
                            break;
                        case SetLanguage_RU:
                            cfg.lang = Locale::Russian;
                            break;
                        case SetLanguage_ZHTW:
                        case SetLanguage_ZHHANT:
                            cfg.lang = Locale::TraditionalChinese;
                            break;
                        default:
                            cfg.lang = Locale::English;   
                            break;
                        }
                    }
                }
                setExit();
            }
            Config::Save(cfg);
        }

        json_decref(root);
        return 0;
    }
}
