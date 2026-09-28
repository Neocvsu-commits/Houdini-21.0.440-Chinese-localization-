#include "../../source/translation/translation_json.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>

namespace {

bool check(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char **argv) {
    std::unordered_map<std::string, std::string> values;
    const std::string valid =
        "\xEF\xBB\xBF"
        R"({"description":"mentions translations first","metadata":{"n":1,"ok":true},"translations":{"Curly \u201cquote\u201d":"\u4e2d\u6587","Emoji":"\ud83d\ude00","Empty":""}})";
    if (!check(HoudiniChineseJson::parseTranslations(valid, values),
               "valid document parses") ||
        !check(values.at("Curly \xE2\x80\x9Cquote\xE2\x80\x9D") ==
                   "\xE4\xB8\xAD\xE6\x96\x87",
               "BMP escapes decode as UTF-8") ||
        !check(values.at("Emoji") == "\xF0\x9F\x98\x80",
               "surrogate pair decodes as UTF-8") ||
        !check(values.at("Empty").empty(), "empty translations are retained")) {
        return 1;
    }

    HoudiniChineseJson::DictionaryDocument complete;
    if (!check(
            HoudiniChineseJson::parseDictionaryDocument(
                R"({"translations":{"Save":"保存"},"templates":{"Frame {n:int}":"第 {n:int} 帧"}})",
                complete),
            "combined dictionary document parses") ||
        !check(complete.hasTranslations && complete.hasTemplates,
               "document records both sections") ||
        !check(complete.translations.at("Save") == "保存",
               "ordinary translations are retained") ||
        !check(complete.templates.at("Frame {n:int}") == "第 {n:int} 帧",
               "template definitions are retained")) {
        return 1;
    }

    HoudiniChineseJson::Parser reusableParser(
        R"({"translations":{"Save":"保存"}})");
    HoudiniChineseJson::DictionaryDocument firstPass;
    HoudiniChineseJson::DictionaryDocument secondPass;
    if (!check(reusableParser.parseDictionaryDocument(firstPass),
               "parser accepts its document on the first pass") ||
        !check(reusableParser.parseDictionaryDocument(secondPass),
               "parser resets its cursor before a repeated parse") ||
        !check(firstPass.translations == secondPass.translations,
               "repeated parses produce the same dictionary")) {
        return 1;
    }

    std::string replaceableInput = R"({"translations":{"Saved":"original"}})";
    HoudiniChineseJson::Parser savedParser(replaceableInput);
    replaceableInput.assign(4096, 'x');
    HoudiniChineseJson::DictionaryDocument savedDocument;
    if (!check(savedParser.parseDictionaryDocument(savedDocument) &&
                   savedDocument.translations.at("Saved") == "original",
               "parser owns its input independently of caller lifetime"))
        return 1;

    const auto original = values;
    if (!check(!HoudiniChineseJson::parseTranslations(
                   R"({"translations":{"Good":"ok","Broken":)", values),
               "truncated document is rejected") ||
        !check(values == original,
               "failed parse leaves destination unchanged") ||
        !check(!HoudiniChineseJson::parseTranslations(
                   R"({"translations":{"Bad":"\ud800"}})", values),
               "unpaired surrogate is rejected") ||
        !check(!HoudiniChineseJson::parseTranslations(
                   R"({"translations":{},"translations":{}})", values),
               "duplicate translations property is rejected") ||
        !check(!HoudiniChineseJson::parseDictionaryDocument(
                   R"({"templates":{},"templates":{}})", complete),
               "duplicate templates property is rejected") ||
        !check(!HoudiniChineseJson::parseTranslations(
                   R"({"translations":{"Bad":1}})", values),
               "non-string translation is rejected") ||
        !check(!HoudiniChineseJson::parseTranslations(
                   R"({"translations":{"Same":"one","Same":"two"}})", values),
               "duplicate translation key is rejected") ||
        !check(!HoudiniChineseJson::parseTranslations(
                   std::string("{\"translations\":{\"Bad\":\"") + "\xC0\xAF" +
                       "\"}}",
                   values),
               "invalid UTF-8 is rejected")) {
        return 1;
    }

    for (int index = 1; index < argc; ++index) {
        std::ifstream file(argv[index], std::ios::binary);
        if (!check(file.is_open(), "dictionary file can be opened")) {
            std::cerr << "FILE: " << argv[index] << '\n';
            return 1;
        }
        const std::string document{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
        HoudiniChineseJson::DictionaryDocument dictionary;
        const bool templateFile =
            std::string(argv[index]).find("templates_zh.json") !=
            std::string::npos;
        if (!check(HoudiniChineseJson::parseDictionaryDocument(document,
                                                               dictionary),
                   "repository dictionary parses") ||
            !check(templateFile ? dictionary.hasTemplates
                                : dictionary.hasTranslations &&
                                      !dictionary.translations.empty(),
                   "repository dictionary has its required section")) {
            std::cerr << "FILE: " << argv[index] << '\n';
            return 1;
        }
    }

    std::cout << "translation_json_test: OK\n";
    return 0;
}
