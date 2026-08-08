#pragma once

#include <yup_audio_processors/yup_audio_processors.h>

#include <array>
#include <cmath>
#include <vector>

namespace blackthrum::plugin
{

/** Saves a product state as stable parameter-ID/value pairs. */
inline yup::Result saveProductState (yup::AudioProcessor& processor,
                                     yup::MemoryBlock& data,
                                     const std::array<char, 4>& magic,
                                     int version,
                                     int presetIndex)
{
    yup::MemoryOutputStream stream (data, false);
    const auto parameters = processor.getParameters();

    if (! stream.write (magic.data(), magic.size())
        || ! stream.writeInt (version)
        || ! stream.writeInt (presetIndex)
        || ! stream.writeInt (static_cast<int> (parameters.size())))
        return yup::Result::fail ("Failed to write product state header");

    for (const auto& parameter : parameters)
    {
        const auto value = parameter->getValue();
        if (! std::isfinite (value)
            || ! stream.writeString (parameter->getID())
            || ! stream.writeFloat (value))
            return yup::Result::fail ("Failed to write product parameter state");
    }

    stream.flush();
    return yup::Result::ok();
}

/** Loads a complete product state by stable ID without relying on parameter order. */
inline yup::Result loadProductState (yup::AudioProcessor& processor,
                                     const yup::MemoryBlock& data,
                                     const std::array<char, 4>& magic,
                                     int version,
                                     int numPresets,
                                     int& presetIndex)
{
    yup::MemoryInputStream stream (data, false);
    std::array<char, 4> loadedMagic {};
    if (stream.read (loadedMagic.data(), loadedMagic.size()) != static_cast<int> (loadedMagic.size())
        || loadedMagic != magic)
        return yup::Result::fail ("Invalid product state header");

    if (stream.readInt() != version)
        return yup::Result::fail ("Unsupported product state version");

    const auto loadedPreset = stream.readInt();
    if (! yup::isPositiveAndBelow (loadedPreset, numPresets))
        return yup::Result::fail ("Invalid product preset index");

    const auto parameterCount = stream.readInt();
    const auto parameters = processor.getParameters();
    if (parameterCount != static_cast<int> (parameters.size()))
        return yup::Result::fail ("Invalid product parameter count");

    struct PendingValue
    {
        yup::AudioParameter::Ptr parameter;
        float value = 0.0f;
    };

    std::vector<PendingValue> pending;
    std::vector<yup::String> loadedIDs;
    pending.reserve (parameters.size());
    loadedIDs.reserve (parameters.size());

    for (int i = 0; i < parameterCount; ++i)
    {
        const auto id = stream.readString();
        const auto value = stream.readFloat();
        const auto parameter = processor.getParameterByID (id);
        if (id.isEmpty() || parameter == nullptr || ! std::isfinite (value)
            || value < parameter->getMinimumValue() || value > parameter->getMaximumValue())
            return yup::Result::fail ("Invalid product parameter state");

        for (const auto& loadedID : loadedIDs)
            if (loadedID == id)
                return yup::Result::fail ("Duplicate product parameter state");

        loadedIDs.push_back (id);
        pending.push_back ({ parameter, value });
    }

    if (! stream.isExhausted())
        return yup::Result::fail ("Unexpected trailing product state data");

    for (const auto& item : pending)
        item.parameter->setValue (item.value);

    presetIndex = loadedPreset;
    return yup::Result::ok();
}

} // namespace blackthrum::plugin
