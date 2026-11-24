#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vd {

// Metadata written next to every voice model. Format 1 is the Voice CTX
// compatible 100/600/40 layout; format 2 is the low-latency 50/400/10
// layout. These are the "expected tensor dimensionalities" the streaming
// pipeline is built around, validated at load time.
struct AiVoiceInfo {
    int format = 0;
    std::string id;
    std::string name;
    int sampleRate = 0;
    int blockMs = 100;
    int contextMs = 600;
    int frames = 0;       // generator "phone" frames (100 fps)
    int tail = 0;         // generator output frames (100 fps)
    int marginFrames = 0;  // crossfade overlap frames
    std::string gender = "other";
    int defaultPitchShift = 0;
    std::string precision = "fp32"; // "fp16" for GPU-optimized conversions
    std::string credits;
    std::string license;
    bool builtIn = false;
    std::filesystem::path modelPath;
};

// Locates the AI model folder and the voices installed in it.
class VoiceLibrary {
public:
    static constexpr const char* kEncoderFile = "content_encoder.onnx";
    static constexpr const char* kPitchFile = "rmvpe.onnx";

    explicit VoiceLibrary(std::filesystem::path root)
        : root_(std::move(root)),
          voicesFolder_(root_ / "voices"),
          encoderPath_(root_ / kEncoderFile),
          pitchPath_(root_ / kPitchFile) {}

    const std::filesystem::path& root() const { return root_; }
    const std::filesystem::path& voicesFolder() const { return voicesFolder_; }
    const std::filesystem::path& encoderPath() const { return encoderPath_; }
    const std::filesystem::path& pitchPath() const { return pitchPath_; }

    bool baseModelsInstalled() const;

    // Valid voices (built-in first), skipping malformed or unsupported files.
    std::vector<AiVoiceInfo> scanVoices() const;

    // Copies a converted voice (.onnx + .json) into the voices folder.
    // "" on success, else an error message.
    std::string import(const std::filesystem::path& onnxPath, AiVoiceInfo& out);

    static bool isSupported(const AiVoiceInfo& v);
    static int expectedFrames(int blockMs, int contextMs);
    static bool loadJson(const std::filesystem::path& jsonFile, AiVoiceInfo& out);

private:
    std::filesystem::path root_;
    std::filesystem::path voicesFolder_;
    std::filesystem::path encoderPath_;
    std::filesystem::path pitchPath_;
};

} // namespace vd
