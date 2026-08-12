#include "ParameterGridEditor.h"

#include <ehl/yup_plugin_ui/EhlPluginTheme.h>
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

} // namespace

ParameterGridEditor::ParameterGridEditor (yup::AudioProcessor& processor,
                                          yup::StringRef newTitle,
                                          yup::StringRef newWarning,
                                          std::uint32_t newAccentColor)
    : title (newTitle)
    , warning (newWarning)
{
    (void) newAccentColor;
    blackthrumProcessor = dynamic_cast<BlackThrumPlugin*> (&processor);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*titleLabel, ehl::ui::TextRole::primary);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*warningLabel, ehl::ui::TextRole::secondary);
    addAndMakeVisible (*warningLabel);

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        ehl::ui::styleLabel (*label, ehl::ui::TextRole::secondary);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<ehl::ui::PixelSlider> (yup::Slider::RotaryVerticalDrag);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->setClickingGrabFocus (false);
        slider->onDragStart = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->beginChangeGesture();
        };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->endChangeGesture();
        };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        ehl::ui::styleLabel (*valueLabel, ehl::ui::TextRole::primary);
        addAndMakeVisible (*valueLabel);
        valueLabels.push_back (std::move (valueLabel));
    }

    if (blackthrumProcessor != nullptr)
    {
        triggerButton = std::make_unique<MomentaryTriggerButton>();
        triggerButton->setButtonText ("Trigger");
        triggerButton->setMouseCursor (yup::MouseCursor::Hand);
        triggerButton->setClickingGrabFocus (false);
        triggerButton->setColor (yup::TextButton::Style::backgroundColorId, yup::Color (ehl::ui::low));
        triggerButton->setColor (yup::TextButton::Style::backgroundPressedColorId, yup::Color (ehl::ui::paper));
        triggerButton->setColor (yup::TextButton::Style::textColorId, yup::Color (ehl::ui::paper));
        triggerButton->setColor (yup::TextButton::Style::textPressedColorId, yup::Color (ehl::ui::ink));
        triggerButton->setColor (yup::TextButton::Style::outlineColorId, yup::Color (ehl::ui::mid));
        static_cast<MomentaryTriggerButton*> (triggerButton.get())->onGateChanged = [this] (bool shouldBeOn)
        {
            mouseGateHeld = shouldBeOn;
            publishTriggerGate();
        };
        addAndMakeVisible (*triggerButton);

        meterLabel = std::make_unique<yup::Label>();
        meterLabel->setText ("Output", yup::dontSendNotification);
        meterLabel->setJustification (yup::Justification::centerLeft);
        ehl::ui::styleLabel (*meterLabel, ehl::ui::TextRole::secondary);
        addAndMakeVisible (*meterLabel);

        outputMeter = std::make_unique<ehl::ui::StripMeter> (ehl::ui::paper);
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
    return ehl::ui::preferredSize;
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    ehl::ui::paintEditorBackground (graphics, getWidth(), getHeight());
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 7;
    constexpr float margin = 16.0f;
    constexpr float top = 128.0f;
    constexpr float gap = 8.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlSize = 72.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;
    const auto labelInset = rows > 1 ? 4.0f : 12.0f;
    const auto valueInset = rows > 1 ? 4.0f : 12.0f;

    titleLabel->setBounds (20.0f, 8.0f, bounds.getWidth() - 40.0f, 28.0f);
    warningLabel->setBounds (20.0f, 36.0f, bounds.getWidth() - 40.0f, 20.0f);

    if (triggerButton != nullptr && meterLabel != nullptr && outputMeter != nullptr)
    {
        constexpr float triggerWidth = 104.0f;
        constexpr float controlHeight = 28.0f;
        const auto triggerX = margin;
        const auto meterX = triggerX + triggerWidth + gap;
        const auto meterWidth = std::max (80.0f, bounds.getWidth() - margin - meterX);

        triggerButton->setBounds (triggerX, 72.0f, triggerWidth, controlHeight);
        meterLabel->setBounds (meterX, 68.0f, 52.0f, 16.0f);
        outputMeter->setBounds (meterX + 52.0f, 76.0f, meterWidth - 52.0f, 12.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto labelY = y + labelInset;
        const auto valueY = y + cellHeight - valueHeight - valueInset;
        const auto controlTop = labelY + labelHeight;
        const auto controlBottom = valueY;
        const auto fittedControlSize = std::min ({ controlSize, cellWidth - 8.0f, std::max (20.0f, controlBottom - controlTop) });
        const auto controlX = x + 0.5f * (cellWidth - fittedControlSize);
        const auto controlY = controlTop + 0.5f * (controlBottom - controlTop - fittedControlSize);

        labels[i]->setBounds (x + 2.0f, labelY, cellWidth - 4.0f, labelHeight);
        sliders[i]->setBounds (controlX, controlY, fittedControlSize, fittedControlSize);
        valueLabels[i]->setBounds (x + 2.0f, valueY, cellWidth - 4.0f, valueHeight);
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
        outputMeter->setLevel (displayedPeak);
    }
}

void ParameterGridEditor::publishTriggerGate()
{
    if (blackthrumProcessor != nullptr)
        blackthrumProcessor->setStandaloneTriggerGate (mouseGateHeld || spaceGateHeld);
}

} // namespace blackthrum::plugin
