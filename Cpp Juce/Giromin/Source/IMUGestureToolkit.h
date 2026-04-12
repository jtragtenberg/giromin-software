/*
  ==============================================================================

    IMUGestureToolkit.h
    Created: 26 Jul 2024 4:53:55pm
    Author:  Solomon Moulang Lewis

  ==============================================================================
*/
    
/**
 NOTE: because these methods are 'static', they can be used ANYWHERE in the codebase.
    
 To USE: GirominMathUtils::'method_name' (p1, ... pN);
 
 e.g. float filtered = GirominMathUtils::(in, filtered_val, filtering_amount);
 */

#pragma once

class IMUGestureToolkit
{
public:

    // Constructor to initialize default values
    IMUGestureToolkit()
        : toggle_state_(0.0f),
          filtered_value_(0.0f),
          last_raw_value_(0.0f)
    {
    }
    
    enum class ButtonAction {
        PUSH,           // Output the raw value
        INVERTED_PUSH,  // Output the opposite value
        TOGGLE          // Output the toggled value
    };
    
    enum class GyroAxis {
        X,              // Gyroscope X-axis
        Y,              // Gyroscope Y-axis
        Z,              // Gyroscope Z-axis
        MAGNITUDE       // Quadratic sum of all axes
    };

    enum class GyroDirection {
        POSITIVE,       // Only positive values
        NEGATIVE,       // Only negative values
        BOTH,           // Both positive and negative values
        ABSOLUTE        // Absolute values
    };
    
    float processButtonSignal(float input, ButtonAction action) {
        switch (action) {
            case ButtonAction::PUSH:
                return input;
            case ButtonAction::INVERTED_PUSH:
                return (input == 0.0f) ? 1.0f : 0.0f;
            case ButtonAction::TOGGLE:
                
                
                
                if (input == 1.f)
                {
                    toggle_state_ = (toggle_state_ == 0.0f) ? 1.0f : 0.0f;
                }
                return toggle_state_;
            default:
                throw std::invalid_argument("Unknown ButtonAction");
        }
    }
    
    float processRotationRate (const std::array<float, 3>& gyroData,
                               GyroAxis axis,
                               GyroDirection direction,
                               float gain = 1.0f,
                               float riseFilteringAmount = 0.0f,
                               float fallFilteringAmount = 0.0f)
    {
        float value;

        // Select axis or magnitude
        switch (axis) {
            case GyroAxis::X:
                value = gyroData[0];
                break;
            case GyroAxis::Y:
                value = gyroData[1];
                break;
            case GyroAxis::Z:
                value = gyroData[2];
                break;
            case GyroAxis::MAGNITUDE:
                value = std::sqrt(gyroData[0] * gyroData[0] +
                                  gyroData[1] * gyroData[1] +
                                  gyroData[2] * gyroData[2]);
                break;
            default:
                throw std::invalid_argument("Unknown GyroAxis");
        }

        // Apply direction filtering
        switch (direction) {
            case GyroDirection::POSITIVE:
                value = (value > 0) ? value : 0;
                break;
            case GyroDirection::NEGATIVE:
                value = (value < 0) ? value : 0;
                break;
            case GyroDirection::BOTH:
                // No change needed
                break;
            case GyroDirection::ABSOLUTE:
                value = std::fabs(value);
                break;
            default:
                throw std::invalid_argument("Unknown GyroDirection");
        }

        // Apply gain
        value *= gain;

        // Apply EMA filter if any filteringAmount is set
        if (riseFilteringAmount > 0.0f || fallFilteringAmount > 0.0f)
        {
//            filtered_value_ = filterEMA(value, filtered_value_, fallFilteringAmount);
            filtered_value_ = filterEMATwoWays (value, filtered_value_, riseFilteringAmount, fallFilteringAmount);
        }

        return filtered_value_;
    }
    
    enum class TaitBryanOrder {
        XYZ = 0,
        XZY = 1,
        YXZ = 2,
        YZX = 3,
        ZXY = 4,
        ZYX = 5
    };

