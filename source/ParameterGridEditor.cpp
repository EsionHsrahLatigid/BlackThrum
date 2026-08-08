#include "ParameterGridEditor.h"

#include "BlackThrumPlugin.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace blackthrum::plugin
{
namespace
{
class MomentaryTriggerButton final : public yup::TextButton
{
public:
    using yup::TextButton::TextButton;

    std::function<void (bool)> onGateChanged;

    void mouseDown (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseDown (event);
        if (onGateChanged)
            onGateChanged (true);
    }

    void mouseUp (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseUp (event);
        if (onGateChanged)
            onGateChanged (false);
    }

    void mouseExit (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseExit (event);
        if (onGateChanged && isButtonDown())
            onGateChanged (false);
    }
};

class OutputMeter final : public yup::Component
{
public:
    void setLevel (float newLevel)
    {
        level = std::clamp (newLevel, 0.0f, 1.0f);
        repaint();
    }

    void paint (yup::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds();
        graphics.setFillColor (0xff15171bu);
        graphics.fillRect (bounds.to<float>());

        graphics.setFillColor (0xff2b3038u);
        graphics.fillRect (bounds.withTrimmedLeft (bounds.getWidth() * level).to<float>());

        graphics.setFillColor (0xfff05a28u);
        graphics.fillRect (0.0f, 0.0f, bounds.getWidth() * level, bounds.getHeight());
    }

private:
    float level = 0.0f;
};
} // namespace

ParameterGridEditor::ParameterGridEditor (yup::AudioProcessor& processor,
                                          yup::StringRef newTitle,
                                          yup::StringRef newWarning,
                                          std::uint32_t newAccentColor)
    : title (newTitle)
    , warning (newWarning)
    , accentColor (newAccentColor)
{
    blackthrumProcessor = dynamic_cast<BlackThrumPlugin*> (&processor);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*warningLabel);

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<yup::Slider> (yup::Slider::RotaryVerticalDrag);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->setClickingGrabFocus (false);
        slider->onDragStart = [parameter] (const yup::MouseEvent&) { parameter->beginChangeGesture(); };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [parameter] (const yup::MouseEvent&) { parameter->endChangeGesture(); };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        addAndMakeVisible (*valueLabel);
        valueLabels.push_back (std::move (valueLabel));
    }

    if (blackthrumProcessor != nullptr)
    {
        triggerButton = std::make_unique<MomentaryTriggerButton>();
        triggerButton->setButtonText ("Trigger");
        triggerButton->setMouseCursor (yup::MouseCursor::Hand);
        triggerButton->setClickingGrabFocus (false);
        static_cast<MomentaryTriggerButton*> (triggerButton.get())->onGateChanged = [this] (bool shouldBeOn)
        {
            mouseGateHeld = shouldBeOn;
            publishTriggerGate();
        };
        addAndMakeVisible (*triggerButton);

        meterLabel = std::make_unique<yup::Label>();
        meterLabel->setText ("Output", yup::dontSendNotification);
        meterLabel->setJustification (yup::Justification::centerLeft);
        addAndMakeVisible (*meterLabel);

        outputMeter = std::make_unique<OutputMeter>();
        addAndMakeVisible (*outputMeter);

        setWantsKeyboardFocus (true);
    }

    setSize (getPreferredSize().to<float>());
    startTimerHz (30);
}

ParameterGridEditor::~ParameterGridEditor()
{
    mouseGateHeld = false;
    spaceGateHeld = false;
    publishTriggerGate();
}

bool ParameterGridEditor::isResizable() const
{
    return true;
}

bool ParameterGridEditor::shouldPreserveAspectRatio() const
{
    return true;
}

