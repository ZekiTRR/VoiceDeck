#include "core/ai/VoiceLibrary.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace vd {

bool VoiceLibrary::baseModelsInstalled() const {
    std::error_code ec;
    return std::filesystem::exists(encoderPath_, ec) && std::filesystem::exists(pitchPath_, ec);
}

bool VoiceLibrary::loadJson(const std::filesystem::path& jsonFile, AiVoiceInfo& out) {
    std::ifstream in(jsonFile);
    if (!in) return false;
    try {
        nlohmann::json j;
        in >> j;
        // Voice CTX writes camelCase keys; accept both spellings.
        auto jget = [&j](const char* a, const char* b, auto def) {
            auto it = j.find(a);
            if (it == j.end()) it = j.find(b);
            if (it == j.end()) return def;
            try {
                return it->get<std::remove_reference_t<decltype(def)>>();
            } catch (...) {
                return def;
            }
        };
        out = AiVoiceInfo{};
        out.format = jget("Format", "format", 0);
        out.id = jget("Id", "id", std::string());
        out.name = jget("Name", "name", std::string());
        out.sampleRate = jget("SampleRate", "sampleRate", 0);
        out.blockMs = jget("BlockMs", "blockMs", 100);
        out.contextMs = jget("ContextMs", "contextMs", 600);
        out.frames = jget("Frames", "frames", 0);
        out.tail = jget("Tail", "tail", 0);
        out.marginFrames = jget("MarginFrames", "marginFrames", 0);
        out.gender = jget("Gender", "gender", std::string("other"));
        out.defaultPitchShift = jget("DefaultPitchShift", "defaultPitchShift", 0);
        out.credits = jget("Credits", "credits", std::string());
        out.license = jget("License", "license", std::string());
        out.builtIn = jget("BuiltIn", "builtIn", false);
        return true;
    } catch (...) {
        return false;
    }
}

bool VoiceLibrary::isSupported(const AiVoiceInfo& v) {
    return v.format == 1 && !v.id.empty() && v.sampleRate >= 16000 && v.sampleRate <= 48000 &&
           v.sampleRate % 100 == 0 && v.blockMs > 0 && v.contextMs > 0 && v.tail > v.marginFrames &&
           v.tail - v.marginFrames == v.blockMs / 10 &&
           v.frames == expectedFrames(v.blockMs, v.contextMs);
}

int VoiceLibrary::expectedFrames(int blockMs, int contextMs) {
    return ((blockMs + contextMs) * 16 - 400) / 320 * 2 + 2;
}

std::vector<AiVoiceInfo> VoiceLibrary::scanVoices() const {
    std::vector<AiVoiceInfo> voices;
    std::error_code ec;
    if (!std::filesystem::exists(voicesFolder_, ec)) return voices;
    for (const auto& entry : std::filesystem::directory_iterator(voicesFolder_, ec)) {
        if (entry.path().extension() != ".json") continue;
        std::filesystem::path model = entry.path();
        model.replace_extension(".onnx");
        if (!std::filesystem::exists(model, ec)) continue;
        AiVoiceInfo info;
        if (!loadJson(entry.path(), info)) continue;
        if (!isSupported(info)) continue;
        info.modelPath = model;
        voices.push_back(std::move(info));
    }
    std::stable_sort(voices.begin(), voices.end(), [](const AiVoiceInfo& a, const AiVoiceInfo& b) {
        if (a.builtIn != b.builtIn) return a.builtIn;
        return a.name < b.name;
    });
    return voices;
}

std::string VoiceLibrary::import(const std::filesystem::path& onnxPath, AiVoiceInfo& out) {
    std::filesystem::path json = onnxPath;
    json.replace_extension(".json");
    std::error_code ec;
    if (!std::filesystem::exists(json, ec))
        return "a voice needs its .json description next to the .onnx file";
    AiVoiceInfo info;
    if (!loadJson(json, info)) return "voice description is malformed";
    if (!isSupported(info)) return "this voice was made for a different format/version";

    std::filesystem::create_directories(voicesFolder_, ec);
    std::string base = info.id;
    for (char& c : base) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
            c == '?' || c == '*')
            c = '_';
    }
    std::filesystem::copy_file(onnxPath, voicesFolder_ / (base + ".onnx"),
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return "failed to copy the voice model";
    std::filesystem::copy_file(json, voicesFolder_ / (base + ".json"),
                                std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return "failed to copy the voice description";
    info.modelPath = voicesFolder_ / (base + ".onnx");
    out = info;
    return "";
}

} // namespace vd