    /**
     * Converts a unit quaternion (w, x, y, z) to Tait-Bryan Euler angles for
     * the requested rotation order.
     *
     * Returns {a_first, a_last, a_mid} where:
     *   a_first and a_last are in [-pi, pi]  (computed with atan2)
     *   a_mid              is in [-pi/2, pi/2] (the constrained angle, via asin)
     *
     * The middle angle is always the third element so callers can easily
     * identify the gimbal-locked axis.
     */
    static std::array<float, 3> convertQuaternionToEuler (float w, float x, float y, float z,
                                                           TaitBryanOrder order = TaitBryanOrder::XYZ)
    {
        // Rotation matrix elements
        float xx = x*x, yy = y*y, zz = z*z;
        float r00 = 1.f - 2.f*(yy+zz),  r01 = 2.f*(x*y - w*z),  r02 = 2.f*(x*z + w*y);
        float r10 = 2.f*(x*y + w*z),     r11 = 1.f - 2.f*(xx+zz), r12 = 2.f*(y*z - w*x);
        float r20 = 2.f*(x*z - w*y),     r21 = 2.f*(y*z + w*x),   r22 = 1.f - 2.f*(xx+yy);

        auto clamp = [](float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); };

        float a_first, a_last, a_mid;
        switch (order)
        {
            case TaitBryanOrder::XYZ:
                a_mid   = std::asin  (clamp ( r02));
                a_first = std::atan2 (-r12,  r22);
                a_last  = std::atan2 (-r01,  r00);
                break;
            case TaitBryanOrder::XZY:
                a_mid   = std::asin  (clamp (-r01));
                a_first = std::atan2 ( r21,  r11);
                a_last  = std::atan2 ( r02,  r00);
                break;
            case TaitBryanOrder::YXZ:
                a_mid   = std::asin  (clamp (-r12));
                a_first = std::atan2 ( r02,  r22);
                a_last  = std::atan2 ( r10,  r11);
                break;
            case TaitBryanOrder::YZX:
                a_mid   = std::asin  (clamp ( r10));
                a_first = std::atan2 (-r20,  r00);
                a_last  = std::atan2 (-r12,  r11);
                break;
            case TaitBryanOrder::ZXY:
                a_mid   = std::asin  (clamp ( r21));
                a_first = std::atan2 (-r01,  r11);
                a_last  = std::atan2 (-r20,  r22);
                break;
            case TaitBryanOrder::ZYX:
            default:
                a_mid   = std::asin  (clamp (-r20));
                a_first = std::atan2 ( r10,  r00);
                a_last  = std::atan2 ( r21,  r22);
                break;
        }
        return { a_first, a_last, a_mid };
    }
    
    // EMA Filter
    static float filterEMA (float inputValue, float filteredValue, float filteringAmount)
    {
        return ((1 - filteringAmount) * inputValue) + (filteringAmount * filteredValue);
    }
    
    
    float filterEMATwoWays(float inputValue, float filteredValue, float riseFilteringAmount, float fallFilteringAmount)
    {
        float filteringAmount = 0.f;
        if (inputValue > last_filtered_value_)
        {
            filteringAmount = riseFilteringAmount;
        }
        else
        {
            filteringAmount = fallFilteringAmount;
        }
        
        std::cout << "filteringAmount: " << filteringAmount << std::endl;
        
        filteredValue = filterEMA (inputValue, filteredValue, filteringAmount);
        last_raw_value_ = inputValue;
        last_filtered_value_ = filteredValue;
        return filteredValue;
    }
    
    // SCALE
    static float scale (float value, float inMin, float inMax, float outMin, float outMax)
    {
        return outMin + (outMax - outMin) * (value - inMin) / (inMax - inMin);
    }
    
    // SCALE & CLAMP
    static float scaleAndClamp (float value, float inMin, float inMax, float outMin, float outMax)
    {
        if (value < inMin) value = inMin;
        if (value > inMax) value = inMax;
        return scale (value, inMin, inMax, outMin, outMax);
    }

    std::array<float, 4> multiplyQuaternions (const std::array<float, 4>& q1, const std::array<float, 4>& q2)
    {
        float w = q1[0] * q2[0] - q1[1] * q2[1] - q1[2] * q2[2] - q1[3] * q2[3];
        float x = q1[0] * q2[1] + q1[1] * q2[0] + q1[2] * q2[3] - q1[3] * q2[2];
        float y = q1[0] * q2[2] - q1[1] * q2[3] + q1[2] * q2[0] + q1[3] * q2[1];
        float z = q1[0] * q2[3] + q1[1] * q2[2] - q1[2] * q2[1] + q1[3] * q2[0];

        return {w, x, y, z};
    }
    
    //On Change Method
    bool changed (int input_value)
    {
        if (input_value != previous_input_value_)
        {
            previous_input_value_ = input_value;
            return (true);
        }
        else
        {
            return (false);
        }
    }
    
private:
    float toggle_state_;
    float filtered_value_;
    float last_raw_value_;
    float last_filtered_value_;
    int previous_input_value_;


};