yup::Size<int> ParameterGridEditor::getPreferredSize() const
{
    return { 940, 520 };
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    graphics.setFillColor (0xff0a0b0du);
    graphics.fillAll();

    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());

    graphics.setStrokeColor (0xff1d2328u);
    graphics.setStrokeWidth (1.0f);
    for (float x = -height; x < width + height; x += 34.0f)
        graphics.strokeLine (x, 5.0f, x + height * 0.72f, height);

    graphics.setStrokeColor (0xff2d353au);
    graphics.setStrokeWidth (0.8f);
    for (float x = 0.0f; x < width + height; x += 46.0f)
        graphics.strokeLine (x, 5.0f, x - height * 0.46f, height);

    graphics.setFillColor (0x990a0b0du);
    graphics.fillRect (0.0f, 5.0f, width, 116.0f);

    graphics.setFillColor (accentColor);
    graphics.fillRect (0.0f, 0.0f, getWidth(), 5.0f);
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 5;
    constexpr float margin = 20.0f;
    constexpr float top = 124.0f;
    constexpr float gap = 12.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlGap = 4.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;

    titleLabel->setBounds (24.0f, 12.0f, bounds.getWidth() - 48.0f, 30.0f);
    warningLabel->setBounds (24.0f, 43.0f, bounds.getWidth() - 48.0f, 24.0f);

    if (triggerButton != nullptr && meterLabel != nullptr && outputMeter != nullptr)
    {
        constexpr float triggerWidth = 132.0f;
        constexpr float controlHeight = 32.0f;
        const auto triggerX = margin;
        const auto meterX = triggerX + triggerWidth + gap;
        const auto meterWidth = std::max (80.0f, bounds.getWidth() - margin - meterX);

        triggerButton->setBounds (triggerX, 80.0f, triggerWidth, controlHeight);
        meterLabel->setBounds (meterX, 80.0f, 70.0f, controlHeight);
        outputMeter->setBounds (meterX + 78.0f, 86.0f, meterWidth - 78.0f, 20.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto controlHeight = cellHeight - labelHeight - valueHeight - 2.0f * controlGap;
        const auto controlSize = std::max (20.0f, std::min (cellWidth - 8.0f, controlHeight));
        const auto controlX = x + 0.5f * (cellWidth - controlSize);
        const auto controlY = y + labelHeight + controlGap;

        labels[i]->setBounds (x, y, cellWidth, labelHeight);
        sliders[i]->setBounds (controlX, controlY, controlSize, controlSize);
        valueLabels[i]->setBounds (x, y + cellHeight - valueHeight, cellWidth, valueHeight);
    }
}

void ParameterGridEditor::focusLost()
{
    yup::AudioProcessorEditor::focusLost();
    mouseGateHeld = false;
    spaceGateHeld = false;
    publishTriggerGate();
}

void ParameterGridEditor::keyDown (const yup::KeyPress& key, const yup::Point<float>& position)
{
    yup::AudioProcessorEditor::keyDown (key, position);

    if (key.getKey() == yup::KeyPress::spaceKey && ! spaceGateHeld)
    {
        spaceGateHeld = true;
        publishTriggerGate();
    }
}

void ParameterGridEditor::keyUp (const yup::KeyPress& key, const yup::Point<float>& position)
{
    yup::AudioProcessorEditor::keyUp (key, position);

    if (key.getKey() == yup::KeyPress::spaceKey)
    {
        spaceGateHeld = false;
        publishTriggerGate();
    }
}

void ParameterGridEditor::timerCallback()
{
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        if (! sliders[i]->isCurrentlyBeingDragged())
            sliders[i]->setValue (parameters[i]->getValue(), yup::dontSendNotification);
        valueLabels[i]->setText (parameters[i]->toString(), yup::dontSendNotification);
    }

    if (blackthrumProcessor != nullptr && outputMeter != nullptr)
    {
        const auto latestPeak = blackthrumProcessor->getOutputPeakLevel();
        displayedPeak = std::max (latestPeak, displayedPeak * 0.82f);
        static_cast<OutputMeter*> (outputMeter.get())->setLevel (displayedPeak);
    }
}

void ParameterGridEditor::publishTriggerGate()
{
    if (blackthrumProcessor != nullptr)
        blackthrumProcessor->setStandaloneTriggerGate (mouseGateHeld || spaceGateHeld);
}

} // namespace blackthrum::plugin
