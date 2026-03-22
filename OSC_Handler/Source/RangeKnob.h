/*
  ==============================================================================
    RangeKnob.h

    Two concentric arcs on a 270° rotary track (gap at 6 o'clock):

    Inner arc  — standard rotary: gray track + blue fill + blue ball at
                 the CC output value [0,1].

    Outer arc  — input range display:
                   dark segment    = outside [lo, hi]  (lo = min of the two handles)
                   light segment   = inside  [lo, hi]
                   colour          = light blue (normal) | orange (inverted)
                   small white dot = current raw input value

    Inverted range: rangeMin_ > rangeMax_ is allowed. The mapping then runs
    in reverse (high input → low output). The arc section lights up in orange
    to signal inversion. An "⇅" button in the centre swaps the two handles.

    Drag the outer arc to move whichever handle is nearest.
    Handles are allowed to cross, enabling inversion by dragging.
    Double-click resets range to [0, 1] (non-inverted).

    Angle convention: 0 = 12 o'clock, clockwise positive.
      kStartA = -0.75π  (7 o'clock)
      kEndA   = +0.75π  (5 o'clock)
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>

class RangeKnob : public juce::Component
{
public:
    std::function<void(float min, float max)> onRangeChanged;

    RangeKnob()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        invertBtn_.setButtonText ("inv");
        invertBtn_.setClickingTogglesState (false);
        invertBtn_.onClick = [this]()
        {
            std::swap (rangeMin_, rangeMax_);
            repaint();
            if (onRangeChanged) onRangeChanged (rangeMin_, rangeMax_);
        };
        addAndMakeVisible (invertBtn_);
    }

    // lo and hi may be in any order — lo > hi signals an inverted range
    void setNormalizedRange (float lo, float hi)
    {
        rangeMin_ = juce::jlimit (0.f, 1.f, lo);
        rangeMax_ = juce::jlimit (0.f, 1.f, hi);
        repaint();
    }

    void setInputValue  (float v) { inputVal_  = juce::jlimit (0.f, 1.f, v); repaint(); }
    void setOutputValue (float v) { outputVal_ = juce::jlimit (0.f, 1.f, v); repaint(); }
    void setCentreLabel (const juce::String& t) { label_ = t; repaint(); }

    float getRangeMin() const { return rangeMin_; }
    float getRangeMax() const { return rangeMax_; }
    bool  isInverted()  const { return rangeMin_ > rangeMax_; }

    // ── layout ─────────────────────────────────────────────────────────────────
    void resized() override
    {
        auto  cx = getLocalBounds().toFloat().reduced (4.f).getCentre();
        float R  = getLocalBounds().toFloat().reduced (4.f).getWidth() * 0.5f;
        float bw = R * 0.60f, bh = R * 0.28f;
        invertBtn_.setBounds (juce::Rectangle<float> (cx.x - bw * 0.5f,
                                                      cx.y + R * 0.12f,
                                                      bw, bh).toNearestInt());
    }

    // ── paint ──────────────────────────────────────────────────────────────────
    void paint (juce::Graphics& g) override
    {
        auto  b  = getLocalBounds().toFloat().reduced (4.f);
        auto  cx = b.getCentre();
        float R  = b.getWidth() * 0.5f;

        const float outerR   = R * 0.90f;
        const float outerW   = R * 0.09f;
        const float innerR   = R * 0.62f;
        const float innerW   = R * 0.09f;
        const float inputDot = R * 0.050f;
        const float ballR    = innerW * 0.85f;

        const bool  inv      = isInverted();
        const float lo       = inv ? rangeMax_ : rangeMin_;   // lower value
        const float hi       = inv ? rangeMin_ : rangeMax_;   // higher value
        const juce::Colour rangeColour = inv ? juce::Colour (0xffffaa44)   // orange
                                              : juce::Colour (0xff4a8ab0); // blue

        // ── Outer arc — input range ────────────────────────────────────────────
        drawArc (g, cx, outerR, kStartA, kEndA,
                 juce::Colour (0xff222a36), outerW);

        if (hi > lo)
            drawArc (g, cx, outerR,
                     valToAngle (lo), valToAngle (hi),
                     rangeColour, outerW);

        // Small white dot for current input value
        {
            auto pt = angleToScreen (valToAngle (inputVal_), outerR, cx);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillEllipse (pt.x - inputDot, pt.y - inputDot,
                           inputDot * 2.f, inputDot * 2.f);
        }

        // ── Inner arc — CC output value ────────────────────────────────────────
        drawArc (g, cx, innerR, kStartA, kEndA,
                 juce::Colour (0xff333333), innerW);

        if (outputVal_ > 0.f)
            drawArc (g, cx, innerR, kStartA, valToAngle (outputVal_),
                     juce::Colour (0xff3377cc), innerW);

        // Blue ball
        {
            auto pt = angleToScreen (valToAngle (outputVal_), innerR, cx);
            g.setColour (juce::Colour (0xff66aaff));
            g.fillEllipse (pt.x - ballR, pt.y - ballR, ballR * 2.f, ballR * 2.f);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.drawEllipse (pt.x - ballR, pt.y - ballR, ballR * 2.f, ballR * 2.f, 0.7f);
        }

        // ── Centre: label normally, dragged value while editing ───────────────
        {
            auto textRect = juce::Rectangle<float> (cx.x - innerR, cx.y - R * 0.42f,
                                                    innerR * 2.f, R * 0.34f);
            if (dragging_ >= 0)
            {
                float dragVal = (dragging_ == 0) ? rangeMin_ : rangeMax_;
                juce::Colour col = (dragging_ == 0) ? juce::Colour (0xff88ccff)
                                                     : juce::Colour (0xffffaa44);
                g.setColour (col);
                g.setFont (juce::Font (juce::FontOptions().withHeight (R * 0.28f)
                                                           .withStyle ("Bold")));
                g.drawText (juce::String (dragVal, 3), textRect,
                            juce::Justification::centred);
            }
            else if (label_.isNotEmpty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.65f));
                g.setFont (juce::Font (juce::FontOptions().withHeight (R * 0.24f)));
                g.drawText (label_, textRect, juce::Justification::centred);
            }
        }
    }

    // ── mouse ──────────────────────────────────────────────────────────────────
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging_ = nearestHandle (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging_ < 0) return;
        float v = pointToValue (e.position);

        // Snap to 0.5 (mid-output) within a small angular threshold
        static constexpr float kSnapTol = 0.025f;
        if (std::abs (v - 0.5f) < kSnapTol) v = 0.5f;

        if (dragging_ == 0)
            rangeMin_ = juce::jlimit (0.f, 1.f, v);
        else
            rangeMax_ = juce::jlimit (0.f, 1.f, v);
        repaint();
        if (onRangeChanged) onRangeChanged (rangeMin_, rangeMax_);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging_ = -1; }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        setNormalizedRange (0.f, 1.f);
        if (onRangeChanged) onRangeChanged (0.f, 1.f);
    }

private:
    static constexpr float kStartA = -juce::MathConstants<float>::pi * 0.75f;
    static constexpr float kEndA   =  juce::MathConstants<float>::pi * 0.75f;
    static constexpr float kSpan   = kEndA - kStartA;

    float rangeMin_  = 0.f;
    float rangeMax_  = 1.f;
    float inputVal_  = 0.f;
    float outputVal_ = 0.f;
    int   dragging_  = -1;
    juce::String   label_;
    juce::TextButton invertBtn_;

    float valToAngle (float v) const { return kStartA + v * kSpan; }
    float angleToVal (float a) const { return juce::jlimit (0.f, 1.f, (a - kStartA) / kSpan); }

    juce::Point<float> angleToScreen (float a, float r, juce::Point<float> cx) const
    {
        return { cx.x + r * std::sin (a), cx.y - r * std::cos (a) };
    }

    float pointToValue (juce::Point<float> pt) const
    {
        auto  cx = getLocalBounds().toFloat().reduced (4.f).getCentre();
        float a  = std::atan2 (pt.x - cx.x, -(pt.y - cx.y));
        return angleToVal (juce::jlimit (kStartA, kEndA, a));
    }

    int nearestHandle (juce::Point<float> pt) const
    {
        float v = pointToValue (pt);
        return (std::abs (v - rangeMin_) < std::abs (v - rangeMax_)) ? 0 : 1;
    }

    void drawArc (juce::Graphics& g, juce::Point<float> cx, float r,
                  float a0, float a1, juce::Colour colour, float w) const
    {
        juce::Path arc;
        arc.addCentredArc (cx.x, cx.y, r, r, 0.f, a0, a1, true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RangeKnob)
};
