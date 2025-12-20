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
    // Remove the manual JSON concatenation constants (no longer used)
    // static const char *config_file = "..."; 
    // static const int buf_size = 128;
    static int config_version_holder = 0;
    static const std::pair<Locale, const char*> LocaleMap[] = {
        {Locale::Japanese, "ja"},
        {Locale::English, "en"},
        {Locale::French, "fr"},
        {Locale::German, "de"},
        {Locale::Italian, "it"},
        {Locale::Spanish, "es"},
        {Locale::SimplifiedChinese, "zh-Hans"},
        {Locale::Korean, "ko"},
        {Locale::Dutch, "nl"},
        {Locale::Portuguese, "pt"},
        {Locale::Russian, "ru"},
        {Locale::TraditionalChinese, "zh-Hant"}
    };
    static const size_t LocaleCount = sizeof(LocaleMap)/sizeof(LocaleMap[0]);

    /**
     * Convert Locale enum to corresponding string
     * @param loc Locale enum value
     * @return Corresponding locale string, default to "English" if not found
     */
    std::string locale2str(Locale loc) {
        for (size_t i=0; i<LocaleCount; i++) {
            if (LocaleMap[i].first == loc)
                return LocaleMap[i].second;
        }
        return "en";
    }

    /**
     * Convert string to corresponding Locale enum
     * @param str Input string to convert
     * @param ignore_case Whether to ignore case during comparison (default: true)
     * @return Corresponding Locale enum, default to Locale::English if not found
     */
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

    /**
     * Save config data to file using jansson for JSON serialization
     * @param config Config data to save
     * @return 0 on success, error code on failure
     */
    int Save(config_t &config) {
        Result ret = 0;

        // Core modification: Build JSON object with jansson
        json_t *root = json_object();
        if (!root) {
            Log::Error("Config::Save failed to create json root object\n");
            return -1;
        }

        // Set config version (integer)
        json_object_set_new(root, "config_version", json_integer(CONFIG_VERSION));
        // Set language (string, jansson handles UTF-8 encoding automatically)
        std::string lang_str = locale2str(config.lang);
        json_object_set_new(root, "language", json_string(lang_str.c_str()));
        // Set boolean values (store as integers for compatibility with existing parsing logic)
        json_object_set_new(root, "dev_options", json_integer(config.dev_options ? 1 : 0));
        json_object_set_new(root, "image_filename", json_integer(config.image_filename ? 1 : 0));
        json_object_set_new(root, "multi_lang", json_integer(config.multi_lang ? 1 : 0));

        // Serialize to JSON string (JSON_INDENT(4) for pretty printing with 4 spaces indent)
        char *json_str = json_dumps(root, JSON_INDENT(4));
        json_decref(root); // Free JSON object (jansson reference counting)
        if (!json_str) {
            Log::Error("Config::Save failed to dump json string\n");
            return -1;
        }

        // Get actual length of serialized string (dynamic, avoid buffer overflow)
        u64 len = strlen(json_str);

        // Original file operation logic (retained, fixed length issue)
        // Delete old file
        fsFsDeleteFile(std::addressof(devices[FileSystemSDMC]), config_path);
        // Create new file (use actual JSON string length)
        ret = fsFsCreateFile(std::addressof(devices[FileSystemSDMC]), config_path, len, 0);
        if (R_FAILED(ret)) {
            Log::Error("Config::Save fsFsCreateFile(%s) failed: 0x%x\n", config_path, ret);
            free(json_str); // String returned by json_dumps is freed with free()
            return ret;
        }

        FsFile file;
        ret = fsFsOpenFile(std::addressof(devices[FileSystemSDMC]), config_path, FsOpenMode_Write, std::addressof(file));
        if (R_FAILED(ret)) {
            Log::Error("Config::Save fsFsOpenFile(%s) failed: 0x%x\n", config_path, ret);
            free(json_str);
            return ret;
        }

        // Write JSON string (complete without truncation)
        ret = fsFileWrite(std::addressof(file), 0, json_str, len, FsWriteOption_Flush);
        if (R_FAILED(ret)) {
            Log::Error("Config::Save fsFileWrite(%s) failed: 0x%x\n", config_path, ret);
            free(json_str);
            fsFileClose(std::addressof(file));
            return ret;
        }

        // Resource release
        fsFileClose(std::addressof(file));
        free(json_str); // Free string serialized by jansson

        return 0;
    }

    /**
     * Load config data from file and parse with jansson
     * @return 0 on success, error code on failure
     */
    int Load(void) {
        Result ret = 0;

        // Create directories if they don't exist
        if (!FS::DirExists("/switch/"))
            fsFsCreateDirectory(std::addressof(devices[FileSystemSDMC]), "/switch");
        if (!FS::DirExists("/switch/NX-Shell/"))
            fsFsCreateDirectory(std::addressof(devices[FileSystemSDMC]), "/switch/NX-Shell");

        // If config file doesn't exist, initialize default config and save
        if (!FS::FileExists(config_path)) {
            cfg = {};
            return Config::Save(cfg);
        }

        // Open config file for reading
        FsFile file;
        if (R_FAILED(ret = fsFsOpenFile(std::addressof(devices[FileSystemSDMC]), config_path, FsOpenMode_Read, std::addressof(file))))
            return ret;

        // Get file size
        s64 size = 0;
        if (R_FAILED(ret = fsFileGetSize(std::addressof(file), std::addressof(size)))) {
            fsFileClose(std::addressof(file));
            return ret;
        }

        // Safety check: File size must be positive and not exceed 1MB (prevent malicious files)
        if (size <= 0 || size > 1024 * 1024) {
            fsFileClose(std::addressof(file));
            Log::Error("Config::Load invalid file size: %lld\n", size);
            return -1;
        }

        // Allocate buffer (dynamic size matching actual file size)
        char *buf = new char[static_cast<size_t>(size) + 1];
        if (!buf) {
            fsFileClose(std::addressof(file));
            Log::Error("Config::Load failed to allocate buffer\n");
            return -1;
        }

        // Fix: Read with correct length and get actual bytes read
        u64 bytes_read = 0;
        ret = fsFileRead(std::addressof(file), 0, buf, static_cast<u64>(size), FsReadOption_None, &bytes_read);
        if (R_FAILED(ret) || bytes_read != static_cast<u64>(size)) {
            delete[] buf;
            fsFileClose(std::addressof(file));
            Log::Error("Config::Load fsFileRead failed or incomplete read\n");
            return ret;
        }

        // Add null terminator to avoid out-of-bounds during parsing
        buf[static_cast<size_t>(size)] = '\0';

        // Close file after reading
        fsFileClose(std::addressof(file));

        // Parse JSON string with jansson (add JSON_REJECT_DUPLICATES for security)
        json_t *root;
        json_error_t error;
        root = json_loads(buf, JSON_REJECT_DUPLICATES, std::addressof(error));
        delete[] buf; // Free buffer after parsing

        if (!root) {
            Log::Error("Config::Load json parse error: line %d: %s\n", error.line, error.text);
            return -1;
        }

        // Fix: Add null and type checks to avoid null pointer exceptions
        json_t *config_ver = json_object_get(root, "config_version");
        if (!json_is_integer(config_ver)) {
            Log::Error("Config::Load config_version is not an integer\n");
            json_decref(root);
            return -1;
        }
        config_version_holder = json_integer_value(config_ver);

        // Delete config file if version is outdated (rare case)
        if (config_version_holder < CONFIG_VERSION) {
            fsFsDeleteFile(std::addressof(devices[FileSystemSDMC]), config_path);
            cfg = {};
            json_decref(root);
            return Config::Save(cfg);
        }

        // Parse dev_options (add type check)
        json_t *dev_options = json_object_get(root, "dev_options");
        if (json_is_integer(dev_options))
            cfg.dev_options = json_integer_value(dev_options) != 0;

        // Parse image_filename (add type check)
        json_t *image_filename = json_object_get(root, "image_filename");
        if (json_is_integer(image_filename))
            cfg.image_filename = json_integer_value(image_filename) != 0;

        // System language detection logic (retained)
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

        // Parse language (retained, type check already added)
        json_t *language = json_object_get(root, "language");
        if (language && json_is_string(language))
            cfg.lang = str2locale(json_string_value(language));

        // Parse multi_lang (add type check)
        json_t *multi_lang = json_object_get(root, "multi_lang");
        if (json_is_integer(multi_lang))
            cfg.multi_lang = json_integer_value(multi_lang) != 0;
        // Force multi_lang to 1 if language is not English
        if (cfg.lang != Locale::English)
            cfg.multi_lang = 1;

        // Set full_charset to 0 if not running as application/system application
        const auto type = appletGetAppletType();
        if (type != AppletType_Application && type != AppletType_SystemApplication)
            cfg.full_charset = 0;

        // Free JSON object
        json_decref(root);

        return 0;
    }
}